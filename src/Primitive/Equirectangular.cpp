#include "Primitive/Equirectangular.h"

Equirectangular::Equirectangular(std::shared_ptr<ImageCPU<float>> imageCPU,
                                 std::shared_ptr<CommandBuffer> commandBufferTransfer,
                                 std::shared_ptr<EngineState> engineState) {
  _commandBufferTransfer = commandBufferTransfer;
  _engineState = engineState;
  auto currentFrame = _engineState->getFrameInFlight();

  auto pixels = imageCPU->getData().get();
  auto [texWidth, texHeight] = imageCPU->getResolution();

  int imageSize = texWidth * texHeight * STBI_rgb_alpha;
  int bufferSize = imageSize * sizeof(float);
  // fill buffer
  _stagingBuffer = std::make_shared<Buffer>(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                            engineState);
  _stagingBuffer->setData(pixels);

  // image
  auto [width, height] = engineState->getSettings()->getResolution();
  // HDR image is in VK_FORMAT_R32G32B32A32_SFLOAT
  _image = std::make_shared<Image>(std::tuple{texWidth, texHeight}, 1, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
                                   VK_IMAGE_TILING_OPTIMAL,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, engineState);
  _image->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                       commandBufferTransfer);
  _image->copyFrom(_stagingBuffer, {0}, commandBufferTransfer);
  _image->changeLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, commandBufferTransfer);
  _imageView = std::make_shared<ImageView>(_image, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1, VK_IMAGE_ASPECT_COLOR_BIT,
                                           engineState);
  auto filter = VK_FILTER_NEAREST;
  if (_engineState->getDevice()->isFormatFeatureSupported(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
    filter = VK_FILTER_LINEAR;
  }

  _texture = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, filter, _imageView, engineState);
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
  _mesh3D->setVertices(vertices, commandBufferTransfer);
  _mesh3D->setIndexes(indices, commandBufferTransfer);
  _mesh3D->setColor(std::vector<glm::vec3>(vertices.size(), glm::vec3(1.f, 1.f, 1.f)), commandBufferTransfer);
  _material = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, commandBufferTransfer, engineState);
  _material->setBaseColor({_texture});
  {
    std::vector<VkAttachmentDescription> colorDescription{
        {.format = _engineState->getSettings()->getGraphicColorFormat(),
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         // comes from previous stage/initialization
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         // goes to render shader as input
         .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};

    // we want write to this attachments
    VkAttachmentReference colorReference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    _renderPass = std::make_shared<RenderPass>(_engineState->getDevice());
    _renderPass->initializeCustom(colorDescription, {colorReference}, std::nullopt);
  }

  // initialize camera UBO and descriptor sets for draw
  // initialize UBO
  _bufferCubemap.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _bufferCubemap[i].resize(6);
    for (int f = 0; f < 6; f++) {
      _bufferCubemap[i][f] = std::make_shared<Buffer>(
          sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, engineState);
    }
  }
  // setup color
  {
    _descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutColor{{.binding = 0,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr}};
    _descriptorSetLayout->createCustom(layoutColor);

    _descriptorSetCubemap.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetCubemap[i].resize(6);
      for (int f = 0; f < 6; f++) {
        _descriptorSetCubemap[i][f] = std::make_shared<DescriptorSet>(_descriptorSetLayout, engineState);
        std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
            {0,
             {VkDescriptorBufferInfo{.buffer = _bufferCubemap[i][f]->getData(),
                                     .offset = 0,
                                     .range = _bufferCubemap[i][f]->getSize()}}}};
        std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
            {1,
             {VkDescriptorImageInfo{
                 .sampler = _texture->getSampler()->getSampler(),
                 .imageView = _texture->getImageView()->getImageView(),
                 .imageLayout = _texture->getImageView()->getImage()->getImageLayout(),
             }}}};
        _descriptorSetCubemap[i][f]->createCustom(bufferInfoColor, textureInfoColor);
      }
    }
    {
      auto shader = std::make_shared<Shader>(engineState);
      shader->add("shaders/IBL/skyboxEquirectangular_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/IBL/skyboxEquirectangular_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      _pipelineEquirectangular = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                                   _engineState->getDevice());
      _pipelineEquirectangular->setDepthTest(true);
      _pipelineEquirectangular->setDepthWrite(true);
      _pipelineEquirectangular->createCustom(
          shader, {std::pair{std::string("color"), _descriptorSetLayout}}, {}, _mesh3D->getBindingDescription(),
          _mesh3D->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}}),
          _renderPass);
    }
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

  _frameBuffer.resize(6);
  for (int i = 0; i < 6; i++) {
    auto currentTexture = _cubemap->getTextureSeparate()[i][0];
    _frameBuffer[i] = std::make_shared<Framebuffer>(std::vector{currentTexture->getImageView()},
                                                    currentTexture->getImageView()->getImage()->getResolution(),
                                                    _renderPass, _engineState->getDevice());
  }

  auto currentFrame = _engineState->getFrameInFlight();
  vkCmdBindPipeline(_commandBufferTransfer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _pipelineEquirectangular->getPipeline());
  auto [width, height] = _engineState->getSettings()->getShadowMapResolution();
  // render equirectangular to cubemap
  /////////////////////////////////////////////////////////////////////////////////////////
  // render graphic
  /////////////////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < 6; i++) {
    auto currentTexture = _cubemap->getTextureSeparate()[i][0];

    auto [widthFramebuffer, heightFramebuffer] = _frameBuffer[i]->getResolution();
    VkClearValue clearColor{.color = _engineState->getSettings()->getClearColor()};
    VkRenderPassBeginInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                         .renderPass = _renderPass->getRenderPass(),
                                         .framebuffer = _frameBuffer[i]->getBuffer(),
                                         .renderArea = {.offset = {0, 0},
                                                        .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                                                   .height = static_cast<uint32_t>(heightFramebuffer)}},
                                         .clearValueCount = 1,
                                         .pClearValues = &clearColor};

    // TODO: only one depth texture?
    vkCmdBeginRenderPass(_commandBufferTransfer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

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
    _bufferCubemap[currentFrame][i]->setData(&cameraUBO);

    VkBuffer vertexBuffers[] = {_mesh3D->getVertexBuffer()->getBuffer()->getData()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(_commandBufferTransfer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

    vkCmdBindIndexBuffer(_commandBufferTransfer->getCommandBuffer(), _mesh3D->getIndexBuffer()->getBuffer()->getData(),
                         0, VK_INDEX_TYPE_UINT32);

    auto pipelineLayout = _pipelineEquirectangular->getDescriptorSetLayout();
    auto colorLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                    [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                      return info.first == std::string("color");
                                    });
    if (colorLayout != pipelineLayout.end()) {
      vkCmdBindDescriptorSets(_commandBufferTransfer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                              _pipelineEquirectangular->getPipelineLayout(), 0, 1,
                              &_descriptorSetCubemap[currentFrame][i]->getDescriptorSets(), 0, nullptr);
    }

    vkCmdDrawIndexed(_commandBufferTransfer->getCommandBuffer(), static_cast<uint32_t>(_mesh3D->getIndexData().size()),
                     1, 0, 0, 0);
    logger->end(_commandBufferTransfer);

    vkCmdEndRenderPass(_commandBufferTransfer->getCommandBuffer());

    VkImageMemoryBarrier colorBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                      .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      .image = currentTexture->getImageView()->getImage()->getImage(),
                                      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                           .baseMipLevel = 0,
                                                           .levelCount = 1,
                                                           .baseArrayLayer = 0,
                                                           .layerCount = 1}};
    vkCmdPipelineBarrier(_commandBufferTransfer->getCommandBuffer(),
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,          // dstStageMask
                         0, 0, nullptr, 0, nullptr,
                         1,             // imageMemoryBarrierCount
                         &colorBarrier  // pImageMemoryBarriers
    );
  }
}

std::shared_ptr<Texture> Equirectangular::getTexture() { return _texture; }

std::shared_ptr<Cubemap> Equirectangular::getCubemap() { return _cubemap; }
