#include <cstring>
#include <chrono>
#include <optional>
#include "Primitive/Skybox.h"

import Shader;

Skybox::Skybox(std::shared_ptr<GameState> gameState, std::shared_ptr<EngineState> engineState) {
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
  _mesh->setVertices(vertices);
  _mesh->setIndexes(indices);
  _defaultMaterialColor = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, engineState);
  _defaultMaterialColor->setBaseColor({gameState->getResourceManager()->getCubemapOne()->getTexture()});
  _material = _defaultMaterialColor;
  _changedMaterial.resize(engineState->getSettings()->getMaxFramesInFlight());

  _uniformBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _uniformBuffer[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *engineState->getMemoryAllocator());

  // setup color
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/skybox/skybox_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/skybox/skybox_fragment.spv"));
    _descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    _descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));

    _descriptorSet.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
      _descriptorSet[i] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout).get()},
          *engineState->getDescriptorPool(), *engineState->getDevice(), 0);

    setMaterial(_defaultMaterialColor);

    // initialize Color
    {
      const auto settings = _engineState->getSettings();
      RenderGraph::PipelineGraphic pipelineGraphic;
      pipelineGraphic.setColorAttachments({settings->getGraphicColorFormat()});
      pipelineGraphic.setDepthAttachment(settings->getDepthFormat());
      pipelineGraphic.setDepthTest(true);
      pipelineGraphic.setDepthWrite(false);
      pipelineGraphic.setDepthCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);
      std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_descriptorSetLayout.get()};
      const auto vertexBinding = _mesh->getBindingDescription();
      const auto vertexAttributes = _mesh->getAttributeDescriptions(
          {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}});
      const VkPipelineVertexInputStateCreateInfo vertexInput{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
          .vertexBindingDescriptionCount = 1,
          .pVertexBindingDescriptions = &vertexBinding,
          .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
          .pVertexAttributeDescriptions = vertexAttributes.data(),
      };
      _pipeline = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipeline->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts, {}, vertexInput);
    }
  }
}

void Skybox::_updateColorDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialColor>(_material);
  material->unregisterUpdate(_descriptorSet[currentFrame]);
  _descriptorSet[currentFrame] = std::make_shared<RenderGraph::DescriptorSet>(
      std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout).get()}, *_engineState->getDescriptorPool(),
      *_engineState->getDevice(), 0);
  _descriptorSet[currentFrame]->add({_uniformBuffer[currentFrame].get()});
  _descriptorSet[currentFrame]->add({material->getBaseColor()[0].get()});
  _descriptorSet[currentFrame]->initialize(commandBuffer);
  material->registerUpdate(_descriptorSet[currentFrame],
                           {.frameInFlight = currentFrame, .materialInfo = {{MaterialTexture::COLOR, 1}}});
}

void Skybox::setMaterial(std::shared_ptr<MaterialColor> material) {
  _materialType = MaterialType::COLOR;
  if (_material) {
    for (const auto& descriptor : _descriptorSet) _material->unregisterUpdate(descriptor);
  }
  _material = material;
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void Skybox::setModel(glm::mat4 model) { _model = model; }

std::shared_ptr<MeshStatic3D> Skybox::getMesh() { return _mesh; }

void Skybox::draw(const RenderGraph::CommandBuffer* commandBuffer) {
  const int currentFrame = _engineState->getFrameInFlight();
  if (_changedMaterial[currentFrame]) {
    switch (_materialType) {
      case MaterialType::COLOR:
        _updateColorDescriptor(*commandBuffer);
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
  std::memcpy(static_cast<std::byte*>(_uniformBuffer[currentFrame]->getAllocationInfo().pMappedData), &cameraUBO,
              static_cast<std::size_t>(_uniformBuffer[currentFrame]->getSize()));

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), _mesh->getIndexBuffer()->getBuffer()->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  _descriptorSet[currentFrame]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->getPipelineLayout(), *commandBuffer);

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getIndexData().size()), 1, 0, 0, 0);
}
