#include "Primitive/Skybox.h"

Skybox::Skybox(std::shared_ptr<CommandBuffer> commandBufferTransfer,
               std::shared_ptr<GameState> gameState,
               std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  _gameState = gameState;
  _mesh = std::make_shared<MeshStatic3D>(engineState);
  std::vector<Vertex3D> vertices{
      {.pos = glm::vec3(-0.5, -0.5, 0.5)},   // 0
      {.pos = glm::vec3(0.5, -0.5, 0.5)},    // 1
      {.pos = glm::vec3(-0.5, 0.5, 0.5)},    // 2
      {.pos = glm::vec3(0.5, 0.5, 0.5)},     // 3
      {.pos = glm::vec3(-0.5, -0.5, -0.5)},  // 4
      {.pos = glm::vec3(0.5, -0.5, -0.5)},   // 5
      {.pos = glm::vec3(-0.5, 0.5, -0.5)},   // 6
      {.pos = glm::vec3(0.5, 0.5, -0.5)}     // 7
  };
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
  _mesh->setVertices(vertices, commandBufferTransfer);
  _mesh->setIndexes(indices, commandBufferTransfer);
  _defaultMaterialColor = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, commandBufferTransfer, engineState);
  _defaultMaterialColor->setBaseColor({gameState->getResourceManager()->getCubemapOne()->getTexture()});
  _material = _defaultMaterialColor;
  _changedMaterial.resize(engineState->getSettings()->getMaxFramesInFlight());

  _uniformBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _uniformBuffer[i] = std::make_shared<Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, engineState);

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

    _descriptorSet.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
      _descriptorSet[i] = std::make_shared<DescriptorSet>(_descriptorSetLayout, engineState);

    setMaterial(_defaultMaterialColor);

    // initialize Color
    {
      auto shader = std::make_shared<Shader>(engineState);
      shader->add("shaders/skybox/skybox_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/skybox/skybox_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

      _pipeline = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(), _engineState->getDevice());
      _pipeline->setDepthTest(true);
      // we force skybox to have the biggest possible depth = 1 so we need to draw skybox if it's depth <= 1
      _pipeline->setDepthCompateOp(VK_COMPARE_OP_LESS_OR_EQUAL);
      _pipeline->createCustom(
          shader, {std::pair{std::string("color"), _descriptorSetLayout}}, {}, _mesh->getBindingDescription(),
          _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}}),
          RenderPassScenario::GRAPHIC);
    }
  }
}

void Skybox::_updateColorDescriptor() {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialColor>(_material);
  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
      {0,
       {{.buffer = _uniformBuffer[currentFrame]->getData(),
         .offset = 0,
         .range = _uniformBuffer[currentFrame]->getSize()}}}};
  std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
      {1,
       {{.sampler = material->getBaseColor()[0]->getSampler()->getSampler(),
         .imageView = material->getBaseColor()[0]->getImageView()->getImageView(),
         .imageLayout = material->getBaseColor()[0]->getImageView()->getImage()->getImageLayout()}}}};
  _descriptorSet[currentFrame]->createCustom(bufferInfoColor, textureInfoColor);
}

void Skybox::setMaterial(std::shared_ptr<MaterialColor> material) {
  int frameInFlight = _engineState->getFrameInFlight();
  if (_material) _material->unregisterUpdate(_descriptorSet[frameInFlight]);
  material->registerUpdate(_descriptorSet[frameInFlight],
                           {._frameInFlight = frameInFlight, ._materialInfo = {{MaterialTexture::COLOR, 1}}});
  _materialType = MaterialType::COLOR;
  _material = material;
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void Skybox::setModel(glm::mat4 model) { _model = model; }

std::shared_ptr<MeshStatic3D> Skybox::getMesh() { return _mesh; }

void Skybox::draw(std::shared_ptr<CommandBuffer> commandBuffer) {
  auto currentFrame = _engineState->getFrameInFlight();
  if (_changedMaterial[currentFrame]) {
    switch (_materialType) {
      case MaterialType::COLOR:
        _updateColorDescriptor();
        break;
    }
    _changedMaterial[currentFrame] = false;
  }

  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->getPipeline());
  VkViewport viewport{.x = 0.0f,
                      .y = static_cast<float>(std::get<1>(_engineState->getSettings()->getResolution())),
                      .width = static_cast<float>(std::get<0>(_engineState->getSettings()->getResolution())),
                      .height = static_cast<float>(-std::get<1>(_engineState->getSettings()->getResolution())),
                      .minDepth = 0.f,
                      .maxDepth = 1.f};
  vkCmdSetViewport(commandBuffer->getCommandBuffer(), 0, 1, &viewport);

  VkRect2D scissor{.offset = {0, 0},
                   .extent = VkExtent2D(std::get<0>(_engineState->getSettings()->getResolution()),
                                        std::get<1>(_engineState->getSettings()->getResolution()))};
  vkCmdSetScissor(commandBuffer->getCommandBuffer(), 0, 1, &scissor);

  BufferMVP cameraUBO{.model = _model,
                      .view = glm::mat4(glm::mat3(_gameState->getCameraManager()->getCurrentCamera()->getView())),
                      .projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection()};
  _uniformBuffer[currentFrame]->setData(&cameraUBO);

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getData()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), _mesh->getIndexBuffer()->getBuffer()->getData(), 0,
                       VK_INDEX_TYPE_UINT32);

  auto pipelineLayout = _pipeline->getDescriptorSetLayout();
  auto cameraLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                   [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                     return info.first == std::string("color");
                                   });
  if (cameraLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            _pipeline->getPipelineLayout(), 0, 1, &_descriptorSet[currentFrame]->getDescriptorSets(), 0,
                            nullptr);
  }

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getIndexData().size()), 1, 0, 0, 0);
}