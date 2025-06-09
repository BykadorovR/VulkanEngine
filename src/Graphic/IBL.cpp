#include "Graphic/IBL.h"

struct FragmentPush {
  float roughness;
};

IBL::IBL(std::shared_ptr<CommandBuffer> commandBufferTransfer,
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
  _mesh3D->setVertices(vertices, commandBufferTransfer);
  _mesh3D->setIndexes(indices, commandBufferTransfer);
  _mesh3D->setColor(std::vector<glm::vec3>(vertices.size(), glm::vec3(1.f, 1.f, 1.f)), commandBufferTransfer);
  // 3   0
  // 2   1
  _mesh2D->setVertices(
      {Vertex2D{{0.5f, 0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}},
       Vertex2D{{0.5f, -0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
       Vertex2D{{-0.5f, -0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
       Vertex2D{{-0.5f, 0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 0.f}, {1.f, 0.f, 0.f, 1.f}}},
      commandBufferTransfer);
  _mesh2D->setIndexes({0, 3, 2, 2, 1, 0}, commandBufferTransfer);

  // TODO: should be texture zero??
  _material = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, commandBufferTransfer, engineState);
  std::dynamic_pointer_cast<MaterialColor>(_material)->setBaseColor(
      {_gameState->getResourceManager()->getCubemapOne()->getTexture()});

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

  // setup BRDF
  {
    auto cameraLayout = std::make_shared<DescriptorSetLayout>(engineState->getDevice());
    VkDescriptorSetLayoutBinding layoutBinding = {.binding = 0,
                                                  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                  .descriptorCount = 1,
                                                  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                                  .pImmutableSamplers = nullptr};
    cameraLayout->createCustom({layoutBinding});
    _descriptorSetBRDF.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
      _descriptorSetBRDF[i] = std::make_shared<DescriptorSet>(cameraLayout, engineState);
    _cameraBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _cameraBuffer[i] = std::make_shared<Buffer>(
          sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, engineState);

      std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
          {0,
           {VkDescriptorBufferInfo{.buffer = _cameraBuffer[i]->getData(),
                                   .offset = 0,
                                   .range = _cameraBuffer[i]->getSize()}}}};
      _descriptorSetBRDF[i]->createCustom(bufferInfoColor, {});
    }

    {
      auto shader = std::make_shared<Shader>(engineState);
      shader->add("shaders/IBL/specularBRDF_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/IBL/specularBRDF_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      _pipelineSpecularBRDF = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                                _engineState->getDevice());
      _pipelineSpecularBRDF->setDepthTest(true);
      _pipelineSpecularBRDF->setDepthWrite(true);
      _pipelineSpecularBRDF->createCustom(
          shader, {{"brdf", cameraLayout}}, {}, _mesh2D->getBindingDescription(),
          _mesh2D->Mesh2D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
                                                     {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}}),
          _renderPass);
    }
  }

  // initialize camera UBO and descriptor sets for draw
  // initialize UBO
  _cameraBufferCubemap.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _cameraBufferCubemap[i].resize(6);
    for (int f = 0; f < 6; f++) {
      _cameraBufferCubemap[i][f] = std::make_shared<Buffer>(
          sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, engineState);
    }
  }

  // setup diffuse and specular
  {
    _descriptorSetLayoutColor = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
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
    _descriptorSetLayoutColor->createCustom(layoutColor);

    _descriptorSetColor.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetColor[i].resize(6);
      for (int f = 0; f < 6; f++) {
        _descriptorSetColor[i][f] = std::make_shared<DescriptorSet>(_descriptorSetLayoutColor, engineState);
      }
    }

    _updateColorDescriptor(_material);

    {
      auto shader = std::make_shared<Shader>(engineState);
      shader->add("shaders/IBL/skyboxDiffuse_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/IBL/skyboxDiffuse_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      _pipelineDiffuse = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                           _engineState->getDevice());
      _pipelineDiffuse->setDepthTest(true);
      _pipelineDiffuse->setDepthWrite(true);
      _pipelineDiffuse->createCustom(
          shader, {std::pair{std::string("color"), _descriptorSetLayoutColor}}, {}, _mesh3D->getBindingDescription(),
          _mesh3D->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}}),
          _renderPass);
    }
    {
      std::map<std::string, VkPushConstantRange> defaultPushConstants;
      defaultPushConstants["fragment"] =
          VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(FragmentPush)};

      auto shader = std::make_shared<Shader>(engineState);
      shader->add("shaders/IBL/skyboxSpecular_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/IBL/skyboxSpecular_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      _pipelineSpecular = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                            _engineState->getDevice());
      _pipelineSpecular->setDepthTest(true);
      _pipelineSpecular->setDepthWrite(true);
      _pipelineSpecular->createCustom(
          shader, {std::pair{std::string("color"), _descriptorSetLayoutColor}}, defaultPushConstants,
          _mesh3D->getBindingDescription(),
          _mesh3D->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}}),
          _renderPass);
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
  auto brdfImage = std::make_shared<Image>(
      _engineState->getSettings()->getShadowMapResolution(), 1, 1, _engineState->getSettings()->getGraphicColorFormat(),
      VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, engineState);
  brdfImage->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, commandBufferTransfer);
  auto brdfImageView = std::make_shared<ImageView>(brdfImage, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1,
                                                   VK_IMAGE_ASPECT_COLOR_BIT, engineState);
  _textureSpecularBRDF = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, VK_FILTER_LINEAR,
                                                   brdfImageView, engineState);

  _cameraSpecularBRDF = std::make_shared<CameraOrtho>();
  _cameraSpecularBRDF->setProjectionParameters({-1, 1, 1, -1}, 0, 1);
  _cameraSpecularBRDF->setViewParameters(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));

  _camera = std::make_shared<CameraPerspective>(_engineState);
  _camera->setViewParameters(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, -1.f), glm::vec3(0.f, 1.f, 0.f));
  _camera->setProjectionParameters(90.f, 0.1f, 100.f);
  _camera->setAspect(1.f);

  _frameBufferSpecular.resize(6);
  for (int i = 0; i < 6; i++) {
    _frameBufferSpecular[i].resize(_engineState->getSettings()->getSpecularMipMap());
    for (int j = 0; j < _engineState->getSettings()->getSpecularMipMap(); j++) {
      auto currentTexture = _cubemapSpecular->getTextureSeparate()[i][j];
      auto [width, height] = currentTexture->getImageView()->getImage()->getResolution();
      _frameBufferSpecular[i][j] = std::make_shared<Framebuffer>(
          std::vector{currentTexture->getImageView()}, std::tuple{width * std::pow(0.5, j), height * std::pow(0.5, j)},
          _renderPass, _engineState->getDevice());
    }
  }

  _frameBufferDiffuse.resize(6);
  for (int i = 0; i < 6; i++) {
    auto currentTexture = _cubemapDiffuse->getTextureSeparate()[i][0];
    _frameBufferDiffuse[i] = std::make_shared<Framebuffer>(std::vector{currentTexture->getImageView()},
                                                           currentTexture->getImageView()->getImage()->getResolution(),
                                                           _renderPass, _engineState->getDevice());
  }

  _frameBufferBRDF = std::make_shared<Framebuffer>(std::vector{_textureSpecularBRDF->getImageView()},
                                                   _textureSpecularBRDF->getImageView()->getImage()->getResolution(),
                                                   _renderPass, _engineState->getDevice());
}

