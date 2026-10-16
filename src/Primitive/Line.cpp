#include <cstring>
#include "Primitive/Line.h"

import Shader;

Line::Line(const RenderGraph::CommandBuffer* commandBufferTransfer,
           std::shared_ptr<GameState> gameState,
           std::shared_ptr<EngineState> engineState) {
  setName("Line");
  _engineState = engineState;
  _gameState = gameState;
  _mesh = std::make_shared<MeshDynamic3D>(engineState);
  _mesh->setIndexes({0, 1});
  _mesh->setVertices({Vertex3D{}, Vertex3D{}});

  auto shader = std::make_shared<RenderGraph::Shader>(*engineState->getDevice());
  shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                            "shaders/line/line_vertex.spv"));
  shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                            "shaders/line/line_fragment.spv"));
  _descriptorSetLayoutCamera = std::make_shared<RenderGraph::DescriptorSetLayout>(*engineState->getDevice());
  _descriptorSetLayoutCamera->createCustom(shader->getDescriptorSetLayoutBindings().at(0));
  _descriptorSetCamera.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _descriptorSetCamera[i] = std::make_shared<RenderGraph::DescriptorSet>(
        std::vector<RenderGraph::DescriptorSetLayout*>{_descriptorSetLayoutCamera.get()},
        *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
  _cameraBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _cameraBuffer[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *engineState->getMemoryAllocator());

    _descriptorSetCamera[i]->add({_cameraBuffer[i].get()});
    _descriptorSetCamera[i]->initialize(*commandBufferTransfer);
  }

  const auto settings = _engineState->getSettings();
  RenderGraph::PipelineGraphic pipelineGraphic;
  pipelineGraphic.setColorAttachments({settings->getGraphicColorFormat()});
  pipelineGraphic.setDepthAttachment(settings->getDepthFormat());
  pipelineGraphic.setDepthTest(true);
  pipelineGraphic.setDepthWrite(true);
  pipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
  std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_descriptorSetLayoutCamera.get()};
  const auto vertexBinding = _mesh->getBindingDescription();
  const auto vertexAttributes = _mesh->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
       {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)}});
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

std::shared_ptr<MeshDynamic3D> Line::getMesh() { return _mesh; }

void Line::setModel(glm::mat4 model) { _model = model; }

void Line::draw(const RenderGraph::CommandBuffer* commandBuffer) {
  const int currentFrame = _engineState->getFrameInFlight();
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

  std::memcpy(static_cast<std::byte*>(_cameraBuffer[currentFrame]->getAllocationInfo().pMappedData), &cameraUBO,
              static_cast<std::size_t>(_cameraBuffer[currentFrame]->getSize()));

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), _mesh->getIndexBuffer()->getBuffer()->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  _descriptorSetCamera[currentFrame]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->getPipelineLayout(),
                                           *commandBuffer);

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getIndexData().size()), 1, 0, 0, 0);
}
