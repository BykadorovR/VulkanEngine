#include <cstring>
#include <future>
#include "stb_image.h"
#include "Primitive/Equirectangular.h"

// Keep standard-library declarations outside the imported module with MSVC.
import Shader;

Equirectangular::Equirectangular(std::shared_ptr<ImageCPU<float>> imageCPU,
                                 const RenderGraph::CommandBuffer* commandBufferTransfer,
                                 std::shared_ptr<EngineState> engineState) {
  _commandBufferTransfer = commandBufferTransfer;
  _engineState = engineState;
  auto pixels = imageCPU->getData().get();
  auto [texWidth, texHeight] = imageCPU->getResolution();

  int imageSize = texWidth * texHeight * STBI_rgb_alpha;
  int bufferSize = imageSize * sizeof(float);
  // fill buffer
  _stagingBuffer = std::make_shared<RenderGraph::Buffer>(
      bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      *engineState->getMemoryAllocator());
  std::memcpy(static_cast<std::byte*>(_stagingBuffer->getAllocationInfo().pMappedData), pixels,
              static_cast<std::size_t>(_stagingBuffer->getSize()));

  // image
  auto [width, height] = engineState->getSettings()->getResolution();
  // HDR image is in VK_FORMAT_R32G32B32A32_SFLOAT
  _image = std::make_unique<RenderGraph::Image>(*engineState->getMemoryAllocator());
  _image->createImage(VK_FORMAT_R32G32B32A32_SFLOAT, {texWidth, texHeight}, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  const VkDeviceSize imageOffset = 0;
  const auto* imageBytes = static_cast<const std::byte*>(_stagingBuffer->getAllocationInfo().pMappedData);
  engineState->getResourceUploader()->upload(
      *_image, std::span(imageBytes, static_cast<std::size_t>(_stagingBuffer->getSize())), std::span(&imageOffset, 1));
  engineState->getUploadTickets().push_back(engineState->getResourceUploader()->submit());

  _imageView = std::make_shared<RenderGraph::ImageView>(std::move(_image), *engineState->getDevice());
  _imageView->createImageView(VK_IMAGE_VIEW_TYPE_2D, 0, 0);
  auto filter = VK_FILTER_NEAREST;
  if (_engineState->getDevice()->isFormatFeatureSupported(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
    filter = VK_FILTER_LINEAR;
  }

  auto sampler = std::make_shared<RenderGraph::Sampler>(*engineState->getDevice());
  sampler->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, engineState->getSettings()->getAnisotropicSamples(),
                         filter);
  _texture = std::make_shared<RenderGraph::Texture>(_imageView, std::move(sampler));
  // convert to cubemap
  _mesh3D = std::make_shared<MeshStatic3D>(engineState);

  std::vector<Vertex3D> vertices{{.pos = glm::vec3(-0.5, -0.5, 0.5)},   // 0
                                 {.pos = glm::vec3(0.5, -0.5, 0.5)},    // 1
                                 {.pos = glm::vec3(-0.5, 0.5, 0.5)},    // 2
                                 {.pos = glm::vec3(0.5, 0.5, 0.5)},     // 3
                                 {.pos = glm::vec3(-0.5, -0.5, -0.5)},  // 4
                                 {.pos = glm::vec3(0.5, -0.5, -0.5)},   // 5
                                 {.pos = glm::vec3(-0.5, 0.5, -0.5)},   // 6
                                 {.pos = glm::vec3(0.5, 0.5, -0.5)}};   // 7

  std::vector<uint32_t> indices{                    // Bottom
                                0, 4, 5, 5, 1, 0,   // ccw if look to this face from down
                                                    //  Top
                                2, 3, 7, 7, 6, 2,   // ccw if look to this face from up
                                                    //  Left
                                0, 2, 6, 6, 4, 0,   // ccw if look to this face from left
                                                    //  Right
                                1, 5, 7, 7, 3, 1,   // ccw if look to this face from right
                                                    //  Front
                                1, 3, 2, 2, 0, 1,   // ccw if look to this face from front
                                                    //  Back
                                5, 4, 6, 6, 7, 5};  // ccw if look to this face from back
  _mesh3D->setVertices(vertices);
  _mesh3D->setIndexes(indices);
  _mesh3D->setColor(std::vector<glm::vec3>(vertices.size(), glm::vec3(1.f, 1.f, 1.f)));
  _material = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, engineState);
  _material->setBaseColor({_texture});

  // initialize camera UBO and descriptor sets for draw
  // initialize UBO
  _bufferCubemap.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _bufferCubemap[i].resize(6);
    for (int f = 0; f < 6; f++) {
      _bufferCubemap[i][f] = std::make_shared<RenderGraph::Buffer>(
          sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
          *engineState->getMemoryAllocator());
    }
  }
  // setup color
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/IBL/skyboxEquirectangular_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/IBL/skyboxEquirectangular_fragment.spv"));
    _descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    _descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));

    _descriptorSetCubemap.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetCubemap[i].resize(6);
      for (int f = 0; f < 6; f++) {
        _descriptorSetCubemap[i][f] = std::make_shared<RenderGraph::DescriptorSet>(
            std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout).get()},
            *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
        _descriptorSetCubemap[i][f]->add({_bufferCubemap[i][f].get()});
        _descriptorSetCubemap[i][f]->add({_texture.get()});
        _descriptorSetCubemap[i][f]->initialize(*commandBufferTransfer);
      }
    }
    RenderGraph::PipelineGraphic pipelineGraphic;
    pipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    pipelineGraphic.setDepthAttachment(std::nullopt);
    pipelineGraphic.setDepthTest(false);
    pipelineGraphic.setDepthWrite(false);
    std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_descriptorSetLayout.get()};
    const auto vertexBinding = _mesh3D->getBindingDescription();
    const auto vertexAttributes = _mesh3D->getAttributeDescriptions(
        {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}});
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertexBinding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
        .pVertexAttributeDescriptions = vertexAttributes.data(),
    };
    _pipelineEquirectangular = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelineEquirectangular->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts, {},
                                            vertexInput);
  }

  _camera = std::make_shared<CameraFly>(_engineState);
  _camera->setViewParameters(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, -1.f), glm::vec3(0.f, 1.f, 0.f));
  _camera->setProjectionParameters(90.f, 0.1f, 100.f);
  _camera->setAspect(1.f);

  _convertToCubemap();
}

