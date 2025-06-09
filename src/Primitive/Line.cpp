#include "Primitive/Line.h"

Line::Line(std::shared_ptr<CommandBuffer> commandBufferTransfer,
           std::shared_ptr<GameState> gameState,
           std::shared_ptr<EngineState> engineState) {
  setName("Line");
  _engineState = engineState;
  _gameState = gameState;
  _mesh = std::make_shared<MeshDynamic3D>(engineState);
  _mesh->setIndexes({0, 1});
  _mesh->setVertices({Vertex3D{}, Vertex3D{}});

  auto cameraLayout = std::make_shared<DescriptorSetLayout>(engineState->getDevice());
  VkDescriptorSetLayoutBinding layoutBinding = {.binding = 0,
                                                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                .descriptorCount = 1,
                                                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                                .pImmutableSamplers = nullptr};
  cameraLayout->createCustom({layoutBinding});
  _descriptorSetCamera.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _descriptorSetCamera[i] = std::make_shared<DescriptorSet>(cameraLayout, engineState);
  _cameraBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _cameraBuffer[i] = std::make_shared<Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, engineState);

    std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfo = {
        {0,
         {VkDescriptorBufferInfo{.buffer = _cameraBuffer[i]->getData(),
                                 .offset = 0,
                                 .range = _cameraBuffer[i]->getSize()}}}};
    _descriptorSetCamera[i]->createCustom(bufferInfo, {});
  }

  auto shader = std::make_shared<Shader>(engineState);
  shader->add("shaders/line/line_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
  shader->add("shaders/line/line_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
  _pipeline = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(), _engineState->getDevice());
  _pipeline->setDepthTest(true);
  _pipeline->setDepthWrite(true);
  _pipeline->setTopology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
  _pipeline->createCustom(
      shader, {std::pair{std::string("camera"), cameraLayout}}, {}, _mesh->getBindingDescription(),
      _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                               {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)}}),
      RenderPassScenario::GRAPHIC);
}

std::shared_ptr<MeshDynamic3D> Line::getMesh() { return _mesh; }

void Line::setModel(glm::mat4 model) { _model = model; }

void Line::draw(std::shared_ptr<CommandBuffer> commandBuffer) {
  auto currentFrame = _engineState->getFrameInFlight();
  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->getPipeline());
  auto resolution = _engineState->getSettings()->getResolution();
  VkViewport viewport{.x = 0.0f,
                      .y = static_cast<float>(std::get<1>(resolution)),
                      .width = static_cast<float>(std::get<0>(resolution)),
                      .height = static_cast<float>(-std::get<1>(resolution)),
                      .minDepth = 0.0f,
                      .maxDepth = 1.0f};
  vkCmdSetViewport(commandBuffer->getCommandBuffer(), 0, 1, &viewport);

  VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(std::get<0>(resolution), std::get<1>(resolution))};
  vkCmdSetScissor(commandBuffer->getCommandBuffer(), 0, 1, &scissor);

  BufferMVP cameraUBO{.model = _model,
                      .view = _gameState->getCameraManager()->getCurrentCamera()->getView(),
                      .projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection()};

  _cameraBuffer[currentFrame]->setData(&cameraUBO);

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getData()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), _mesh->getIndexBuffer()->getBuffer()->getData(), 0,
                       VK_INDEX_TYPE_UINT32);

  auto pipelineLayout = _pipeline->getDescriptorSetLayout();
  auto cameraLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                   [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                     return info.first == std::string("camera");
                                   });
  if (cameraLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            _pipeline->getPipelineLayout(), 0, 1,
                            &_descriptorSetCamera[currentFrame]->getDescriptorSets(), 0, nullptr);
  }

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getIndexData().size()), 1, 0, 0, 0);
}