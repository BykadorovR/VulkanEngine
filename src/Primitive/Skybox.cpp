#include "Skybox.h"

Skybox::Skybox(std::shared_ptr<CommandBuffer> commandBufferTransfer,
               std::shared_ptr<ResourceManager> resourceManager,
               std::shared_ptr<State> state) {
  _state = state;
  _mesh = std::make_shared<Mesh3D>(state);
  std::vector<Vertex3D> vertices(8);
  vertices[0].pos = glm::vec3(-0.5, -0.5, 0.5);   // 0
  vertices[1].pos = glm::vec3(0.5, -0.5, 0.5);    // 1
  vertices[2].pos = glm::vec3(-0.5, 0.5, 0.5);    // 2
  vertices[3].pos = glm::vec3(0.5, 0.5, 0.5);     // 3
  vertices[4].pos = glm::vec3(-0.5, -0.5, -0.5);  // 4
  vertices[5].pos = glm::vec3(0.5, -0.5, -0.5);   // 5
  vertices[6].pos = glm::vec3(-0.5, 0.5, -0.5);   // 6
  vertices[7].pos = glm::vec3(0.5, 0.5, -0.5);    // 7

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
  _defaultMaterialColor = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, commandBufferTransfer, state);
  _defaultMaterialColor->setBaseColor({resourceManager->getTextureOne()});
  _material = _defaultMaterialColor;

  _uniformBuffer = std::make_shared<UniformBuffer>(_state->getSettings()->getMaxFramesInFlight(), sizeof(BufferMVP),
                                                   state);
  auto setLayout = std::make_shared<DescriptorSetLayout>(state->getDevice());
  setLayout->createUniformBuffer();
  _descriptorSetCamera = std::make_shared<DescriptorSet>(state->getSettings()->getMaxFramesInFlight(), setLayout,
                                                         state->getDescriptorPool(), state->getDevice());
  _descriptorSetCamera->createUniformBuffer(_uniformBuffer);

  auto shader = std::make_shared<Shader>(state->getDevice());
  shader->add("shaders/skybox_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
  shader->add("shaders/skybox_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
  _pipeline = std::make_shared<Pipeline>(_state->getSettings(), _state->getDevice());
  _pipeline->createSkybox(
      std::vector{_state->getSettings()->getGraphicColorFormat(), _state->getSettings()->getGraphicColorFormat()},
      VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
      {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
       shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
      {std::pair{std::string("camera"), setLayout},
       std::pair{std::string("texture"), _material->getDescriptorSetLayoutTextures()}},
      {}, _mesh->getBindingDescription(), _mesh->getAttributeDescriptions());
}

void Skybox::setMaterial(std::shared_ptr<MaterialColor> material) {
  _material = material;
  _materialType = MaterialType::COLOR;
}

void Skybox::setModel(glm::mat4 model) { _model = model; }

std::shared_ptr<Mesh3D> Skybox::getMesh() { return _mesh; }

void Skybox::draw(std::tuple<int, int> resolution,
                  std::shared_ptr<Camera> camera,
                  std::shared_ptr<CommandBuffer> commandBuffer) {
  auto currentFrame = _state->getFrameInFlight();
  vkCmdBindPipeline(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _pipeline->getPipeline());
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = std::get<1>(_state->getSettings()->getResolution());
  viewport.width = std::get<0>(_state->getSettings()->getResolution());
  viewport.height = -std::get<1>(_state->getSettings()->getResolution());
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer->getCommandBuffer()[currentFrame], 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = VkExtent2D(std::get<0>(_state->getSettings()->getResolution()),
                              std::get<1>(_state->getSettings()->getResolution()));
  vkCmdSetScissor(commandBuffer->getCommandBuffer()[currentFrame], 0, 1, &scissor);

  BufferMVP cameraUBO{};
  cameraUBO.model = _model;
  cameraUBO.view = glm::mat4(glm::mat3(camera->getView()));
  cameraUBO.projection = camera->getProjection();

  void* data;
  vkMapMemory(_state->getDevice()->getLogicalDevice(), _uniformBuffer->getBuffer()[currentFrame]->getMemory(), 0,
              sizeof(cameraUBO), 0, &data);
  memcpy(data, &cameraUBO, sizeof(cameraUBO));
  vkUnmapMemory(_state->getDevice()->getLogicalDevice(), _uniformBuffer->getBuffer()[currentFrame]->getMemory());

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getData()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer()[currentFrame], 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer()[currentFrame], _mesh->getIndexBuffer()->getBuffer()->getData(),
                       0, VK_INDEX_TYPE_UINT32);

  auto pipelineLayout = _pipeline->getDescriptorSetLayout();
  auto cameraLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                   [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                     return info.first == std::string("camera");
                                   });
  if (cameraLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            _pipeline->getPipelineLayout(), 0, 1,
                            &_descriptorSetCamera->getDescriptorSets()[currentFrame], 0, nullptr);
  }

  auto textureLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                    [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                      return info.first == std::string("texture");
                                    });
  if (textureLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            _pipeline->getPipelineLayout(), 1, 1,
                            &_material->getDescriptorSetTextures(currentFrame)->getDescriptorSets()[currentFrame], 0,
                            nullptr);
  }

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer()[currentFrame], static_cast<uint32_t>(_mesh->getIndexData().size()),
                   1, 0, 0, 0);
}