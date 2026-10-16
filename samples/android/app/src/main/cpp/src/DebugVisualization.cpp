#include "DebugVisualization.h"
#include <glm/gtx/matrix_decompose.hpp>

DebugVisualization::DebugVisualization(std::shared_ptr<Camera> camera, std::shared_ptr<Core> core) {
  _camera = camera;
  _core = core;
  auto state = core->getEngineState();

  for (auto elem : _core->getEngineState()->getSettings()->getAttenuations()) {
    _attenuationKeys.push_back(std::to_string(std::get<0>(elem)));
  }

  _lineFrustum.resize(12);
  for (int i = 0; i < _lineFrustum.size(); i++) {
    auto line = _core->createLine();
    auto mesh = line->getMesh();
    mesh->setColor({glm::vec3(1.f, 0.f, 0.f)});
    _lineFrustum[i] = line;
  }

  _near = _camera->getNear();
  _far = _camera->getFar();
  _farPlaneCW = _core->createSprite();
  _farPlaneCW->enableLighting(false);
  _farPlaneCW->enableShadow(false);
  _farPlaneCW->enableDepth(false);
  _farPlaneCCW = _core->createSprite();
  _farPlaneCCW->enableLighting(false);
  _farPlaneCCW->enableShadow(false);
  _farPlaneCCW->enableDepth(false);

  auto boxModel = _core->createModelGLTF("box/Box.gltf");
  auto meshSphere = std::make_shared<MeshSphere>(_core->getEngineState());
  for (auto light : _core->getPointLights()) {
    _pointValue = light->getColor()[0];
    auto model = _core->createModel3D(boxModel);
    model->enableDepth(false);
    model->enableShadow(false);
    model->enableLighting(false);
    core->addDrawable(model);
    _pointLightModels.push_back(model);

    auto sphere = _core->createShape3D(ShapeType::SPHERE, meshSphere, VK_CULL_MODE_NONE);
    _spheres.push_back(sphere);
  }

  for (auto light : _core->getDirectionalLights()) {
    _directionalValue = light->getColor()[0];
    auto model = _core->createModel3D(boxModel);
    model->enableDepth(false);
    model->enableShadow(false);
    model->enableLighting(false);
    _core->addDrawable(model);
    _directionalLightModels.push_back(model);
  }

  glm::mat4 model = glm::mat4(1.f);
  auto [resX, resY] = state->getSettings()->getResolution();
  float aspectY = (float)resX / (float)resY;
  glm::vec3 scale = glm::vec3(0.4f, aspectY * 0.4f, 1.f);
  // we don't multiple on view / projection so it's raw coords in NDC space (-1,
  // 1) we shift by x to right and by y to down, that's why -1.f + by Y axis
  // size of object is 1.0f, scale is 0.5f, so shift by X should be 0.25f so
  // right edge is on right edge of screen the same with Y
  // need to compensate aspect ratio, texture is square but screen resolution is not
  _spriteShadow = _core->createSprite();
  _spriteShadow->setScale(scale);
  _spriteShadow->setTranslate(glm::vec3(1.f - (0.5f * scale.x), -1.f + (1.f * scale.y), 0.f));
  _spriteShadow->enableHUD(true);
  _materialShadow = _core->createMaterialColor(MaterialTarget::SIMPLE);
  _materialShadow->setBaseColor({core->getResourceManager()->getTextureOne()});
  _spriteShadow->setMaterial(_materialShadow);
}

