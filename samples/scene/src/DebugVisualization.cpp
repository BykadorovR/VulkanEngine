#include "DebugVisualization.h"
#include "Descriptor.h"
#include <format>

DebugVisualization::DebugVisualization(std::shared_ptr<Camera> camera,
                                       std::shared_ptr<GUI> gui,
                                       std::shared_ptr<CommandBuffer> commandBufferTransfer,
                                       std::shared_ptr<ResourceManager> resourceManager,
                                       std::shared_ptr<State> state) {
  _camera = camera;
  _resourceManager = resourceManager;
  _gui = gui;
  _state = state;
  _commandBufferTransfer = commandBufferTransfer;
  _meshSprite = std::make_shared<Mesh2D>(state);
  // Vulkan image origin (0,0) is left-top corner
  _meshSprite->setVertices(
      {Vertex2D{{0.5f, 0.5f, 0.f}, {0.f, 0.f, -1.f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.f, 0.f}},
       Vertex2D{{0.5f, -0.5f, 0.f}, {0.f, 0.f, -1.f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.f, 0.f}},
       Vertex2D{{-0.5f, -0.5f, 0.f}, {0.f, 0.f, -1.f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.f, 0.f}},
       Vertex2D{{-0.5f, 0.5f, 0.f}, {0.f, 0.f, -1.f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.f, 0.f}}},
      commandBufferTransfer);
  _meshSprite->setIndexes({0, 1, 3, 1, 2, 3}, commandBufferTransfer);

  auto shader = std::make_shared<Shader>(state->getDevice());
  shader->add("shaders/quad2D_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
  shader->add("shaders/quad2D_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
  _uniformBuffer = std::make_shared<UniformBuffer>(state->getSettings()->getMaxFramesInFlight(), sizeof(glm::mat4),
                                                   state);

  auto cameraLayout = std::make_shared<DescriptorSetLayout>(state->getDevice());
  cameraLayout->createUniformBuffer();

  _cameraSet = std::make_shared<DescriptorSet>(state->getSettings()->getMaxFramesInFlight(), cameraLayout,
                                               state->getDescriptorPool(), state->getDevice());
  _cameraSet->createUniformBuffer(_uniformBuffer);

  _textureSetLayout = std::make_shared<DescriptorSetLayout>(state->getDevice());
  _textureSetLayout->createTexture();

  _pipeline = std::make_shared<Pipeline>(state->getSettings(), state->getDevice());
  _pipeline->createHUD(
      _state->getSettings()->getSwapchainColorFormat(),
      {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
       shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
      {{"camera", cameraLayout}, {"texture", _textureSetLayout}},
      std::map<std::string, VkPushConstantRange>{{std::string("fragment"), DepthPush::getPushConstant()}},
      _meshSprite->getBindingDescription(), _meshSprite->getAttributeDescriptions());

  for (auto elem : _state->getSettings()->getAttenuations()) {
    _attenuationKeys.push_back(std::to_string(std::get<0>(elem)));
  }

  _lineFrustum.resize(12);
  for (int i = 0; i < _lineFrustum.size(); i++) {
    auto line = std::make_shared<Line>(3, std::vector{_state->getSettings()->getSwapchainColorFormat()},
                                       commandBufferTransfer, state);
    auto mesh = line->getMesh();
    mesh->setColor({glm::vec3(1.f, 0.f, 0.f)}, commandBufferTransfer);
    _lineFrustum[i] = line;
  }

  _near = _camera->getNear();
  _far = _camera->getFar();
  auto [r, g, b, a] = _state->getSettings()->getClearColor().float32;
  _R = r;
  _G = g;
  _B = b;

  _boxModel = resourceManager->loadModel("assets/box/Box.gltf");
}

void DebugVisualization::setLights(std::shared_ptr<LightManager> lightManager) {
  _lightManager = lightManager;
  _farPlaneCW = std::make_shared<Sprite>(std::vector{_state->getSettings()->getSwapchainColorFormat()}, lightManager,
                                         _commandBufferTransfer, _resourceManager, _state);
  _farPlaneCW->enableLighting(false);
  _farPlaneCW->enableShadow(false);
  _farPlaneCW->enableDepth(false);
  _farPlaneCCW = std::make_shared<Sprite>(std::vector{_state->getSettings()->getSwapchainColorFormat()}, lightManager,
                                          _commandBufferTransfer, _resourceManager, _state);
  _farPlaneCCW->enableLighting(false);
  _farPlaneCCW->enableShadow(false);
  _farPlaneCCW->enableDepth(false);

  for (auto light : lightManager->getPointLights()) {
    auto model = std::make_shared<Model3D>(std::vector{_state->getSettings()->getSwapchainColorFormat()},
                                           _boxModel->getNodes(), _boxModel->getMeshes(), lightManager,
                                           _commandBufferTransfer, _resourceManager, _state);
    model->enableDepth(false);
    model->enableShadow(false);
    model->enableLighting(false);
    _modelManager.push_back(model);
    _pointLightModels.push_back(model);

    auto sphereMesh = std::make_shared<MeshSphere>(_commandBufferTransfer, _state);
    auto sphere = std::make_shared<Shape3D>(
        ShapeType::SPHERE, std::vector{_state->getSettings()->getSwapchainColorFormat()}, VK_CULL_MODE_NONE,
        lightManager, _commandBufferTransfer, _resourceManager, _state);
    _spheres.push_back(sphere);
  }

  for (auto light : lightManager->getDirectionalLights()) {
    auto model = std::make_shared<Model3D>(std::vector{_state->getSettings()->getSwapchainColorFormat()},
                                           _boxModel->getNodes(), _boxModel->getMeshes(), lightManager,
                                           _commandBufferTransfer, _resourceManager, _state);
    model->enableDepth(false);
    model->enableShadow(false);
    model->enableLighting(false);
    _modelManager.push_back(model);
    _directionalLightModels.push_back(model);
  }
}

void DebugVisualization::_drawShadowMaps(std::shared_ptr<CommandBuffer> commandBuffer) {
  auto currentFrame = _state->getFrameInFlight();
  if (_showDepth) {
    if (_initializedDepth == false) {
      _descriptorSetDirectional.resize(_lightManager->getDirectionalLights().size());
      for (int i = 0; i < _lightManager->getDirectionalLights().size(); i++) {
        auto descriptorSet = std::make_shared<DescriptorSet>(_state->getSettings()->getMaxFramesInFlight(),
                                                             _textureSetLayout, _state->getDescriptorPool(),
                                                             _state->getDevice());
        descriptorSet->createTexture(
            {_lightManager->getDirectionalLights()[i]->getDepthTexture()[_state->getFrameInFlight()]});
        _descriptorSetDirectional[i] = descriptorSet;
        glm::vec3 pos = _lightManager->getDirectionalLights()[i]->getPosition();
        _shadowKeys.push_back("Dir " + std::to_string(i) + ": " + std::format("{:.2f}", pos.x) + "x" +
                              std::format("{:.2f}", pos.y));
      }
      _descriptorSetPoint.resize(_lightManager->getPointLights().size());
      for (int i = 0; i < _lightManager->getPointLights().size(); i++) {
        std::vector<std::shared_ptr<DescriptorSet>> cubeDescriptor(6);
        for (int j = 0; j < 6; j++) {
          auto descriptorSet = std::make_shared<DescriptorSet>(_state->getSettings()->getMaxFramesInFlight(),
                                                               _textureSetLayout, _state->getDescriptorPool(),
                                                               _state->getDevice());
          descriptorSet->createTexture(
              {_lightManager->getPointLights()[i]->getDepthCubemap()[currentFrame]->getTextureSeparate()[j]});
          cubeDescriptor[j] = descriptorSet;
          glm::vec3 pos = _lightManager->getPointLights()[i]->getPosition();
          _shadowKeys.push_back("Point " + std::to_string(i) + ": " + std::format("{:.2f}", pos.x) + "x" +
                                std::format("{:.2f}", pos.y) + "_" + std::to_string(j));
        }
        _descriptorSetPoint[i] = cubeDescriptor;
      }

      _initializedDepth = true;
    }

    if (_lightManager->getDirectionalLights().size() + _lightManager->getPointLights().size() > 0) {
      std::map<std::string, int*> toggleShadows;
      toggleShadows["##Shadows"] = &_shadowMapIndex;
      _gui->drawListBox(_shadowKeys, toggleShadows);

      // check if directional
      std::shared_ptr<DescriptorSet> shadowDescriptorSet;
      if (_shadowMapIndex < _lightManager->getDirectionalLights().size()) {
        shadowDescriptorSet = _descriptorSetDirectional[_shadowMapIndex];
      } else {
        int pointIndex = _shadowMapIndex - _lightManager->getDirectionalLights().size();
        int faceIndex = pointIndex % 6;
        // find index of point light
        pointIndex /= 6;
        shadowDescriptorSet = _descriptorSetPoint[pointIndex][faceIndex];
      }

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

      DepthPush pushConstants;
      pushConstants.near = _camera->getNear();
      pushConstants.far = _camera->getFar();
      vkCmdPushConstants(commandBuffer->getCommandBuffer()[currentFrame], _pipeline->getPipelineLayout(),
                         VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DepthPush), &pushConstants);

      glm::mat4 model = glm::mat4(1.f);
      auto [resX, resY] = _state->getSettings()->getResolution();
      float aspectY = (float)resX / (float)resY;
      glm::vec3 scale = glm::vec3(0.4f, aspectY * 0.4f, 1.f);
      // we don't multiple on view / projection so it's raw coords in NDC space (-1, 1)
      // we shift by x to right and by y to down, that's why -1.f + by Y axis
      // size of object is 1.0f, scale is 0.5f, so shift by X should be 0.25f so right edge is on right edge of screen
      // the same with Y
      model = glm::translate(model, glm::vec3(1.f - (0.5f * scale.x), -1.f + (0.5f * scale.y), 0.f));

      // need to compensate aspect ratio, texture is square but screen resolution is not
      model = glm::scale(model, scale);

      void* data;
      vkMapMemory(_state->getDevice()->getLogicalDevice(), _uniformBuffer->getBuffer()[currentFrame]->getMemory(), 0,
                  sizeof(glm::mat4), 0, &data);
      memcpy(data, &model, sizeof(glm::mat4));
      vkUnmapMemory(_state->getDevice()->getLogicalDevice(), _uniformBuffer->getBuffer()[currentFrame]->getMemory());

      VkBuffer vertexBuffers[] = {_meshSprite->getVertexBuffer()->getBuffer()->getData()};
      VkDeviceSize offsets[] = {0};
      vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer()[currentFrame], 0, 1, vertexBuffers, offsets);

      vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer()[currentFrame],
                           _meshSprite->getIndexBuffer()->getBuffer()->getData(), 0, VK_INDEX_TYPE_UINT32);

      vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                              _pipeline->getPipelineLayout(), 0, 1, &_cameraSet->getDescriptorSets()[currentFrame], 0,
                              nullptr);

      vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                              _pipeline->getPipelineLayout(), 1, 1,
                              &shadowDescriptorSet->getDescriptorSets()[currentFrame], 0, nullptr);

      vkCmdDrawIndexed(commandBuffer->getCommandBuffer()[currentFrame],
                       static_cast<uint32_t>(_meshSprite->getIndexData().size()), 1, 0, 0, 0);
    }
  }
}

