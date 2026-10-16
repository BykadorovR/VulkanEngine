#include <cstring>
#include <algorithm>
#include <chrono>
#include "Graphic/IBL.h"

import Shader;

struct FragmentPush {
  float roughness;
};

IBL::IBL(const RenderGraph::CommandBuffer* commandBufferTransfer,
         std::shared_ptr<GameState> gameState,
         std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  _commandBufferTransfer = commandBufferTransfer;
  _mesh3D = std::make_shared<MeshStatic3D>(engineState);
  _mesh2D = std::make_shared<MeshStatic2D>(engineState);
  _gameState = gameState;

  std::vector<Vertex3D> vertices{{.pos = glm::vec3(-0.5, -0.5, 0.5)},  {.pos = glm::vec3(0.5, -0.5, 0.5)},
                                 {.pos = glm::vec3(-0.5, 0.5, 0.5)},   {.pos = glm::vec3(0.5, 0.5, 0.5)},
                                 {.pos = glm::vec3(-0.5, -0.5, -0.5)}, {.pos = glm::vec3(0.5, -0.5, -0.5)},
                                 {.pos = glm::vec3(-0.5, 0.5, -0.5)},  {.pos = glm::vec3(0.5, 0.5, -0.5)}};
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
  // 3   0
  // 2   1
  _mesh2D->setVertices(
      {Vertex2D{{0.5f, 0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}},
       Vertex2D{{0.5f, -0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
       Vertex2D{{-0.5f, -0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
       Vertex2D{{-0.5f, 0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 0.f}, {1.f, 0.f, 0.f, 1.f}}});
  _mesh2D->setIndexes({0, 3, 2, 2, 1, 0});

  // TODO: should be texture zero??
  _material = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, engineState);
  std::dynamic_pointer_cast<MaterialColor>(_material)->setBaseColor(
      {_gameState->getResourceManager()->getCubemapOne()->getTexture()});

  // setup BRDF
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/IBL/specularBRDF_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/IBL/specularBRDF_fragment.spv"));
    _descriptorSetLayoutBRDF = std::make_shared<RenderGraph::DescriptorSetLayout>(*engineState->getDevice());
    _descriptorSetLayoutBRDF->createCustom(shader->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetBRDF.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
      _descriptorSetBRDF[i] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{_descriptorSetLayoutBRDF.get()},
          *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
    _cameraBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _cameraBuffer[i] = std::make_shared<RenderGraph::Buffer>(
          sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
          *engineState->getMemoryAllocator());

      _descriptorSetBRDF[i]->add({_cameraBuffer[i].get()});
      _descriptorSetBRDF[i]->initialize(*commandBufferTransfer);
    }

    RenderGraph::PipelineGraphic pipelineGraphic;
    pipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    pipelineGraphic.setDepthAttachment(std::nullopt);
    pipelineGraphic.setDepthTest(false);
    pipelineGraphic.setDepthWrite(false);
    std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_descriptorSetLayoutBRDF.get()};
    const auto vertexBinding = _mesh2D->getBindingDescription();
    const auto vertexAttributes = _mesh2D->getAttributeDescriptions(
        {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
         {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}});
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertexBinding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
        .pVertexAttributeDescriptions = vertexAttributes.data(),
    };
    _pipelineSpecularBRDF = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelineSpecularBRDF->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts, {},
                                         vertexInput);
  }

  // initialize camera UBO and descriptor sets for draw
  // initialize UBO
  _cameraBufferCubemap.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _cameraBufferCubemap[i].resize(6);
    for (int f = 0; f < 6; f++) {
      _cameraBufferCubemap[i][f] = std::make_shared<RenderGraph::Buffer>(
          sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
          *engineState->getMemoryAllocator());
    }
  }

  // setup diffuse and specular
  {
    auto shaderDiffuse = std::make_shared<RenderGraph::Shader>(*engineState->getDevice());
    shaderDiffuse->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                     "shaders/IBL/skyboxDiffuse_vertex.spv"));
    shaderDiffuse->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                     "shaders/IBL/skyboxDiffuse_fragment.spv"));
    _descriptorSetLayoutColor = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    _descriptorSetLayoutColor->createCustom(shaderDiffuse->getDescriptorSetLayoutBindings().at(0));

    _descriptorSetColor.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetColor[i].resize(6);
      for (int f = 0; f < 6; f++) {
        _descriptorSetColor[i][f] = std::make_shared<RenderGraph::DescriptorSet>(
            std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayoutColor).get()},
            *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
      }
    }

    _updateColorDescriptor();

    RenderGraph::PipelineGraphic diffusePipelineGraphic;
    diffusePipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    diffusePipelineGraphic.setDepthAttachment(std::nullopt);
    diffusePipelineGraphic.setDepthTest(false);
    diffusePipelineGraphic.setDepthWrite(false);
    std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_descriptorSetLayoutColor.get()};
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
    _pipelineDiffuse = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelineDiffuse->createGraphic(diffusePipelineGraphic, shaderDiffuse->getShaderStageInfo(), descriptorSetLayouts,
                                    {}, vertexInput);
    {
      std::unordered_map<std::string, VkPushConstantRange> defaultPushConstants;
      defaultPushConstants["fragment"] =
          VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(FragmentPush)};

      auto shader = std::make_shared<RenderGraph::Shader>(*engineState->getDevice());
      shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                "shaders/IBL/skyboxSpecular_vertex.spv"));
      shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                "shaders/IBL/skyboxSpecular_fragment.spv"));
      RenderGraph::PipelineGraphic specularPipelineGraphic;
      specularPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      specularPipelineGraphic.setDepthAttachment(std::nullopt);
      specularPipelineGraphic.setDepthTest(false);
      specularPipelineGraphic.setDepthWrite(false);
      _pipelineSpecular = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipelineSpecular->createGraphic(specularPipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts,
                                       defaultPushConstants, vertexInput);
    }
  }

  _cubemapDiffuse = std::make_shared<Cubemap>(_engineState->getSettings()->getDiffuseIBLResolution(),
                                              _engineState->getSettings()->getGraphicColorFormat(), 1,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
                                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                              VK_FILTER_LINEAR, commandBufferTransfer, engineState);
  _cubemapSpecular = std::make_shared<Cubemap>(
      _engineState->getSettings()->getSpecularIBLResolution(), _engineState->getSettings()->getGraphicColorFormat(),
      _engineState->getSettings()->getSpecularMipMap(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FILTER_LINEAR,
      commandBufferTransfer, engineState);
  const auto [brdfWidth, brdfHeight] = _engineState->getSettings()->getShadowMapResolution();
  auto brdfImage = std::make_unique<RenderGraph::Image>(*engineState->getMemoryAllocator());
  brdfImage->createImage(_engineState->getSettings()->getGraphicColorFormat(), {brdfWidth, brdfHeight}, 1, 1,
                         VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  brdfImage->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                          VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                          VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, *commandBufferTransfer);
  auto brdfImageView = std::make_shared<RenderGraph::ImageView>(std::move(brdfImage), *engineState->getDevice());
  brdfImageView->createImageView(VK_IMAGE_VIEW_TYPE_2D, 0, 0);
  auto brdfSampler = std::make_shared<RenderGraph::Sampler>(*engineState->getDevice());
  brdfSampler->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1,
                             engineState->getSettings()->getAnisotropicSamples(), VK_FILTER_LINEAR);
  _textureSpecularBRDF = std::make_shared<RenderGraph::Texture>(std::move(brdfImageView), std::move(brdfSampler));

  _cameraSpecularBRDF = std::make_shared<CameraOrtho>();
  _cameraSpecularBRDF->setProjectionParameters({-1, 1, 1, -1}, 0, 1);
  _cameraSpecularBRDF->setViewParameters(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));

  _camera = std::make_shared<CameraPerspective>(_engineState);
  _camera->setViewParameters(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, -1.f), glm::vec3(0.f, 1.f, 0.f));
  _camera->setProjectionParameters(90.f, 0.1f, 100.f);
  _camera->setAspect(1.f);
}

