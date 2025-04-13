#include <iostream>
#include <chrono>
#include <future>
#include "Main.h"
#include <glm/gtx/matrix_decompose.hpp>
#include "Utility/PhysicsManager.h"
#include <nlohmann/json.hpp>
#include "glm/gtx/vector_angle.hpp"

void InputHandler::setMoveCallback(std::function<void(glm::vec2)> callback) { _callbackMove = callback; }

InputHandler::InputHandler(std::shared_ptr<Core> core) { _core = core; }

void InputHandler::cursorNotify(float xPos, float yPos) { _position = glm::vec2{xPos, yPos}; }

void InputHandler::mouseNotify(int button, int action, int mods) {
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    _callbackMove(_position);
  }
}

void InputHandler::keyNotify(int key, int scancode, int action, int mods) {
#ifndef __ANDROID__
  if ((action == GLFW_RELEASE && key == GLFW_KEY_C)) {
    if (_core->getEngineState()->getInput()->cursorEnabled()) {
      _core->getEngineState()->getInput()->showCursor(false);
    } else {
      _core->getEngineState()->getInput()->showCursor(true);
    }
  }
#endif
}

void InputHandler::charNotify(unsigned int code) {}

void InputHandler::scrollNotify(double xOffset, double yOffset) {}

void Main::_loadTerrain(std::string path) {
  std::ifstream file(path);
  nlohmann::json inputJSON;
  file >> inputJSON;
  if (inputJSON["stripe"].is_null() == false) {
    _stripeLeft = inputJSON["stripe"][0];
    _stripeRight = inputJSON["stripe"][1];
    _stripeTop = inputJSON["stripe"][2];
    _stripeBot = inputJSON["stripe"][3];
  }

  _patchX = inputJSON["patches"][0];
  _patchY = inputJSON["patches"][1];
  _patchRotationsIndex.resize(_patchX * _patchY);
  for (int i = 0; i < inputJSON["rotation"].size(); i++) {
    _patchRotationsIndex[i] = inputJSON["rotation"][i];
  }
  _patchTextures.resize(_patchX * _patchY);
  for (int i = 0; i < inputJSON["textures"].size(); i++) {
    _patchTextures[i] = inputJSON["textures"][i];
  }
}

