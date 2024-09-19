#include <iostream>
#include <chrono>
#include <future>
#include "Main.h"
#include <glm/gtx/matrix_decompose.hpp>
#include "Utility/PhysicsManager.h"
#include <Jolt/Physics/Character/CharacterVirtual.h>

glm::vec3 getPosition(std::shared_ptr<Drawable> drawable) {
  glm::vec3 scale;
  glm::quat rotation;
  glm::vec3 translation;
  glm::vec3 skew;
  glm::vec4 perspective;
  glm::decompose(drawable->getModel(), scale, rotation, translation, skew, perspective);
  return translation;
}

float getHeight(std::tuple<std::shared_ptr<uint8_t[]>, std::tuple<int, int, int>> heightmap, glm::vec3 position) {
  auto [data, dimension] = heightmap;
  auto [width, height, channels] = dimension;

  float x = position.x + (width - 1) / 2.f;
  float z = position.z + (height - 1) / 2.f;
  int xIntegral = x;
  int zIntegral = z;
  // 0 1
  // 2 3

  // channels 2, width 4, (2, 1) = 2 * 2 + 1 * 4 * 2 = 12
  // 0 0 1 1 2 2 3 3
  // 4 4 5 5 6 6 7 7
  int index0 = xIntegral * channels + zIntegral * (width * channels);
  int index1 = (xIntegral + 1) * channels + zIntegral * (width * channels);
  int index2 = xIntegral * channels + (zIntegral + 1) * (width * channels);
  int index3 = (xIntegral + 1) * channels + (zIntegral + 1) * (width * channels);
  float sample0 = data[index0] / 255.f;
  float sample1 = data[index1] / 255.f;
  float sample2 = data[index2] / 255.f;
  float sample3 = data[index3] / 255.f;
  float fxy1 = sample0 + (x - xIntegral) * (sample1 - sample0);
  float fxy2 = sample2 + (x - xIntegral) * (sample3 - sample2);
  float sample = fxy1 + (z - zIntegral) * (fxy2 - fxy1);
  return sample * 64.f - 16.f;
}

void InputHandler::setMoveCallback(std::function<void(std::optional<glm::vec3>)> callback) { _callback = callback; }

InputHandler::InputHandler(std::shared_ptr<Core> core) { _core = core; }

void InputHandler::cursorNotify(float xPos, float yPos) {}

void InputHandler::mouseNotify(int button, int action, int mods) {}

void InputHandler::keyNotify(int key, int scancode, int action, int mods) {
#ifndef __ANDROID__
  if ((action == GLFW_RELEASE && key == GLFW_KEY_C)) {
    if (_cursorEnabled) {
      _core->getState()->getInput()->showCursor(false);
      _cursorEnabled = false;
    } else {
      _core->getState()->getInput()->showCursor(true);
      _cursorEnabled = true;
    }
  }
  std::optional<glm::vec3> shift = std::nullopt;
  if (action != GLFW_RELEASE && key == GLFW_KEY_UP) {
    shift = glm::vec3(0.f, 0.f, -2.f);
  }
  if (action != GLFW_RELEASE && key == GLFW_KEY_DOWN) {
    shift = glm::vec3(0.f, 0.f, 2.f);
  }
  if (action != GLFW_RELEASE && key == GLFW_KEY_LEFT) {
    shift = glm::vec3(-2.f, 0.f, 0.f);
  }
  if (action != GLFW_RELEASE && key == GLFW_KEY_RIGHT) {
    shift = glm::vec3(2.f, 0.f, 0.f);
  }

  _callback(shift);
#endif
}

void InputHandler::charNotify(unsigned int code) {}

void InputHandler::scrollNotify(double xOffset, double yOffset) {}