std::shared_ptr<Cubemap> IBL::getCubemapDiffuse() { return _cubemapDiffuse; }

std::shared_ptr<Cubemap> IBL::getCubemapSpecular() { return _cubemapSpecular; }

std::shared_ptr<RenderGraph::Texture> IBL::getTextureSpecularBRDF() { return _textureSpecularBRDF; }

void IBL::_updateColorDescriptor() {
  _descriptorSetColor.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    for (int f = 0; f < 6; f++) {
      _descriptorSetColor[i][f] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayoutColor).get()},
          *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
      _descriptorSetColor[i][f]->add({_cameraBufferCubemap[i][f].get()});
      _descriptorSetColor[i][f]->add({_material->getBaseColor()[0].get()});
      _descriptorSetColor[i][f]->initialize(*_commandBufferTransfer);
    }
  }
}

void IBL::setMaterial(std::shared_ptr<MaterialColor> material) {
  _material = material;
  _updateColorDescriptor();
}

void IBL::setPosition(glm::vec3 position) {
  _model = glm::translate(glm::mat4(1.f), position);
  _camera->setViewParameters(position, glm::vec3(0.f, 0.f, -1.f), glm::vec3(0.f, 1.f, 0.f));
}

void IBL::_draw(int face,
                std::shared_ptr<Camera> camera,
                const RenderGraph::CommandBuffer* commandBuffer,
                std::shared_ptr<RenderGraph::Pipeline> pipeline) {
  auto currentFrame = _engineState->getFrameInFlight();
  BufferMVP cameraUBO{.model = _model, .view = camera->getView(), .projection = camera->getProjection()};

  std::memcpy(static_cast<std::byte*>(_cameraBufferCubemap[currentFrame][face]->getAllocationInfo().pMappedData),
              &cameraUBO, static_cast<std::size_t>(_cameraBufferCubemap[currentFrame][face]->getSize()));

  VkBuffer vertexBuffers[] = {_mesh3D->getVertexBuffer()->getBuffer()->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), _mesh3D->getIndexBuffer()->getBuffer()->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  _descriptorSetColor[currentFrame][face]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(),
                                                *commandBuffer);

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh3D->getIndexData().size()), 1, 0, 0,
                   0);
}