void Main::_createTerrainColor() {
  _core->removeDrawable(_terrain);
  //_loadTerrain("../assets/test.json");
  _terrain = _core->createTerrainComposition(_core->loadImageCPU("../assets/heightmap.png"));
  _terrain->setPatchNumber(_patchX, _patchY);
  _terrain->setPatchRotations(_patchRotationsIndex);
  _terrain->setPatchTextures(_patchTextures);
  _terrain->initialize(_core->getCommandBufferApplication());
  _terrain->setMaterial(_materialColor);

  _terrain->setTessellationLevel(_minTessellationLevel, _maxTessellationLevel);
  _terrain->setTesselationDistance(_minDistance, _maxDistance);
  _terrain->setColorHeightLevels(_heightLevels);
  _terrain->setHeight(_heightScale, _heightShift);
  _terrain->setTranslate(_terrainPosition);
  _terrain->setScale(_terrainScale);
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
  settings->setAssetsPath("../assets/");
  settings->setShadersPath("../assets/");

  _core = std::make_shared<Core>(settings);
  _core->initialize();
  _gui = _core->createGUI();
  _core->createPostprocessing();

  _inputHandler = std::make_shared<InputHandler>(_core);
  _core->getEngineState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_inputHandler));

  _pointLightVertical = _core->createPointLight();
  _pointLightVertical->setColor(glm::vec3(1.f, 1.f, 1.f));
  _pointLightHorizontal = _core->createPointLight();
  _pointLightHorizontal->setColor(glm::vec3(1.f, 1.f, 1.f));
  _directionalLight = _core->createDirectionalLight();
  _directionalLight->setColor(glm::vec3(1.f, 1.f, 1.f));
  _directionalLight->getCamera()->setPosition(glm::vec3(0.f, 20.f, 0.f));

  auto ambientLight = _core->createAmbientLight();
  ambientLight->setColor({0.1f, 0.1f, 0.1f});

  {
    auto meshCube = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
    meshCube->setColor(std::vector{meshCube->getVertexData().size(), glm::vec3(1.f, 1.f, 1.f)},
                       _core->getCommandBufferApplication());
    _cubeColoredLightVertical = _core->createShape3D(ShapeType::CUBE, meshCube);
    _core->addDrawable(_cubeColoredLightVertical);

    _cubeColoredLightHorizontal = _core->createShape3D(ShapeType::CUBE, meshCube);
    _core->addDrawable(_cubeColoredLightHorizontal);

    auto cubeColoredLightDirectional = _core->createShape3D(ShapeType::CUBE, meshCube);
    cubeColoredLightDirectional->setTranslate(glm::vec3(0.f, 20.f, 0.f));
    cubeColoredLightDirectional->setScale(glm::vec3(0.3f, 0.3f, 0.3f));
    _core->addDrawable(cubeColoredLightDirectional);
  }
  auto heightmapCPU = _core->loadImageCPU("../assets/heightmap.png");

  _physicsManager = _core->createPhysicsManager();
  _physicsManager->setDeltaTime(1.f / static_cast<float>(settings->getDesiredFPS()));
  _terrainPhysics = std::make_shared<TerrainPhysics>(heightmapCPU, _terrainPosition, _terrainScale, std::tuple{64, 16},
                                                     _physicsManager, _core->getGameState(), _core->getEngineState());
  _terrainPhysics->setFriction(0.5f);

  _shape3DPhysics = std::make_shared<Shape3DPhysics>(glm::vec3(0.f, 50.f, 0.f), glm::vec3(0.5f, 0.5f, 0.5f),
                                                     _physicsManager);
  {
    auto meshCube = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
    meshCube->setColor(std::vector{meshCube->getVertexData().size(), glm::vec3(0.f, 0.f, 1.f)},
                       _core->getCommandBufferApplication());
    _cubePlayer = _core->createShape3D(ShapeType::CUBE, meshCube);
    _cubePlayer->setTranslate(glm::vec3(-4.f, -14.f, -10.f));
    _core->addDrawable(_cubePlayer);
  }
  auto callbackPosition = [&](glm::vec2 click) { _endPoint = _terrainPhysics->getHit(click); };
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
  _terrainCPU->setTranslate(_terrainPosition);
  _terrainCPU->setScale(_terrainScale);
  _terrainCPU->setDrawType(DrawType::WIREFRAME);

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

    auto aabb = _modelSimple->getAABBPositions();
    auto min = aabb->getMin();
    auto max = aabb->getMax();
    // divide to 2.f because Jolt accept half of the box not entire one
    auto minBB = glm::scale(glm::mat4(1.f), glm::vec3(_boundingBoxScale, _modelScale, _modelScale)) *
                 glm::vec4(min.y, min.y, min.z, 1.f);
    auto maxBB = glm::scale(glm::mat4(1.f), glm::vec3(_boundingBoxScale, _modelScale, _modelScale)) *
                 glm::vec4(max.x, max.y, max.z, 1.f);
    _modelSimple->setTranslate(glm::vec3(-4.f, -1.f, -3.f));
    _modelSimple->setScale(glm::vec3(_modelScale, _modelScale, _modelScale));
    // model itself is buggy and it's origin between the foots
    _modelSimple->setOriginShift(glm::vec3(0.f, -((max - min) / 2.f).y, 0.f));
    _core->addDrawable(_modelSimple);

    auto meshCube = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
    _boundingBox = _core->createShape3D(ShapeType::CUBE, meshCube);
    _boundingBox->setDrawType(DrawType::WIREFRAME);
    _boundingBox->setScale(maxBB - minBB);
    _core->addDrawable(_boundingBox);
    // height of middle part of capsule + radius
    _model3DPhysics = std::make_shared<Model3DPhysics>(
        glm::vec3(-4.f, 14.f, -10.f), (maxBB - minBB).y - (maxBB - minBB).z, (maxBB - minBB).z / 2.f, _physicsManager);
    _model3DPhysics->setFriction(0.5f);
    _model3DPhysics->setRotationSpeed(3.f);
    _model3DPhysics->setMovementSpeed(_speed);

    auto meshCapsule = std::make_shared<MeshCapsuleStatic>(
        (maxBB - minBB).y - (maxBB - minBB).z, (maxBB - minBB).z / 2.f, _core->getCommandBufferApplication(),
        _core->getEngineState());
    meshCapsule->setColor(std::vector{meshCapsule->getVertexData().size(), glm::vec3(1.f, 0.f, 0.f)},
                          _core->getCommandBufferApplication());
    _capsule = _core->createShape3D(ShapeType::CAPSULE, meshCapsule);
    _capsule->setTranslate(glm::vec3(0.f, 0.f, 0.f));
    _capsule->setDrawType(DrawType::WIREFRAME);
    _core->addDrawable(_capsule);
  }

  _cameraFly = std::make_shared<CameraFly>(_core->getEngineState());
  _cameraFly->setSpeed(0.05f, 0.5f);
  _cameraFly->setProjectionParameters(60.f, 0.1f, 100.f);
  _core->getEngineState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_cameraFly));
  _cameraRTS = std::make_shared<CameraRTS>(_modelSimple, _core->getEngineState());
  _cameraRTS->setProjectionParameters(60.f, 0.1f, 100.f);
  _cameraRTS->setShift(glm::vec3(0.f, 10.f, 4.f));
  _core->getEngineState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_cameraRTS));
  _core->setCamera(_cameraRTS);

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

  _pointLightVertical->getCamera()->setPosition(lightPositionVertical);
  _cubeColoredLightVertical->setTranslate(lightPositionVertical);
  _pointLightHorizontal->getCamera()->setPosition(lightPositionHorizontal);
  _cubeColoredLightHorizontal->setTranslate(lightPositionHorizontal);

  i += 0.1f;
  angleHorizontal += 0.05f;
  angleVertical += 0.1f;

  auto [FPSLimited, FPSReal] = _core->getFPS();
  auto [widthScreen, heightScreen] = _core->getEngineState()->getSettings()->getResolution();
  _core->getGUI()->startWindow("Terrain");
  _core->getGUI()->setWindowPosition({20, 20});
  if (_core->getGUI()->startTree("Info")) {
    _core->getGUI()->drawText({"Limited FPS: " + std::to_string(FPSLimited)});
    _core->getGUI()->drawText({"Maximum FPS: " + std::to_string(FPSReal)});
    _core->getGUI()->drawText({"Press 'c' to turn cursor on/off"});
    _core->getGUI()->endTree();
  }
  auto currentCamera = _core->getCamera();
  auto eye = currentCamera->getEye();
  auto direction = _model3DPhysics->getLinearVelocity();
  auto up = currentCamera->getUp();
  if (_core->getGUI()->startTree("Coordinates")) {
    _core->getGUI()->drawText({std::string("eye x: ") + std::format("{:.2f}", eye.x),
                               std::string("eye y: ") + std::format("{:.2f}", eye.y),
                               std::string("eye z: ") + std::format("{:.2f}", eye.z)});
    auto model = _model3DPhysics->getTranslate();
    _core->getGUI()->drawText({std::string("player x: ") + std::format("{:.6f}", model.x),
                               std::string("player y: ") + std::format("{:.6f}", model.y),
                               std::string("player z: ") + std::format("{:.6f}", model.z)});
    _core->getGUI()->drawText({std::string("direction x: ") + std::format("{:.2f}", direction.x),
                               std::string("direction y: ") + std::format("{:.2f}", direction.y),
                               std::string("direction z: ") + std::format("{:.2f}", direction.z)});
    auto normal = _model3DPhysics->getGroundNormal();
    _core->getGUI()->drawText({std::string("normal x: ") + std::format("{:.6f}", normal.x),
                               std::string("normal y: ") + std::format("{:.6f}", normal.y),
                               std::string("normal z: ") + std::format("{:.6f}", normal.z)});
    _core->getGUI()->drawText({std::string("angle: ") + std::format("{:.2f}", glm::degrees(_angle))});
    _core->getGUI()->endTree();
  }
  if (_core->getGUI()->startTree("Camera")) {
    std::map<std::string, int*> cameraList;
    cameraList["##Camera"] = &_cameraIndex;
    if (_core->getGUI()->drawListBox({"RTS", "Fly"}, cameraList, 2)) {
      switch (_cameraIndex) {
        case 0:
          _cameraRTS->setViewParameters(currentCamera->getEye(), _cameraRTS->getDirection(), _cameraRTS->getUp());
          _core->setCamera(_cameraRTS);
          break;
        case 1:
          _cameraFly->setViewParameters(currentCamera->getEye(), currentCamera->getDirection(), _cameraFly->getUp());
          _core->setCamera(_cameraFly);
          break;
      };
    }
    _core->getGUI()->endTree();
  }
  if (_core->getGUI()->drawButton("Reset")) {
    _shape3DPhysics->setTranslate(glm::vec3(0.f, 50.f, 0.f));
    _model3DPhysics->setTranslate(glm::vec3(-4.f, 14.f, -10.f));
  }
  if (_core->getGUI()->startTree("Toggles")) {
    std::map<std::string, int*> patchesNumber{{"Patch x", &_patchX}, {"Patch y", &_patchY}};
    if (_core->getGUI()->drawInputInt(patchesNumber)) {
      _createTerrainColor();
    }

    std::map<std::string, int*> tesselationLevels{{"Tesselation min", &_minTessellationLevel},
                                                  {"Tesselation max", &_maxTessellationLevel}};
    if (_core->getGUI()->drawInputInt(tesselationLevels)) {
      _terrain->setTessellationLevel(_minTessellationLevel, _maxTessellationLevel);
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

  if (_endPoint) {
    if (_model3DPhysics->moveTo(_endPoint.value(), 0.1f) == false) _endPoint.reset();
  }

  _cubePlayer->setTranslate(_shape3DPhysics->getTranslate());
  _cubePlayer->setRotate(_shape3DPhysics->getRotate());

  _capsule->setTranslate(_model3DPhysics->getTranslate());
  _capsule->setRotate(_model3DPhysics->getRotate());
  _boundingBox->setTranslate(_model3DPhysics->getTranslate());
  _boundingBox->setRotate(_model3DPhysics->getRotate());

  _modelSimple->setTranslate(_model3DPhysics->getTranslate());
  _modelSimple->setRotate(_model3DPhysics->getRotate());

  // Step the world
  _physicsManager->update();
  _model3DPhysics->postUpdate();
}

void Main::reset(int width, int height) {
  _cameraRTS->setAspect((float)width / (float)height);
  _cameraFly->setAspect((float)width / (float)height);
}

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