std::shared_ptr<Cubemap> IBL::getCubemapDiffuse() { return _cubemapDiffuse; }

std::shared_ptr<Cubemap> IBL::getCubemapSpecular() { return _cubemapSpecular; }

std::shared_ptr<Texture> IBL::getTextureSpecularBRDF() { return _textureSpecularBRDF; }

void IBL::_updateColorDescriptor(std::shared_ptr<MaterialColor> material) {
  _descriptorSetColor.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    for (int f = 0; f < 6; f++) {
      _descriptorSetColor[i][f] = std::make_shared<DescriptorSet>(_descriptorSetLayoutColor, _engineState);
      std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
          {0,
           {VkDescriptorBufferInfo{.buffer = _cameraBufferCubemap[i][f]->getData(),
                                   .offset = 0,
                                   .range = _cameraBufferCubemap[i][f]->getSize()}}}};
      std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
          {1,
           {VkDescriptorImageInfo{
               .sampler = _material->getBaseColor()[0]->getSampler()->getSampler(),
               .imageView = _material->getBaseColor()[0]->getImageView()->getImageView(),
               .imageLayout = _material->getBaseColor()[0]->getImageView()->getImage()->getImageLayout()}}}};

      _descriptorSetColor[i][f]->createCustom(bufferInfoColor, textureInfoColor);
    }
  }
}