void DebugVisualization::_drawFrustumLines(glm::vec3 nearPart, glm::vec3 farPart) {
  _lineFrustum[0]->getMesh()->setPosition({glm::vec3(-nearPart.x / 2.f, -nearPart.y / 2.f, -nearPart.z),
                                           glm::vec3(nearPart.x / 2.f, -nearPart.y / 2.f, -nearPart.z)});
  _lineFrustum[1]->getMesh()->setPosition({glm::vec3(-nearPart.x / 2.f, nearPart.y / 2.f, -nearPart.z),
                                           glm::vec3(nearPart.x / 2.f, nearPart.y / 2.f, -nearPart.z)});
  _lineFrustum[2]->getMesh()->setPosition({glm::vec3(-nearPart.x / 2.f, -nearPart.y / 2.f, -nearPart.z),
                                           glm::vec3(-nearPart.x / 2.f, nearPart.y / 2.f, -nearPart.z)});
  _lineFrustum[3]->getMesh()->setPosition({glm::vec3(nearPart.x / 2.f, -nearPart.y / 2.f, -nearPart.z),
                                           glm::vec3(nearPart.x / 2.f, nearPart.y / 2.f, -nearPart.z)});
  _lineFrustum[4]->getMesh()->setPosition({glm::vec3(-farPart.x / 2.f, -farPart.y / 2.f, -farPart.z),
                                           glm::vec3(farPart.x / 2.f, -farPart.y / 2.f, -farPart.z)});
  _lineFrustum[5]->getMesh()->setPosition({glm::vec3(-farPart.x / 2.f, farPart.y / 2.f, -farPart.z),
                                           glm::vec3(farPart.x / 2.f, farPart.y / 2.f, -farPart.z)});
  _lineFrustum[6]->getMesh()->setPosition({glm::vec3(-farPart.x / 2.f, -farPart.y / 2.f, -farPart.z),
                                           glm::vec3(-farPart.x / 2.f, farPart.y / 2.f, -farPart.z)});
  _lineFrustum[7]->getMesh()->setPosition({glm::vec3(farPart.x / 2.f, -farPart.y / 2.f, -farPart.z),
                                           glm::vec3(farPart.x / 2.f, farPart.y / 2.f, -farPart.z)});
  // bottom
  _lineFrustum[8]->getMesh()->setPosition({glm::vec3(0), _lineFrustum[4]->getMesh()->getVertexData()[0].pos});
  _lineFrustum[8]->getMesh()->setColor({glm::vec3(0.f, 0.f, 1.f), glm::vec3(0.f, 0.f, 1.f)});
  _lineFrustum[9]->getMesh()->setPosition({glm::vec3(0), _lineFrustum[4]->getMesh()->getVertexData()[1].pos});
  _lineFrustum[9]->getMesh()->setColor({glm::vec3(0.f, 0.f, 1.f), glm::vec3(0.f, 0.f, 1.f)});
  // top
  _lineFrustum[10]->getMesh()->setPosition({glm::vec3(0.f), _lineFrustum[5]->getMesh()->getVertexData()[0].pos});
  _lineFrustum[10]->getMesh()->setColor({glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 1.f, 0.f)});
  _lineFrustum[11]->getMesh()->setPosition({glm::vec3(0), _lineFrustum[5]->getMesh()->getVertexData()[1].pos});
  _lineFrustum[11]->getMesh()->setColor({glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 1.f, 0.f)});
}

void DebugVisualization::_drawShadowMaps() {
  auto currentFrame = _core->getEngineState()->getFrameInFlight();
  auto gui = _core->getGUI();
  if (_showDepth) {
    if (_initializedDepth == false) {
      for (int i = 0; i < _core->getDirectionalLights().size(); i++) {
        glm::vec3 pos = _core->getDirectionalLights()[i]->getCamera()->getPosition();
        _shadowKeys.push_back("Dir " + std::to_string(i) + ": " + std::to_string(pos.x) + "x" + std::to_string(pos.y));
      }
      for (int i = 0; i < _core->getPointLights().size(); i++) {
        for (int j = 0; j < 6; j++) {
          glm::vec3 pos = _core->getPointLights()[i]->getCamera()->getPosition();
          _shadowKeys.push_back("Point " + std::to_string(i) + ": " + std::to_string(pos.x) + "x" +
                                std::to_string(pos.y) + "_" + std::to_string(j));
        }
      }

      _initializedDepth = true;
    }

    if (_core->getDirectionalLights().size() + _core->getPointLights().size() > 0) {
      std::map<std::string, int*> toggleShadows;
      toggleShadows["##Shadows"] = &_shadowMapIndex;
      gui->drawListBox(_shadowKeys, toggleShadows, _shadowKeys.size());

      std::shared_ptr<RenderGraph::Texture> currentTexture;
      // check if directional
      if (_shadowMapIndex < _core->getDirectionalLights().size()) {
        currentTexture = _core->getDirectionalShadows()[_shadowMapIndex]->getShadowMapTexture()[currentFrame];
      } else {
        int pointIndex = _shadowMapIndex - _core->getDirectionalLights().size();
        int faceIndex = pointIndex % 6;
        // find index of point light
        pointIndex /= 6;
        currentTexture = _core->getPointShadows()[pointIndex]
                             ->getShadowMapCubemap()[currentFrame]
                             ->getTextureSeparate()[faceIndex][0];
      }

      _materialShadow->setBaseColor({currentTexture});

      auto drawables = _core->getDrawables(AlphaType::OPAQUE);
      if (std::find(drawables.begin(), drawables.end(), _spriteShadow) == drawables.end()) {
        _core->addDrawable(_spriteShadow, AlphaType::OPAQUE);
      }
    }
  } else {
    auto drawables = _core->getDrawables(AlphaType::OPAQUE);
    if (std::find(drawables.begin(), drawables.end(), _spriteShadow) != drawables.end()) {
      _core->removeDrawable(_spriteShadow);
    }
  }
}