void DebugVisualization::_drawFrustum(std::shared_ptr<CommandBuffer> commandBuffer) {
  if (_frustumDraw) {
    for (auto& line : _lineFrustum) {
      line->draw(_state->getSettings()->getResolution(), _camera, commandBuffer);
    }
  }
}

void DebugVisualization::setPostprocessing(std::shared_ptr<Postprocessing> postprocessing) {
  _postprocessing = postprocessing;
  _gamma = _postprocessing->getGamma();
  _exposure = _postprocessing->getExposure();
}

void DebugVisualization::calculate(std::shared_ptr<CommandBuffer> commandBuffer) {
  auto [resX, resY] = _state->getSettings()->getResolution();
  auto eye = _camera->getEye();
  auto direction = _camera->getDirection();
  _gui->drawText(

      {std::string("eye x: ") + std::format("{:.2f}", eye.x), std::string("eye y: ") + std::format("{:.2f}", eye.y),
       std::string("eye z: ") + std::format("{:.2f}", eye.z)});
  _gui->drawText({std::string("dir x: ") + std::format("{:.2f}", direction.x),
                  std::string("dir y: ") + std::format("{:.2f}", direction.y),
                  std::string("dir z: ") + std::format("{:.2f}", direction.z)});

  std::string buttonText = "Hide frustum";
  if (_frustumDraw == false) {
    buttonText = "Show frustum";
  }

  if (_gui->drawInputFloat({{"near", &_near}})) {
    auto cameraFly = std::dynamic_pointer_cast<CameraFly>(_camera);
    cameraFly->setProjectionParameters(cameraFly->getFOV(), _near, _camera->getFar());
  }

  if (_gui->drawInputFloat({{"far", &_far}})) {
    auto cameraFly = std::dynamic_pointer_cast<CameraFly>(_camera);
    cameraFly->setProjectionParameters(cameraFly->getFOV(), _camera->getNear(), _far);
  }

  if (_gui->drawButton("Save camera")) {
    _eyeSave = _camera->getEye();
    _dirSave = _camera->getDirection();
    _upSave = _camera->getUp();
    _angles = std::dynamic_pointer_cast<CameraFly>(_camera)->getAngles();
  }

  if (_gui->drawButton("Load camera")) {
    _camera->setViewParameters(_eyeSave, _dirSave, _upSave);
    std::dynamic_pointer_cast<CameraFly>(_camera)->setAngles(_angles.x, _angles.y, _angles.z);
  }

  auto clicked = _gui->drawButton(buttonText);
  _gui->drawCheckbox({{"Show planes", &_showPlanes}});
  if (_frustumDraw && _showPlanes) {
    if (_planesRegistered == false) {
      _spriteManager.push_back(_farPlaneCW);
      _spriteManager.push_back(_farPlaneCCW);
      _planesRegistered = true;
    }
  } else {
    if (_planesRegistered == true) {
      _spriteManager.erase(std::remove(_spriteManager.begin(), _spriteManager.end(), _farPlaneCW),
                           _spriteManager.end());
      _spriteManager.erase(std::remove(_spriteManager.begin(), _spriteManager.end(), _farPlaneCCW),
                           _spriteManager.end());
      _planesRegistered = false;
    }
  }

  if (clicked) {
    if (_frustumDraw == true) {
      _frustumDraw = false;
    } else {
      _frustumDraw = true;
      auto camera = std::dynamic_pointer_cast<CameraFly>(_camera);
      if (camera == nullptr) return;

      auto [resX, resY] = _state->getSettings()->getResolution();
      float height1 = 2 * tan(glm::radians(camera->getFOV() / 2.f)) * camera->getNear();
      float width1 = height1 * ((float)resX / (float)resY);
      _lineFrustum[0]->getMesh()->setPosition({glm::vec3(-width1 / 2.f, -height1 / 2.f, -camera->getNear()),
                                               glm::vec3(width1 / 2.f, -height1 / 2.f, -camera->getNear())},
                                              commandBuffer);
      _lineFrustum[1]->getMesh()->setPosition({glm::vec3(-width1 / 2.f, height1 / 2.f, -camera->getNear()),
                                               glm::vec3(width1 / 2.f, height1 / 2.f, -camera->getNear())},
                                              commandBuffer);
      _lineFrustum[2]->getMesh()->setPosition({glm::vec3(-width1 / 2.f, -height1 / 2.f, -camera->getNear()),
                                               glm::vec3(-width1 / 2.f, height1 / 2.f, -camera->getNear())},
                                              commandBuffer);
      _lineFrustum[3]->getMesh()->setPosition({glm::vec3(width1 / 2.f, -height1 / 2.f, -camera->getNear()),
                                               glm::vec3(width1 / 2.f, height1 / 2.f, -camera->getNear())},
                                              commandBuffer);

      float height2 = 2 * tan(glm::radians(camera->getFOV() / 2.f)) * camera->getFar();
      float width2 = height2 * ((float)resX / (float)resY);
      _lineFrustum[4]->getMesh()->setPosition({glm::vec3(-width2 / 2.f, -height2 / 2.f, -camera->getFar()),
                                               glm::vec3(width2 / 2.f, -height2 / 2.f, -camera->getFar())},
                                              commandBuffer);
      _lineFrustum[5]->getMesh()->setPosition({glm::vec3(-width2 / 2.f, height2 / 2.f, -camera->getFar()),
                                               glm::vec3(width2 / 2.f, height2 / 2.f, -camera->getFar())},
                                              commandBuffer);
      _lineFrustum[6]->getMesh()->setPosition({glm::vec3(-width2 / 2.f, -height2 / 2.f, -camera->getFar()),
                                               glm::vec3(-width2 / 2.f, height2 / 2.f, -camera->getFar())},
                                              commandBuffer);
      _lineFrustum[7]->getMesh()->setPosition({glm::vec3(width2 / 2.f, -height2 / 2.f, -camera->getFar()),
                                               glm::vec3(width2 / 2.f, height2 / 2.f, -camera->getFar())},
                                              commandBuffer);

      auto model = glm::scale(glm::mat4(1.f), glm::vec3(width2, height2, 1.f));
      model = glm::translate(model, glm::vec3(0.f, 0.f, -camera->getFar()));
      _farPlaneCW->setModel(glm::inverse(camera->getView()) * model);
      model = glm::rotate(model, glm::radians(180.f), glm::vec3(1, 0, 0));
      _farPlaneCCW->setModel(glm::inverse(camera->getView()) * model);

      // bottom
      _lineFrustum[8]->getMesh()->setPosition({glm::vec3(0), _lineFrustum[4]->getMesh()->getVertexData()[0].pos},
                                              commandBuffer);
      _lineFrustum[8]->getMesh()->setColor({glm::vec3(0.f, 0.f, 1.f), glm::vec3(0.f, 0.f, 1.f)}, commandBuffer);
      _lineFrustum[9]->getMesh()->setPosition({glm::vec3(0), _lineFrustum[4]->getMesh()->getVertexData()[1].pos},
                                              commandBuffer);
      _lineFrustum[9]->getMesh()->setColor({glm::vec3(0.f, 0.f, 1.f), glm::vec3(0.f, 0.f, 1.f)}, commandBuffer);
      // top
      _lineFrustum[10]->getMesh()->setPosition({glm::vec3(0.f), _lineFrustum[5]->getMesh()->getVertexData()[0].pos},
                                               commandBuffer);
      _lineFrustum[10]->getMesh()->setColor({glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 1.f, 0.f)}, commandBuffer);
      _lineFrustum[11]->getMesh()->setPosition({glm::vec3(0), _lineFrustum[5]->getMesh()->getVertexData()[1].pos},
                                               commandBuffer);
      _lineFrustum[11]->getMesh()->setColor({glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 1.f, 0.f)}, commandBuffer);

      for (auto& line : _lineFrustum) {
        line->setModel(glm::inverse(camera->getView()));
      }
    }
  }
}