void IBL::setMaterial(std::shared_ptr<MaterialColor> material) {
  _material = material;
  _updateColorDescriptor(material);
}

void IBL::setPosition(glm::vec3 position) {
  _model = glm::translate(glm::mat4(1.f), position);
  _camera->setViewParameters(position, glm::vec3(0.f, 0.f, -1.f), glm::vec3(0.f, 1.f, 0.f));
}

void IBL::_draw(int face,
                std::shared_ptr<Camera> camera,
                std::shared_ptr<CommandBuffer> commandBuffer,
                std::shared_ptr<Pipeline> pipeline) {
  auto currentFrame = _engineState->getFrameInFlight();
  BufferMVP cameraUBO{.model = _model, .view = camera->getView(), .projection = camera->getProjection()};

  _cameraBufferCubemap[currentFrame][face]->setData(&cameraUBO);

  VkBuffer vertexBuffers[] = {_mesh3D->getVertexBuffer()->getBuffer()->getData()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), _mesh3D->getIndexBuffer()->getBuffer()->getData(), 0,
                       VK_INDEX_TYPE_UINT32);

  auto pipelineLayout = pipeline->getDescriptorSetLayout();
  auto cameraLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                   [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                     return info.first == std::string("color");
                                   });
  if (cameraLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->getPipelineLayout(), 0, 1,
                            &_descriptorSetColor[currentFrame][face]->getDescriptorSets(), 0, nullptr);
  }

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh3D->getIndexData().size()), 1, 0, 0,
                   0);
}

void IBL::drawSpecular() {
  auto logger = _engineState->getLogger();

  if (_commandBufferTransfer->getActive() == false)
    throw std::runtime_error("Command buffer isn't in record engineState");

  auto currentFrame = _engineState->getFrameInFlight();
  vkCmdBindPipeline(_commandBufferTransfer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _pipelineSpecular->getPipeline());

  /////////////////////////////////////////////////////////////////////////////////////////
  // render graphic
  /////////////////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < _engineState->getSettings()->getSpecularMipMap(); j++) {
      auto [widthFramebuffer, heightFramebuffer] = _frameBufferSpecular[i][j]->getResolution();
      VkClearValue clearColor{.color = _engineState->getSettings()->getClearColor()};
      VkRenderPassBeginInfo renderPassInfo{
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
          .renderPass = _renderPass->getRenderPass(),
          .framebuffer = _frameBufferSpecular[i][j]->getBuffer(),
          .renderArea = {.offset = {0, 0},
                         .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                    .height = static_cast<uint32_t>(heightFramebuffer)}},
          .clearValueCount = 1,
          .pClearValues = &clearColor};

      // TODO: only one depth texture?
      vkCmdBeginRenderPass(_commandBufferTransfer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

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

      auto [width, height] = _engineState->getSettings()->getSpecularIBLResolution();
      width *= std::pow(0.5, j);
      height *= std::pow(0.5, j);
      VkViewport viewport{.x = 0.f,
                          .y = 0.f,
                          .width = static_cast<float>(width),
                          .height = static_cast<float>(height),
                          .minDepth = 0.0f,
                          .maxDepth = 1.0f};
      vkCmdSetViewport(_commandBufferTransfer->getCommandBuffer(), 0, 1, &viewport);

      VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(width, height)};
      vkCmdSetScissor(_commandBufferTransfer->getCommandBuffer(), 0, 1, &scissor);

      if (_pipelineSpecular->getPushConstants().find("fragment") != _pipelineSpecular->getPushConstants().end()) {
        FragmentPush pushConstants;
        float roughness = (float)j / (float)(_engineState->getSettings()->getSpecularMipMap() - 1);
        pushConstants.roughness = roughness;
        auto info = _pipelineSpecular->getPushConstants()["fragment"];
        vkCmdPushConstants(_commandBufferTransfer->getCommandBuffer(), _pipelineSpecular->getPipelineLayout(),
                           info.stageFlags, info.offset, info.size, &pushConstants);
      }

      _draw(i, _camera, _commandBufferTransfer, _pipelineSpecular);

      logger->end(_commandBufferTransfer);

      vkCmdEndRenderPass(_commandBufferTransfer->getCommandBuffer());
    }
  }
}