void DebugVisualization::_drawFrustum() {
  if (_frustumDraw) {
    for (auto& line : _lineFrustum) {
      auto drawables = _core->getDrawables(AlphaType::OPAQUE);
      if (std::find(drawables.begin(), drawables.end(), line) == drawables.end()) {
        _core->addDrawable(line, AlphaType::OPAQUE);
      }
    }
  } else {
    for (auto& line : _lineFrustum) {
      auto drawables = _core->getDrawables(AlphaType::OPAQUE);
      if (std::find(drawables.begin(), drawables.end(), line) != drawables.end()) {
        _core->removeDrawable(line);
      }
    }
  }
}

void DebugVisualization::update() {
  if (_registerLights == false) {
    for (int i = 0; i < _core->getPointLights().size(); i++) {
      _pointLightModels[i]->setTranslate(_core->getPointLights()[i]->getCamera()->getPosition());
      _pointLightModels[i]->setScale(glm::vec3(0.2f, 0.2f, 0.2f));
    }

    for (int i = 0; i < _core->getDirectionalLights().size(); i++) {
      _directionalLightModels[i]->setTranslate(_core->getDirectionalLights()[i]->getCamera()->getPosition());
      _directionalLightModels[i]->setScale(glm::vec3(0.2f, 0.2f, 0.2f));
    }
  }
}