void Main::_createTerrainColor() {
  _core->removeDrawable(_terrain);
  _terrain = _core->createTerrain(_core->loadImageCPU("../assets/heightmap.png"), {_patchX, _patchY});
  _terrain->setMaterial(_materialColor);

  _terrain->setTessellationLevel(_minTessellationLevel, _maxTessellationLevel);
  _terrain->setDisplayDistance(_minDistance, _maxDistance);
  _terrain->setColorHeightLevels(_heightLevels);
  _terrain->setHeight(_heightScale, _heightShift);
  _terrain->patchEdge(_showPatches);
  _terrain->showLoD(_showLoD);
  if (_showWireframe) {
    _terrain->setDrawType(DrawType::WIREFRAME);
  }
  if (_showNormals) {
    _terrain->setDrawType(DrawType::NORMAL);
  }
  if (_showWireframe == false && _showNormals == false) {
    _terrain->setDrawType(DrawType::FILL);
  }

  _core->addDrawable(_terrain);
}

Main::Main() {
  int mipMapLevels = 8;
  auto settings = std::make_shared<Settings>();
  settings->setName("Terrain");
  settings->setClearColor({0.01f, 0.01f, 0.01f, 1.f});
  // TODO: fullscreen if resolution is {0, 0}
  // TODO: validation layers complain if resolution is {2560, 1600}
  settings->setResolution(std::tuple{1920, 1080});
  // for HDR, linear 16 bit per channel to represent values outside of 0-1 range (UNORM - float [0, 1], SFLOAT - float)
  // https://registry.khronos.org/vulkan/specs/1.1/html/vkspec.html#_identification_of_formats
  settings->setGraphicColorFormat(VK_FORMAT_R32G32B32A32_SFLOAT);
  settings->setSwapchainColorFormat(VK_FORMAT_B8G8R8A8_UNORM);
  // SRGB the same as UNORM but + gamma conversion out of box (!)
  settings->setLoadTextureColorFormat(VK_FORMAT_R8G8B8A8_SRGB);
  settings->setLoadTextureAuxilaryFormat(VK_FORMAT_R8G8B8A8_UNORM);
  settings->setAnisotropicSamples(4);
  settings->setDepthFormat(VK_FORMAT_D32_SFLOAT);
  settings->setMaxFramesInFlight(2);
  settings->setThreadsInPool(6);
  settings->setDesiredFPS(60);

  _core = std::make_shared<Core>(settings);
  _core->initialize();
  _core->startRecording();
  _camera = std::make_shared<CameraFly>(_core->getState());
  _camera->setSpeed(0.05f, 0.5f);
  _camera->setProjectionParameters(60.f, 0.1f, 100.f);
  _core->getState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_camera));
  _inputHandler = std::make_shared<InputHandler>(_core);
  _core->getState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_inputHandler));
  _core->setCamera(_camera);

  _pointLightVertical = _core->createPointLight(settings->getDepthResolution());
  _pointLightVertical->setColor(glm::vec3(1.f, 1.f, 1.f));
  _pointLightHorizontal = _core->createPointLight(settings->getDepthResolution());
  _pointLightHorizontal->setColor(glm::vec3(1.f, 1.f, 1.f));
  _directionalLight = _core->createDirectionalLight(settings->getDepthResolution());
  _directionalLight->setColor(glm::vec3(1.f, 1.f, 1.f));
  _directionalLight->setPosition(glm::vec3(0.f, 20.f, 0.f));
  // TODO: rename setCenter to lookAt
  //  looking to (0.f, 0.f, 0.f) with up vector (0.f, 0.f, -1.f)
  _directionalLight->setCenter({0.f, 0.f, 0.f});
  _directionalLight->setUp({0.f, 0.f, -1.f});
  auto ambientLight = _core->createAmbientLight();
  ambientLight->setColor({0.1f, 0.1f, 0.1f});

  // cube colored light
  _cubeColoredLightVertical = _core->createShape3D(ShapeType::CUBE);
  _cubeColoredLightVertical->getMesh()->setColor(
      std::vector{_cubeColoredLightVertical->getMesh()->getVertexData().size(), glm::vec3(1.f, 1.f, 1.f)},
      _core->getCommandBufferApplication());
  _core->addDrawable(_cubeColoredLightVertical);

  _cubeColoredLightHorizontal = _core->createShape3D(ShapeType::CUBE);
  _cubeColoredLightHorizontal->getMesh()->setColor(
      std::vector{_cubeColoredLightHorizontal->getMesh()->getVertexData().size(), glm::vec3(1.f, 1.f, 1.f)},
      _core->getCommandBufferApplication());
  _core->addDrawable(_cubeColoredLightHorizontal);

  auto cubeColoredLightDirectional = _core->createShape3D(ShapeType::CUBE);
  cubeColoredLightDirectional->getMesh()->setColor(
      std::vector{cubeColoredLightDirectional->getMesh()->getVertexData().size(), glm::vec3(1.f, 1.f, 1.f)},
      _core->getCommandBufferApplication());
  {
    auto model = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 20.f, 0.f));
    model = glm::scale(model, glm::vec3(0.3f, 0.3f, 0.3f));
    cubeColoredLightDirectional->setModel(model);
  }
  _core->addDrawable(cubeColoredLightDirectional);

  auto heightmapCPU = _core->loadImageCPU("../assets/heightmap.png");

  _physicsManager = std::make_shared<PhysicsManager>();
  _terrainPhysics = std::make_shared<TerrainPhysics>(heightmapCPU, std::tuple{64, 16}, _physicsManager);

  _shape3DPhysics = std::make_shared<Shape3DPhysics>(glm::vec3(0.f, 50.f, 0.f), glm::vec3(0.5f, 0.5f, 0.5f),
                                                     _physicsManager);
  _cubePlayer = _core->createShape3D(ShapeType::CUBE);
  _cubePlayer->setModel(glm::translate(glm::mat4(1.f), glm::vec3(0.f, 2.f, 0.f)));
  _cubePlayer->getMesh()->setColor(
      std::vector{_cubePlayer->getMesh()->getVertexData().size(), glm::vec3(0.f, 0.f, 1.f)},
      _core->getCommandBufferApplication());
  _core->addDrawable(_cubePlayer);
  auto callbackPosition = [&](std::optional<glm::vec3> shift) { _shift = shift; };
  _inputHandler->setMoveCallback(callbackPosition);

  {
    auto tile0 = _core->createTexture("../assets/desert/albedo.png", settings->getLoadTextureColorFormat(),
                                      mipMapLevels);
    auto tile1 = _core->createTexture("../assets/rock/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile2 = _core->createTexture("../assets/grass/albedo.png", settings->getLoadTextureColorFormat(),
                                      mipMapLevels);
    auto tile3 = _core->createTexture("../assets/ground/albedo.png", settings->getLoadTextureColorFormat(),
                                      mipMapLevels);
    _materialColor = _core->createMaterialColor(MaterialTarget::TERRAIN);
    _materialColor->setBaseColor({tile0, tile1, tile2, tile3});
  }

  _createTerrainColor();

  //_terrainCPU = _core->createTerrainCPU(heightmapCPU, {_patchX, _patchY});
  auto heights = _terrainPhysics->getHeights();
  _terrainCPU = _core->createTerrainCPU(heights, _terrainPhysics->getResolution());
  _core->addDrawable(_terrainCPU);

  {
    auto fillMaterialColor = [core = _core](std::shared_ptr<MaterialColor> material) {
      if (material->getBaseColor().size() == 0) material->setBaseColor({core->getResourceManager()->getTextureOne()});
    };

    auto gltfModelSimple = _core->createModelGLTF("../../model/assets/BrainStem/BrainStem.gltf");
    _modelSimple = _core->createModel3D(gltfModelSimple);
    auto materialModelSimple = gltfModelSimple->getMaterialsColor();
    for (auto& material : materialModelSimple) {
      fillMaterialColor(material);
    }
    _modelSimple->setMaterial(materialModelSimple);

    auto aabb = _modelSimple->getAABB();
    auto min = aabb->getMin();
    auto max = aabb->getMax();
    auto center = (max + min) / 2.f;
    float part = 0.5f;
    // divide to 2.f because we want to have part size overall and not part for left and the same for right
    auto minPart = glm::vec3(center.x - part * (max - min).x / 2.f, min.y, min.z);
    auto maxPart = glm::vec3(center.x + part * (max - min).x / 2.f, max.y, max.z);
    {
      auto model = glm::translate(glm::mat4(1.f), glm::vec3(-4.f, -1.f, -3.f));
      _modelSimple->setModel(model);
      auto origin = glm::translate(glm::mat4(1.f), glm::vec3(0.f, -((max - min) / 2.f).y, 0.f));
      _modelSimple->setOrigin(origin);
    }
    _core->addDrawable(_modelSimple);

    _boundingBox = _core->createBoundingBox(minPart, maxPart);
    _boundingBox->setDrawType(DrawType::WIREFRAME);
    {
      auto model = glm::translate(glm::mat4(1.f), glm::vec3(-4.f, -1.f, -3.f));
      _boundingBox->setModel(model);
      auto origin = glm::translate(glm::mat4(1.f), glm::vec3(0.f, -((max - min) / 2.f).y, 0.f));
      _boundingBox->setOrigin(origin);
    }
    _core->addDrawable(_boundingBox);
    _model3DPhysics = std::make_shared<Model3DPhysics>(glm::vec3(-4.f, -1.f, -3.f), (maxPart - minPart) / 2.f,
                                                       _physicsManager);
  }

  _core->endRecording();

  _core->registerUpdate(std::bind(&Main::update, this));
  // can be lambda passed that calls reset
  _core->registerReset(std::bind(&Main::reset, this, std::placeholders::_1, std::placeholders::_2));
}