void Equirectangular::_convertToCubemap() {
  auto logger = _engineState->getLogger();

  _cubemap = std::make_shared<Cubemap>(_engineState->getSettings()->getShadowMapResolution(),
                                       _engineState->getSettings()->getGraphicColorFormat(), 1,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                       VK_FILTER_LINEAR, _commandBufferTransfer, _engineState);

  auto currentFrame = _engineState->getFrameInFlight();
  vkCmdBindPipeline(_commandBufferTransfer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _pipelineEquirectangular->getPipeline());
  auto [width, height] = _engineState->getSettings()->getShadowMapResolution();
  auto& cubemapImage = _cubemap->getTexture()->getImageView().getImage();
  cubemapImage.changeLayout(
      cubemapImage.getImageLayout(), VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, *_commandBufferTransfer);
  // render equirectangular to cubemap
  /////////////////////////////////////////////////////////////////////////////////////////
  // render graphic
  /////////////////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < 6; i++) {
    auto currentTexture = _cubemap->getTextureSeparate()[i][0];

    VkRenderingAttachmentInfo colorAttachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                              .imageView = currentTexture->getImageView().getImageView(),
                                              .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                              .clearValue = {.color = {.float32 = {0.f, 0.f, 0.f, 1.f}}}};
    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment};
    vkCmdBeginRendering(_commandBufferTransfer->getCommandBuffer(), &renderingInfo);

    logger->begin("Render equirectangular", _commandBufferTransfer);
    // up is inverted for X and Z because of some specific cubemap Y coordinate stuff
    switch (i) {
      case 0:
        // POSITIVE_X / right
        _camera->setViewParameters(glm::vec3(0.f, 0.f, 0.f), glm::vec3(1.f, 0.f, 0.f), glm::vec3(0.f, -1.f, 0.f));
        break;
      case 1:
        // NEGATIVE_X /left
        _camera->setViewParameters(glm::vec3(0.f, 0.f, 0.f), glm::vec3(-1.f, 0.f, 0.f), glm::vec3(0.f, -1.f, 0.f));
        break;
      case 2:
        // POSITIVE_Y / top
        _camera->setViewParameters(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 0.f, 1.f));
        break;
      case 3:
        // NEGATIVE_Y / bottom
        _camera->setViewParameters(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, -1.f, 0.f), glm::vec3(0.f, 0.f, -1.f));
        break;
      case 4:
        // POSITIVE_Z / near
        _camera->setViewParameters(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, 1.f), glm::vec3(0.f, -1.f, 0.f));
        break;
      case 5:
        // NEGATIVE_Z / far
        _camera->setViewParameters(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, -1.f), glm::vec3(0.f, -1.f, 0.f));
        break;
    }
    VkViewport viewport{.x = 0.f,
                        .y = 0.f,
                        .width = static_cast<float>(width),
                        .height = static_cast<float>(height),
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f};
    vkCmdSetViewport(_commandBufferTransfer->getCommandBuffer(), 0, 1, &viewport);

    VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(width, height)};
    vkCmdSetScissor(_commandBufferTransfer->getCommandBuffer(), 0, 1, &scissor);
    BufferMVP cameraUBO{.model = glm::mat4(1.f), .view = _camera->getView(), .projection = _camera->getProjection()};
    std::memcpy(static_cast<std::byte*>(_bufferCubemap[currentFrame][i]->getAllocationInfo().pMappedData), &cameraUBO,
                static_cast<std::size_t>(_bufferCubemap[currentFrame][i]->getSize()));

    VkBuffer vertexBuffers[] = {_mesh3D->getVertexBuffer()->getBuffer()->getBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(_commandBufferTransfer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

    vkCmdBindIndexBuffer(_commandBufferTransfer->getCommandBuffer(),
                         _mesh3D->getIndexBuffer()->getBuffer()->getBuffer(), 0, VK_INDEX_TYPE_UINT32);

    _descriptorSetCubemap[currentFrame][i]->bind(
        VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineEquirectangular->getPipelineLayout(), *_commandBufferTransfer);

    vkCmdDrawIndexed(_commandBufferTransfer->getCommandBuffer(), static_cast<uint32_t>(_mesh3D->getIndexData().size()),
                     1, 0, 0, 0);
    logger->end(_commandBufferTransfer);

    vkCmdEndRendering(_commandBufferTransfer->getCommandBuffer());
  }
  cubemapImage.changeLayout(
      cubemapImage.getImageLayout(), VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, *_commandBufferTransfer);
}

std::shared_ptr<RenderGraph::Texture> Equirectangular::getTexture() { return _texture; }

std::shared_ptr<Cubemap> Equirectangular::getCubemap() { return _cubemap; }