void IBL::drawSpecular() {
  auto logger = _engineState->getLogger();

  vkCmdBindPipeline(_commandBufferTransfer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _pipelineSpecular->getPipeline());
  auto& specularImage = _cubemapSpecular->getTexture()->getImageView().getImage();
  if (specularImage.getImageLayout() != VK_IMAGE_LAYOUT_GENERAL)
    specularImage.changeLayout(
        specularImage.getImageLayout(), VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, *_commandBufferTransfer);

  /////////////////////////////////////////////////////////////////////////////////////////
  // render graphic
  /////////////////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < _engineState->getSettings()->getSpecularMipMap(); j++) {
      auto currentTexture = _cubemapSpecular->getTextureSeparate()[i][j];
      const auto imageResolution = currentTexture->getImageView().getImage().getResolution();
      auto widthFramebuffer = static_cast<uint32_t>(imageResolution.x * std::pow(0.5, j));
      auto heightFramebuffer = static_cast<uint32_t>(imageResolution.y * std::pow(0.5, j));
      VkRenderingAttachmentInfo colorAttachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                                .imageView = currentTexture->getImageView().getImageView(),
                                                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                                .clearValue = {.color = {.float32 = {0.f, 0.f, 0.f, 1.f}}}};
      VkRenderingInfo renderingInfo{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                    .renderArea = {.offset = {0, 0}, .extent = {widthFramebuffer, heightFramebuffer}},
                                    .layerCount = 1,
                                    .colorAttachmentCount = 1,
                                    .pColorAttachments = &colorAttachment};
      vkCmdBeginRendering(_commandBufferTransfer->getCommandBuffer(), &renderingInfo);

      logger->begin("Render specular", _commandBufferTransfer);
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

      const auto [baseWidth, baseHeight] = _engineState->getSettings()->getSpecularIBLResolution();
      const int width = std::max(1, baseWidth >> j);
      const int height = std::max(1, baseHeight >> j);
      VkViewport viewport{.x = 0.f,
                          .y = 0.f,
                          .width = static_cast<float>(width),
                          .height = static_cast<float>(height),
                          .minDepth = 0.0f,
                          .maxDepth = 1.0f};
      vkCmdSetViewport(_commandBufferTransfer->getCommandBuffer(), 0, 1, &viewport);

      VkRect2D scissor{.offset = {0, 0},
                       .extent = VkExtent2D(static_cast<uint32_t>(width), static_cast<uint32_t>(height))};
      vkCmdSetScissor(_commandBufferTransfer->getCommandBuffer(), 0, 1, &scissor);

      if (_pipelineSpecular->getPushConstants().find("fragment") != _pipelineSpecular->getPushConstants().end()) {
        FragmentPush pushConstants;
        float roughness = static_cast<float>(j) /
                          static_cast<float>(_engineState->getSettings()->getSpecularMipMap() - 1);
        pushConstants.roughness = roughness;
        auto info = _pipelineSpecular->getPushConstants().at("fragment");
        vkCmdPushConstants(_commandBufferTransfer->getCommandBuffer(), _pipelineSpecular->getPipelineLayout(),
                           info.stageFlags, info.offset, info.size, &pushConstants);
      }

      _draw(i, _camera, _commandBufferTransfer, _pipelineSpecular);

      logger->end(_commandBufferTransfer);

      vkCmdEndRendering(_commandBufferTransfer->getCommandBuffer());
    }
  }
}