void Main::update() {
  static float i = 0;
  // update light position
  float radius = 15.f;
  static float angleHorizontal = 90.f;
  glm::vec3 lightPositionHorizontal = glm::vec3(radius * cos(glm::radians(angleHorizontal)), radius,
                                                radius * sin(glm::radians(angleHorizontal)));
  static float angleVertical = 0.f;
  glm::vec3 lightPositionVertical = glm::vec3(0.f, radius * sin(glm::radians(angleVertical)),
                                              radius * cos(glm::radians(angleVertical)));

  _pointLightVertical->setPosition(lightPositionVertical);
  {
    auto model = glm::translate(glm::mat4(1.f), lightPositionVertical);
    model = glm::scale(model, glm::vec3(0.3f, 0.3f, 0.3f));
    _cubeColoredLightVertical->setModel(model);
  }
  _pointLightHorizontal->setPosition(lightPositionHorizontal);
  {
    auto model = glm::translate(glm::mat4(1.f), lightPositionHorizontal);
    model = glm::scale(model, glm::vec3(0.3f, 0.3f, 0.3f));
    _cubeColoredLightHorizontal->setModel(model);
  }

  i += 0.1f;
  angleHorizontal += 0.05f;
  angleVertical += 0.1f;

  auto [FPSLimited, FPSReal] = _core->getFPS();
  auto [widthScreen, heightScreen] = _core->getState()->getSettings()->getResolution();
  _core->getGUI()->startWindow("Terrain", {20, 20}, {widthScreen / 10, heightScreen / 10});
  if (_core->getGUI()->startTree("Info")) {
    _core->getGUI()->drawText({"Limited FPS: " + std::to_string(FPSLimited)});
    _core->getGUI()->drawText({"Maximum FPS: " + std::to_string(FPSReal)});
    _core->getGUI()->drawText({"Press 'c' to turn cursor on/off"});
    _core->getGUI()->endTree();
  }
  auto eye = _camera->getEye();
  auto direction = _camera->getDirection();
  if (_core->getGUI()->startTree("Coordinates")) {
    _core->getGUI()->drawText({std::string("eye x: ") + std::format("{:.2f}", eye.x),
                               std::string("eye y: ") + std::format("{:.2f}", eye.y),
                               std::string("eye z: ") + std::format("{:.2f}", eye.z)});
    auto model = _cubePlayer->getModel();
    _core->getGUI()->drawText({std::string("player x: ") + std::format("{:.6f}", model[3][0]),
                               std::string("player y: ") + std::format("{:.6f}", model[3][1]),
                               std::string("player z: ") + std::format("{:.6f}", model[3][2])});
    _core->getGUI()->endTree();
  }
  if (_core->getGUI()->drawButton("Reset")) {
    _shape3DPhysics->setPosition(glm::vec3(0.f, 50.f, 0.f));
    _model3DPhysics->setPosition(glm::vec3(-4.f, -1.f, -3.f));
  }
  if (_core->getGUI()->startTree("Toggles")) {
    std::map<std::string, int*> patchesNumber{{"Patch x", &_patchX}, {"Patch y", &_patchY}};
    if (_core->getGUI()->drawInputInt(patchesNumber)) {
      _core->startRecording();
      _createTerrainColor();
      _core->endRecording();
    }

    std::map<std::string, int*> tesselationLevels{{"Tesselation min", &_minTessellationLevel},
                                                  {"Tesselation max", &_maxTessellationLevel}};
    if (_core->getGUI()->drawInputInt(tesselationLevels)) {
      _terrain->setTessellationLevel(_minTessellationLevel, _maxTessellationLevel);
    }

    if (_core->getGUI()->drawCheckbox({{"Patches", &_showPatches}})) {
      _terrain->patchEdge(_showPatches);
    }
    if (_core->getGUI()->drawCheckbox({{"LoD", &_showLoD}})) {
      _terrain->showLoD(_showLoD);
    }
    if (_core->getGUI()->drawCheckbox({{"Wireframe", &_showWireframe}})) {
      _terrain->setDrawType(DrawType::WIREFRAME);
      _terrainCPU->setDrawType(DrawType::WIREFRAME);
      _showNormals = false;
    }
    if (_core->getGUI()->drawCheckbox({{"Normal", &_showNormals}})) {
      _terrain->setDrawType(DrawType::NORMAL);
      _showWireframe = false;
    }
    if (_showWireframe == false && _showNormals == false) {
      _terrain->setDrawType(DrawType::FILL);
      _terrainCPU->setDrawType(DrawType::FILL);
    }
    if (_core->getGUI()->drawCheckbox({{"Show GPU", &_showGPU}})) {
      if (_showGPU == false) {
        _core->removeDrawable(_terrain);
      } else {
        _core->addDrawable(_terrain);
      }
    }
    if (_core->getGUI()->drawCheckbox({{"Show CPU", &_showCPU}})) {
      if (_showCPU == false) {
        _core->removeDrawable(_terrainCPU);
      } else {
        _core->addDrawable(_terrainCPU);
      }
    }
    _core->getGUI()->endTree();
  }
  _core->getGUI()->endWindow();

  if (_shift.has_value()) {
    _shape3DPhysics->setLinearVelocity(_shift.value());
  }
  _cubePlayer->setModel(_shape3DPhysics->getModel());
  _modelSimple->setModel(_model3DPhysics->getModel());
  _boundingBox->setModel(_model3DPhysics->getModel());

  // Step the world
  _physicsManager->step();
}

void Main::reset(int width, int height) { _camera->setAspect((float)width / (float)height); }

void Main::start() { _core->draw(); }

int main() {
  try {
    auto main = std::make_shared<Main>();
    main->start();
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}