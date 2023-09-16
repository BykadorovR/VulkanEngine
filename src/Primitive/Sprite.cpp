#include "Sprite.h"

Sprite::Sprite(std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>> descriptorSetLayout,
               std::shared_ptr<CommandBuffer> commandBufferTransfer,
               std::shared_ptr<State> state) {
  _state = state;

  _vertexBuffer = std::make_shared<VertexBuffer<Vertex2D>>(_vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                           commandBufferTransfer, _state->getDevice());
  _indexBuffer = std::make_shared<VertexBuffer<uint32_t>>(_indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                                          commandBufferTransfer, _state->getDevice());
  // initialize camera UBO and descriptor sets for shadow
  for (int i = 0; i < _state->getSettings()->getMaxDirectionalLights(); i++) {
    _cameraUBODepth.push_back({std::make_shared<UniformBuffer>(_state->getSettings()->getMaxFramesInFlight(),
                                                               sizeof(BufferMVP), _state->getDevice())});
  }

  for (int i = 0; i < _state->getSettings()->getMaxPointLights(); i++) {
    std::vector<std::shared_ptr<UniformBuffer>> facesBuffer(6);
    for (int j = 0; j < 6; j++) {
      facesBuffer[j] = std::make_shared<UniformBuffer>(_state->getSettings()->getMaxFramesInFlight(), sizeof(BufferMVP),
                                                       _state->getDevice());
    }
    _cameraUBODepth.push_back(facesBuffer);
  }

  {
    auto cameraLayout = std::find_if(descriptorSetLayout.begin(), descriptorSetLayout.end(),
                                     [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                       return info.first == std::string("camera");
                                     });

    for (int i = 0; i < _state->getSettings()->getMaxDirectionalLights(); i++) {
      auto cameraSet = std::make_shared<DescriptorSet>(_state->getSettings()->getMaxFramesInFlight(),
                                                       (*cameraLayout).second, _state->getDescriptorPool(),
                                                       _state->getDevice());
      cameraSet->createUniformBuffer(_cameraUBODepth[i][0]);

      _descriptorSetCameraDepth.push_back({cameraSet});
    }

    for (int i = 0; i < _state->getSettings()->getMaxPointLights(); i++) {
      std::vector<std::shared_ptr<DescriptorSet>> facesSet(6);
      for (int j = 0; j < 6; j++) {
        facesSet[j] = std::make_shared<DescriptorSet>(_state->getSettings()->getMaxFramesInFlight(),
                                                      (*cameraLayout).second, _state->getDescriptorPool(),
                                                      _state->getDevice());
        facesSet[j]->createUniformBuffer(_cameraUBODepth[i + _state->getSettings()->getMaxDirectionalLights()][j]);
      }
      _descriptorSetCameraDepth.push_back(facesSet);
    }
  }

  // initialize camera UBO and descriptor sets for draw pass
  _cameraUBOFull = std::make_shared<UniformBuffer>(_state->getSettings()->getMaxFramesInFlight(), sizeof(BufferMVP),
                                                   _state->getDevice());
  {
    auto cameraLayout = std::find_if(descriptorSetLayout.begin(), descriptorSetLayout.end(),
                                     [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                       return info.first == std::string("camera");
                                     });
    auto cameraSet = std::make_shared<DescriptorSet>(_state->getSettings()->getMaxFramesInFlight(),
                                                     (*cameraLayout).second, _state->getDescriptorPool(),
                                                     _state->getDevice());
    cameraSet->createUniformBuffer(_cameraUBOFull);
    _descriptorSetCameraFull = cameraSet;
  }
}

void Sprite::setVertexNormal(glm::vec3 normal) {
  for (auto& vertex : _vertices) {
    vertex.normal = normal;
  }
}

void Sprite::setMaterial(std::shared_ptr<MaterialSpritePhong> material) { _material = material; }

void Sprite::enableDepth(bool enable) { _enableDepth = enable; }

bool Sprite::isDepthEnabled() { return _enableDepth; }

void Sprite::enableShadow(bool enable) { _enableShadow = enable; }

void Sprite::enableLighting(bool enable) { _enableLighting = enable; }

void Sprite::setModel(glm::mat4 model) { _model = model; }