void DebugVisualization::draw() {
  auto gui = _core->getGUI();

  auto [resX, resY] = _core->getEngineState()->getSettings()->getResolution();
  auto eye = _camera->getEye();
  auto direction = _camera->getDirection();
  if (gui->startTree("Coordinates")) {
    gui->drawText({std::string("eye x: ") + std::to_string(eye.x), std::string("eye y: ") + std::to_string(eye.y),
                   std::string("eye z: ") + std::to_string(eye.z)});
    gui->drawText({std::string("dir x: ") + std::to_string(direction.x),
                   std::string("dir y: ") + std::to_string(direction.y),
                   std::string("dir z: ") + std::to_string(direction.z)});
    if (gui->drawButton("Save camera")) {
      _eyeSave = _camera->getEye();
      _dirSave = _camera->getDirection();
      _upSave = _camera->getUp();
      _angles = std::dynamic_pointer_cast<CameraFly>(_camera)->getAngles();
    }

    if (gui->drawButton("Load camera")) {
      _camera->setViewParameters(_eyeSave, _dirSave, _upSave);
      std::dynamic_pointer_cast<CameraFly>(_camera)->setAngles(_angles.x, _angles.y, _angles.z);
    }
    gui->endTree();
  }

  if (gui->startTree("Frustum")) {
    std::string buttonText = "Hide frustum";
    if (_frustumDraw == false) {
      buttonText = "Show frustum";
    }

    if (gui->drawInputFloat({{"near", &_near}})) {
      auto cameraFly = std::dynamic_pointer_cast<CameraFly>(_camera);
      cameraFly->setProjectionParameters(cameraFly->getFOV(), _near, _camera->getFar());
    }

    if (gui->drawInputFloat({{"far", &_far}})) {
      auto cameraFly = std::dynamic_pointer_cast<CameraFly>(_camera);
      cameraFly->setProjectionParameters(cameraFly->getFOV(), _camera->getNear(), _far);
    }

    auto clicked = gui->drawButton(buttonText);
    gui->drawCheckbox({{"Show planes", &_showPlanes}});
    if (_frustumDraw && _showPlanes) {
      if (_planesRegistered == false) {
        _core->addDrawable(_farPlaneCW);
        _core->addDrawable(_farPlaneCCW);
        _planesRegistered = true;
      }
    } else {
      if (_planesRegistered == true) {
        _core->removeDrawable(_farPlaneCW);
        _core->removeDrawable(_farPlaneCCW);
        _planesRegistered = false;
      }
    }
    if (clicked) {
      if (_frustumDraw == true) {
        _frustumDraw = false;
      } else {
        _frustumDraw = true;
        auto camera = std::dynamic_pointer_cast<CameraFly>(_camera);
        auto [resX, resY] = _core->getEngineState()->getSettings()->getResolution();
        float height1 = 2 * tan(glm::radians(camera->getFOV() / 2.f)) * camera->getNear();
        float width1 = height1 * ((float)resX / (float)resY);
        float height2 = 2 * tan(glm::radians(camera->getFOV() / 2.f)) * camera->getFar();
        float width2 = height2 * ((float)resX / (float)resY);
        _drawFrustumLines(glm::vec3(width1, height1, camera->getNear()), glm::vec3(width2, height2, camera->getFar()));
        auto model = glm::scale(glm::mat4(1.f), glm::vec3(width2, height2, 1.f));
        model = glm::translate(model, glm::vec3(0.f, 0.f, -camera->getFar()));
        {
          auto transform = glm::inverse(camera->getView()) * model;
          glm::vec3 scale;
          glm::quat rotation;
          glm::vec3 translation;
          glm::vec3 skew;
          glm::vec4 perspective;
          glm::decompose(transform, scale, rotation, translation, skew, perspective);
          _farPlaneCW->setRotate(rotation);
          _farPlaneCW->setTranslate(translation);
          _farPlaneCW->setScale(scale);
        }
        model = glm::rotate(model, glm::radians(180.f), glm::vec3(1, 0, 0));
        {
          auto transform = glm::inverse(camera->getView()) * model;
          glm::vec3 scale;
          glm::quat rotation;
          glm::vec3 translation;
          glm::vec3 skew;
          glm::vec4 perspective;
          glm::decompose(transform, scale, rotation, translation, skew, perspective);
          _farPlaneCCW->setRotate(rotation);
          _farPlaneCCW->setTranslate(translation);
          _farPlaneCCW->setScale(scale);
        }
        for (auto& line : _lineFrustum) {
          line->setModel(glm::inverse(camera->getView()));
        }
      }
    }
    _drawFrustum();
    gui->endTree();
  }
  if (gui->startTree("Postprocessing")) {
    float gamma = _core->getPostprocessing()->getGamma();
    if (_core->getGUI()->drawInputFloat({{"gamma", &gamma}})) _core->getPostprocessing()->setGamma(gamma);
    float exposure = _core->getPostprocessing()->getExposure();
    if (_core->getGUI()->drawInputFloat({{"exposure", &exposure}})) _core->getPostprocessing()->setExposure(exposure);

    gui->endTree();
  }

  if (gui->startTree("Light")) {
    if (_core->getGUI()->drawSlider({{"Directional", &_directionalValue}, {"Point", &_pointValue}},
                                    {{"Directional", {0.f, 20.f}}, {"Point", {0.f, 20.f}}})) {
      for (auto& light : _core->getDirectionalLights()) {
        light->setColor(glm::vec3(_directionalValue, _directionalValue, _directionalValue));
      }
      for (auto& light : _core->getPointLights()) {
        light->setColor(glm::vec3(_pointValue, _pointValue, _pointValue));
      }
    }

    std::map<std::string, bool*> toggle;
    toggle["Lights"] = &_showLights;
    gui->drawCheckbox(toggle);
    if (_showLights) {
      std::map<std::string, bool*> toggle;
      toggle["Spheres"] = &_enableSpheres;
      gui->drawCheckbox(toggle);
      if (_enableSpheres) {
        std::map<std::string, int*> toggleSpheres;
        toggleSpheres["##Spheres"] = &_lightSpheresIndex;
        gui->drawListBox(_attenuationKeys, toggleSpheres, 4);
      }
      for (int i = 0; i < _core->getPointLights().size(); i++) {
        if (_registerLights) _core->addDrawable(_pointLightModels[i]);
        {
          if (_enableSpheres) {
            if (_lightSpheresIndex < 0) _lightSpheresIndex = _core->getPointLights()[i]->getAttenuationIndex();
            _core->getPointLights()[i]->setAttenuationIndex(_lightSpheresIndex);
            int distance = _core->getPointLights()[i]->getDistance();
            _spheres[i]->setTranslate(_core->getPointLights()[i]->getCamera()->getPosition());
            _spheres[i]->setScale(glm::vec3(distance, distance, distance));
            _spheres[i]->setDrawType(DrawType::WIREFRAME);
            auto drawables = _core->getDrawables(AlphaType::OPAQUE);
            if (std::find(drawables.begin(), drawables.end(), _spheres[i]) == drawables.end()) {
              _core->addDrawable(_spheres[i], AlphaType::OPAQUE);
            }
          } else {
            auto drawables = _core->getDrawables(AlphaType::OPAQUE);
            if (std::find(drawables.begin(), drawables.end(), _spheres[i]) != drawables.end()) {
              _core->removeDrawable(_spheres[i]);
            }
          }
        }
      }
      for (int i = 0; i < _core->getDirectionalLights().size(); i++) {
        if (_registerLights) _core->addDrawable(_directionalLightModels[i]);
      }
      _registerLights = false;
    } else {
      if (_registerLights == false) {
        _registerLights = true;
        for (int i = 0; i < _core->getPointLights().size(); i++) {
          _core->removeDrawable(_pointLightModels[i]);
        }
        for (int i = 0; i < _core->getDirectionalLights().size(); i++) {
          _core->removeDrawable(_directionalLightModels[i]);
        }
      }
    }
    std::map<std::string, bool*> toggleDepth;
    toggleDepth["Depth"] = &_showDepth;
    gui->drawCheckbox(toggleDepth);
    _drawShadowMaps();

    gui->endTree();
  }
}