void DebugVisualization::draw(int currentFrame, std::shared_ptr<CommandBuffer> commandBuffer) {
  std::map<std::string, bool*> toggleDepth;
  toggleDepth["Depth"] = &_showDepth;
  _gui->drawCheckbox(toggleDepth);

  std::map<std::string, bool*> toggleNormals;
  toggleNormals["Normals"] = &_showNormals;
  if (_gui->drawCheckbox(toggleNormals)) {
    if (_showNormals) {
      _state->getSettings()->setDrawType(_state->getSettings()->getDrawType() | DrawType::NORMAL);
    } else {
      _state->getSettings()->setDrawType(_state->getSettings()->getDrawType() & ~DrawType::NORMAL);
    }
  }
  std::map<std::string, bool*> toggleWireframe;
  toggleWireframe["Wireframe"] = &_showWireframe;
  if (_gui->drawCheckbox(toggleWireframe)) {
    if (_showWireframe) {
      _state->getSettings()->setDrawType(_state->getSettings()->getDrawType() | DrawType::WIREFRAME);
    } else {
      _state->getSettings()->setDrawType(_state->getSettings()->getDrawType() & ~DrawType::WIREFRAME);
    }
  }

  if (_lightManager) {
    std::map<std::string, bool*> toggle;
    toggle["Lights"] = &_showLights;
    _gui->drawCheckbox(toggle);
    if (_showLights) {
      std::map<std::string, bool*> toggle;
      toggle["Spheres"] = &_enableSpheres;
      _gui->drawCheckbox(toggle);
      if (_enableSpheres) {
        std::map<std::string, int*> toggleSpheres;
        toggleSpheres["##Spheres"] = &_lightSpheresIndex;
        _gui->drawListBox(_attenuationKeys, toggleSpheres);
      }
      for (int i = 0; i < _lightManager->getPointLights().size(); i++) {
        if (_registerLights) _modelManager.push_back(_pointLightModels[i]);
        {
          auto model = glm::translate(glm::mat4(1.f), _lightManager->getPointLights()[i]->getPosition());
          model = glm::scale(model, glm::vec3(0.2f, 0.2f, 0.2f));
          _pointLightModels[i]->setModel(model);
        }
        {
          auto model = glm::translate(glm::mat4(1.f), _lightManager->getPointLights()[i]->getPosition());
          if (_enableSpheres) {
            if (_lightSpheresIndex < 0) _lightSpheresIndex = _lightManager->getPointLights()[i]->getAttenuationIndex();
            _lightManager->getPointLights()[i]->setAttenuationIndex(_lightSpheresIndex);
            int distance = _lightManager->getPointLights()[i]->getDistance();
            model = glm::scale(model, glm::vec3(distance, distance, distance));
            _spheres[i]->setModel(model);
            _spheres[i]->setDrawType(DrawType::WIREFRAME);
            _spheres[i]->draw(_state->getSettings()->getResolution(), _camera, commandBuffer);
          }
        }
      }
      for (int i = 0; i < _lightManager->getDirectionalLights().size(); i++) {
        if (_registerLights) _modelManager.push_back(_directionalLightModels[i]);
        auto model = glm::translate(glm::mat4(1.f), _lightManager->getDirectionalLights()[i]->getPosition());
        model = glm::scale(model, glm::vec3(0.2f, 0.2f, 0.2f));
        _directionalLightModels[i]->setModel(model);
      }
      _registerLights = false;
    } else {
      if (_registerLights == false) {
        _registerLights = true;
        for (int i = 0; i < _lightManager->getPointLights().size(); i++) {
          _modelManager.erase(std::remove(_modelManager.begin(), _modelManager.end(), _pointLightModels[i]),
                              _modelManager.end());
        }
        for (int i = 0; i < _lightManager->getDirectionalLights().size(); i++) {
          _modelManager.erase(std::remove(_modelManager.begin(), _modelManager.end(), _directionalLightModels[i]),
                              _modelManager.end());
        }
      }
    }

    _gui->drawInputFloat({{"gamma", &_gamma}});
    _postprocessing->setGamma(_gamma);
    _gui->drawInputFloat({{"exposure", &_exposure}});
    _postprocessing->setExposure(_exposure);
    _gui->drawInputFloat({{"R", &_R}});
    _R = std::min(_R, 1.f);
    _R = std::max(_R, 0.f);
    _gui->drawInputFloat({{"G", &_G}});
    _G = std::min(_G, 1.f);
    _G = std::max(_G, 0.f);
    _gui->drawInputFloat({{"B", &_B}});
    _B = std::min(_B, 1.f);
    _B = std::max(_B, 0.f);
    _state->getSettings()->setClearColor({_R, _G, _B, 1.f});
  }

  for (auto& model : _modelManager) {
    model->draw(_state->getSettings()->getResolution(), _camera, commandBuffer);
  }
  for (auto& sprite : _spriteManager) {
    sprite->draw(_state->getSettings()->getResolution(), _camera, commandBuffer);
  }
  _drawFrustum(commandBuffer);

  _drawShadowMaps(commandBuffer);
}

void DebugVisualization::cursorNotify(GLFWwindow* window, float xPos, float yPos) {}

void DebugVisualization::mouseNotify(GLFWwindow* window, int button, int action, int mods) {}

void DebugVisualization::keyNotify(GLFWwindow* window, int key, int scancode, int action, int mods) {
  if ((action == GLFW_RELEASE && key == GLFW_KEY_C)) {
    if (_cursorEnabled) {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
      _cursorEnabled = false;
    } else {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
      _cursorEnabled = true;
    }
  }
}

void DebugVisualization::charNotify(GLFWwindow* window, unsigned int code) {}

void DebugVisualization::scrollNotify(GLFWwindow* window, double xOffset, double yOffset) {}