void IBL::drawDiffuse() {
  auto logger = _engineState->getLogger();

  if (_commandBufferTransfer->getActive() == false)
    throw std::runtime_error("Command buffer isn't in record engineState");
  // render cubemap to diffuse
  auto currentFrame = _engineState->getFrameInFlight();
  vkCmdBindPipeline(_commandBufferTransfer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _pipelineDiffuse->getPipeline());
  /////////////////////////////////////////////////////////////////////////////////////////
  // render graphic
  /////////////////////////////////////////////////////////////////////////////////////////
  for (int i = 0; i < 6; i++) {
    auto [widthFramebuffer, heightFramebuffer] = _frameBufferDiffuse[i]->getResolution();
    VkClearValue clearColor{.color = _engineState->getSettings()->getClearColor()};
    VkRenderPassBeginInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                         .renderPass = _renderPass->getRenderPass(),
                                         .framebuffer = _frameBufferDiffuse[i]->getBuffer(),
                                         .renderArea = {.offset = {0, 0},
                                                        .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                                                   .height = static_cast<uint32_t>(heightFramebuffer)}},
                                         .clearValueCount = 1,
                                         .pClearValues = &clearColor};

    vkCmdBeginRenderPass(_commandBufferTransfer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

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

    vkCmdEndRenderPass(_commandBufferTransfer->getCommandBuffer());
  }
}

void IBL::drawSpecularBRDF() {
  auto logger = _engineState->getLogger();

  if (_commandBufferTransfer->getActive() == false)
    throw std::runtime_error("Command buffer isn't in record engineState");

  auto resolution = _textureSpecularBRDF->getImageView()->getImage()->getResolution();
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

  auto [widthFramebuffer, heightFramebuffer] = _frameBufferBRDF->getResolution();
  VkClearValue clearColor{.color = _engineState->getSettings()->getClearColor()};
  VkRenderPassBeginInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                       .renderPass = _renderPass->getRenderPass(),
                                       .framebuffer = _frameBufferBRDF->getBuffer(),
                                       .renderArea = {.offset = {0, 0},
                                                      .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                                                 .height = static_cast<uint32_t>(heightFramebuffer)}},
                                       .clearValueCount = 1,
                                       .pClearValues = &clearColor};

  vkCmdBeginRenderPass(_commandBufferTransfer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  logger->begin("Render specular brdf", _commandBufferTransfer);
  auto model = glm::scale(_model, glm::vec3(2.f, 2.f, 1.f));
  BufferMVP cameraMVP{.model = model,
                      .view = _cameraSpecularBRDF->getView(),
                      .projection = _cameraSpecularBRDF->getProjection()};
  _cameraBuffer[currentFrame]->setData(&cameraMVP);

  VkBuffer vertexBuffers[] = {_mesh2D->getVertexBuffer()->getBuffer()->getData()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(_commandBufferTransfer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(_commandBufferTransfer->getCommandBuffer(), _mesh2D->getIndexBuffer()->getBuffer()->getData(), 0,
                       VK_INDEX_TYPE_UINT32);

  auto pipelineLayout = _pipelineSpecularBRDF->getDescriptorSetLayout();
  vkCmdBindPipeline(_commandBufferTransfer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _pipelineSpecularBRDF->getPipeline());

  // camera
  auto cameraLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                   [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                     return info.first == std::string("brdf");
                                   });
  if (cameraLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(_commandBufferTransfer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            _pipelineSpecularBRDF->getPipelineLayout(), 0, 1,
                            &_descriptorSetBRDF[currentFrame]->getDescriptorSets(), 0, nullptr);
  }

  vkCmdDrawIndexed(_commandBufferTransfer->getCommandBuffer(), static_cast<uint32_t>(_mesh2D->getIndexData().size()), 1,
                   0, 0, 0);
  logger->end(_commandBufferTransfer);

  vkCmdEndRenderPass(_commandBufferTransfer->getCommandBuffer());
}