void Sprite::setCamera(std::shared_ptr<Camera> camera) { _camera = camera; }

void Sprite::draw(int currentFrame, std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<Pipeline> pipeline) {
  // update material
  _material->draw(currentFrame, _vertexBuffer);

  if (pipeline->getPushConstants().find("fragment") != pipeline->getPushConstants().end()) {
    LightPush pushConstants;
    pushConstants.enableShadow = _enableShadow;
    pushConstants.enableLighting = _enableLighting;
    pushConstants.cameraPosition = _camera->getEye();

    vkCmdPushConstants(commandBuffer->getCommandBuffer()[currentFrame], pipeline->getPipelineLayout(),
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(LightPush), &pushConstants);
  }

  BufferMVP cameraMVP{};
  cameraMVP.model = _model;
  cameraMVP.view = _camera->getView();
  cameraMVP.projection = _camera->getProjection();

  void* data;
  vkMapMemory(_state->getDevice()->getLogicalDevice(), _cameraUBOFull->getBuffer()[currentFrame]->getMemory(), 0,
              sizeof(cameraMVP), 0, &data);
  memcpy(data, &cameraMVP, sizeof(cameraMVP));
  vkUnmapMemory(_state->getDevice()->getLogicalDevice(), _cameraUBOFull->getBuffer()[currentFrame]->getMemory());

  VkBuffer vertexBuffers[] = {_vertexBuffer->getBuffer()->getData()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer()[currentFrame], 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer()[currentFrame], _indexBuffer->getBuffer()->getData(), 0,
                       VK_INDEX_TYPE_UINT32);

  auto pipelineLayout = pipeline->getDescriptorSetLayout();
  auto cameraLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                   [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                     return info.first == std::string("camera");
                                   });
  if (cameraLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->getPipelineLayout(), 0, 1,
                            &_descriptorSetCameraFull->getDescriptorSets()[currentFrame], 0, nullptr);
  }

  auto textureLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                    [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                      return info.first == std::string("texture");
                                    });
  if (textureLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->getPipelineLayout(), 1, 1,
                            &_material->getDescriptorSetTextures()->getDescriptorSets()[currentFrame], 0, nullptr);
  }

  auto phongCoefficientsLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                              [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                                return info.first == std::string("phongCoefficients");
                                              });
  if (phongCoefficientsLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->getPipelineLayout(), 5, 1,
                            &_material->getDescriptorSetCoefficients()->getDescriptorSets()[currentFrame], 0, nullptr);
  }

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer()[currentFrame], static_cast<uint32_t>(_indices.size()), 1, 0, 0, 0);
}

void Sprite::drawShadow(int currentFrame,
                        std::shared_ptr<CommandBuffer> commandBuffer,
                        std::shared_ptr<Pipeline> pipeline,
                        int lightIndex,
                        glm::mat4 view,
                        glm::mat4 projection,
                        int face) {
  BufferMVP cameraMVP{};
  cameraMVP.model = _model;
  cameraMVP.view = view;
  cameraMVP.projection = projection;

  void* data;
  vkMapMemory(_state->getDevice()->getLogicalDevice(),
              _cameraUBODepth[lightIndex][face]->getBuffer()[currentFrame]->getMemory(), 0, sizeof(cameraMVP), 0,
              &data);
  memcpy(data, &cameraMVP, sizeof(cameraMVP));
  vkUnmapMemory(_state->getDevice()->getLogicalDevice(),
                _cameraUBODepth[lightIndex][face]->getBuffer()[currentFrame]->getMemory());

  VkBuffer vertexBuffers[] = {_vertexBuffer->getBuffer()->getData()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer()[currentFrame], 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer()[currentFrame], _indexBuffer->getBuffer()->getData(), 0,
                       VK_INDEX_TYPE_UINT32);

  auto pipelineLayout = pipeline->getDescriptorSetLayout();
  auto cameraLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                   [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                     return info.first == std::string("camera");
                                   });
  if (cameraLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(
        commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(),
        0, 1, &_descriptorSetCameraDepth[lightIndex][face]->getDescriptorSets()[currentFrame], 0, nullptr);
  }

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer()[currentFrame], static_cast<uint32_t>(_indices.size()), 1, 0, 0, 0);
}