void IBL::drawDiffuse() {
  auto logger = _engineState->getLogger();

  // render cubemap to diffuse
  vkCmdBindPipeline(_commandBufferTransfer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _pipelineDiffuse->getPipeline());
  auto& diffuseImage = _cubemapDiffuse->getTexture()->getImageView().getImage();
  if (diffuseImage.getImageLayout() != VK_IMAGE_LAYOUT_GENERAL)
    diffuseImage.changeLayout(
        diffuseImage.getImageLayout(), VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, *_commandBufferTransfer);
  /////////////////////////////////////////////////////////////////////////////////////////
  // render graphic
  /////////////////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < 6; i++) {
    auto currentTexture = _cubemapDiffuse->getTextureSeparate()[i][0];
    auto [widthFramebuffer, heightFramebuffer] = std::tuple{
        (currentTexture->getImageView().getImage()).getResolution().x,
        (currentTexture->getImageView().getImage()).getResolution().y};
    VkRenderingAttachmentInfo colorAttachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                              .imageView = currentTexture->getImageView().getImageView(),
                                              .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                              .clearValue = {.color = {.float32 = {0.f, 0.f, 0.f, 1.f}}}};
    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0},
                       .extent = {static_cast<uint32_t>(widthFramebuffer), static_cast<uint32_t>(heightFramebuffer)}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment};
    vkCmdBeginRendering(_commandBufferTransfer->getCommandBuffer(), &renderingInfo);

    logger->begin("Render diffuse", _commandBufferTransfer);
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
    auto [width, height] = _engineState->getSettings()->getDiffuseIBLResolution();
    VkViewport viewport{.x = 0.f,
                        .y = 0.f,
                        .width = static_cast<float>(width),
                        .height = static_cast<float>(height),
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f};
    vkCmdSetViewport(_commandBufferTransfer->getCommandBuffer(), 0, 1, &viewport);

    VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(width, height)};
    vkCmdSetScissor(_commandBufferTransfer->getCommandBuffer(), 0, 1, &scissor);
    _draw(i, _camera, _commandBufferTransfer, _pipelineDiffuse);

    logger->end(_commandBufferTransfer);

    vkCmdEndRendering(_commandBufferTransfer->getCommandBuffer());
  }
}

void IBL::drawSpecularBRDF() {
  auto logger = _engineState->getLogger();

  auto resolution = std::tuple{(_textureSpecularBRDF->getImageView().getImage()).getResolution().x,
                               (_textureSpecularBRDF->getImageView().getImage()).getResolution().y};
  int currentFrame = _engineState->getFrameInFlight();
  VkViewport viewport{.x = 0.0f,
                      .y = static_cast<float>(std::get<1>(resolution)),
                      .width = static_cast<float>(std::get<0>(resolution)),
                      .height = static_cast<float>(-std::get<1>(resolution)),
                      .minDepth = 0.0f,
                      .maxDepth = 1.0f};
  vkCmdSetViewport(_commandBufferTransfer->getCommandBuffer(), 0, 1, &viewport);

  VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(std::get<0>(resolution), std::get<1>(resolution))};
  vkCmdSetScissor(_commandBufferTransfer->getCommandBuffer(), 0, 1, &scissor);

  auto& brdfImage = _textureSpecularBRDF->getImageView().getImage();
  if (brdfImage.getImageLayout() != VK_IMAGE_LAYOUT_GENERAL)
    brdfImage.changeLayout(brdfImage.getImageLayout(), VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, *_commandBufferTransfer);
  VkRenderingAttachmentInfo colorAttachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                            .imageView = _textureSpecularBRDF->getImageView().getImageView(),
                                            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                            .clearValue = {.color = {.float32 = {0.f, 0.f, 0.f, 1.f}}}};
  VkRenderingInfo renderingInfo{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                .renderArea = {.offset = {0, 0},
                                               .extent = {static_cast<uint32_t>(std::get<0>(resolution)),
                                                          static_cast<uint32_t>(std::get<1>(resolution))}},
                                .layerCount = 1,
                                .colorAttachmentCount = 1,
                                .pColorAttachments = &colorAttachment};
  vkCmdBeginRendering(_commandBufferTransfer->getCommandBuffer(), &renderingInfo);

  logger->begin("Render specular brdf", _commandBufferTransfer);
  auto model = glm::scale(_model, glm::vec3(2.f, 2.f, 1.f));
  BufferMVP cameraMVP{.model = model,
                      .view = _cameraSpecularBRDF->getView(),
                      .projection = _cameraSpecularBRDF->getProjection()};
  std::memcpy(static_cast<std::byte*>(_cameraBuffer[currentFrame]->getAllocationInfo().pMappedData), &cameraMVP,
              static_cast<std::size_t>(_cameraBuffer[currentFrame]->getSize()));

  VkBuffer vertexBuffers[] = {_mesh2D->getVertexBuffer()->getBuffer()->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(_commandBufferTransfer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(_commandBufferTransfer->getCommandBuffer(), _mesh2D->getIndexBuffer()->getBuffer()->getBuffer(),
                       0, VK_INDEX_TYPE_UINT32);

  vkCmdBindPipeline(_commandBufferTransfer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _pipelineSpecularBRDF->getPipeline());

  _descriptorSetBRDF[currentFrame]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineSpecularBRDF->getPipelineLayout(),
                                         *_commandBufferTransfer);

  vkCmdDrawIndexed(_commandBufferTransfer->getCommandBuffer(), static_cast<uint32_t>(_mesh2D->getIndexData().size()), 1,
                   0, 0, 0);
  logger->end(_commandBufferTransfer);

  vkCmdEndRendering(_commandBufferTransfer->getCommandBuffer());
}
