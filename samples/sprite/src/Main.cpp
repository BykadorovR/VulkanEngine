#include <iostream>
#include <chrono>
#include <future>
#include "Main.h"
#include "Primitive/Sprite.h"

InputHandler::InputHandler(std::shared_ptr<Core> core) { _core = core; }

void InputHandler::cursorNotify(float xPos, float yPos) {}

void InputHandler::mouseNotify(int button, int action, int mods) {}

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

Main::Main() {
  int mipMapLevels = 4;
  auto settings = std::make_shared<Settings>();
  settings->setName("Sprite");
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
  settings->setAnisotropicSamples(0);
  settings->setDepthFormat(VK_FORMAT_D32_SFLOAT);
  settings->setMaxFramesInFlight(2);
  settings->setThreadsInPool(6);
  settings->setDesiredFPS(1000);
  settings->setAssetsPath("../assets/");
  settings->setShadersPath("../assets/");

  _core = std::make_shared<Core>(settings);
  _core->initialize();
  _gui = _core->createGUI();
  _core->createPostprocessing();

  _camera = std::make_shared<CameraFly>(_core->getEngineState());
  _camera->setSpeed(0.05f, 0.01f);
  _camera->setProjectionParameters(60.f, 0.1f, 100.f);
  _core->getEngineState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_camera));
  _inputHandler = std::make_shared<InputHandler>(_core);
  _core->getEngineState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_inputHandler));
  _core->setCamera(_camera);

  _pointLightVertical = _core->createPointLight();
  _pointLightVertical->setColor(glm::vec3(1.f, 1.f, 1.f));
  _pointLightHorizontal = _core->createPointLight();
  _pointLightHorizontal->setColor(glm::vec3(1.f, 1.f, 1.f));
  _directionalLight = _core->createDirectionalLight();
  _directionalLight->setColor(glm::vec3(1.f, 1.f, 1.f));
  _directionalLight->getCamera()->setPosition(glm::vec3(0.f, 20.f, 0.f));

  // cube colored light
  auto meshCube = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
  meshCube->setColor(std::vector{meshCube->getVertexData().size(), glm::vec3(1.f, 1.f, 1.f)},
                     _core->getCommandBufferApplication());
  _cubeColoredLightVertical = _core->createShape3D(ShapeType::CUBE, meshCube);
  _cubeColoredLightVertical->setScale(glm::vec3(0.3f, 0.3f, 0.3f));
  _core->addDrawable(_cubeColoredLightVertical);

  _cubeColoredLightHorizontal = _core->createShape3D(ShapeType::CUBE, meshCube);
  _cubeColoredLightHorizontal->setScale(glm::vec3(0.3f, 0.3f, 0.3f));
  _core->addDrawable(_cubeColoredLightHorizontal);

  auto cubeColoredLightDirectional = _core->createShape3D(ShapeType::CUBE, meshCube);
  cubeColoredLightDirectional->setScale(glm::vec3(0.3f, 0.3f, 0.3f));
  cubeColoredLightDirectional->setTranslate(glm::vec3(0.f, 20.f, 0.f));
  _core->addDrawable(cubeColoredLightDirectional);

  auto fillMaterialPhong = [core = _core](std::shared_ptr<MaterialPhong> material) {
    if (material->getBaseColor().size() == 0) material->setBaseColor({core->getResourceManager()->getTextureOne()});
    if (material->getNormal().size() == 0) material->setNormal({core->getResourceManager()->getTextureZero()});
    if (material->getSpecular().size() == 0) material->setSpecular({core->getResourceManager()->getTextureZero()});
  };

  auto fillMaterialPBR = [core = _core](std::shared_ptr<MaterialPBR> material) {
    if (material->getBaseColor().size() == 0) material->setBaseColor({core->getResourceManager()->getTextureOne()});
    if (material->getNormal().size() == 0) material->setNormal({core->getResourceManager()->getTextureZero()});
    if (material->getMetallic().size() == 0) material->setMetallic({core->getResourceManager()->getTextureZero()});
    if (material->getRoughness().size() == 0) material->setRoughness({core->getResourceManager()->getTextureZero()});
    if (material->getOccluded().size() == 0) material->setOccluded({core->getResourceManager()->getTextureZero()});
    if (material->getEmissive().size() == 0) material->setEmissive({core->getResourceManager()->getTextureZero()});
    material->setDiffuseIBL(core->getResourceManager()->getCubemapZero()->getTexture());
    material->setSpecularIBL(core->getResourceManager()->getCubemapZero()->getTexture(),
                             core->getResourceManager()->getTextureZero());
  };

  // draw textured Sprite, check alpha blending
  {
    auto texture = _core->createTexture("../assets/buterfly.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto sprite = _core->createSprite();
    auto materialColor = _core->createMaterialColor(MaterialTarget::SIMPLE);
    materialColor->setBaseColor({texture});
    sprite->setMaterial(materialColor);
    sprite->setTranslate(glm::vec3(2.f, -2.f, -2.5f));
    sprite->setScale(glm::vec3(1.f, 1.f, 1.f));
    _core->addDrawable(sprite);
  }
  // draw textured Sprite without lighting
  {
    auto textureTree = _core->createTexture("../assets/tree.png", settings->getLoadTextureAuxilaryFormat(),
                                            mipMapLevels);
    _spriteTree = _core->createSprite();
    _materialColor = _core->createMaterialColor(MaterialTarget::SIMPLE);
    _materialColor->setBaseColor({textureTree});
    _spriteTree->setMaterial(_materialColor);
    _spriteTree->setTranslate(glm::vec3(2.f, -2.f, -3.f));
    _spriteTree->setScale(glm::vec3(3.f, 3.f, 3.f));

    _core->addDrawable(_spriteTree);
  }

  // draw textured Sprite with Phong
  {
    auto textureTree = _core->createTexture("../assets/tree.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto spriteTree = _core->createSprite();
    _materialPhong = _core->createMaterialPhong(MaterialTarget::SIMPLE);
    _materialPhong->setBaseColor({textureTree});
    fillMaterialPhong(_materialPhong);
    spriteTree->setMaterial(_materialPhong);
    spriteTree->setTranslate(glm::vec3(-2.f, -2.f, -3.f));
    spriteTree->setScale(glm::vec3(3.f, 3.f, 3.f));
    _core->addDrawable(spriteTree);
  }
  // draw textured Sprite with PBR
  {
    auto textureTree = _core->createTexture("../assets/tree.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto spriteTree = _core->createSprite();
    _materialPBR = _core->createMaterialPBR(MaterialTarget::SIMPLE);
    _materialPBR->setBaseColor({textureTree});
    fillMaterialPBR(_materialPBR);
    spriteTree->setMaterial(_materialPBR);
    spriteTree->setTranslate(glm::vec3(6.f, -2.f, -3.f));
    spriteTree->setScale(glm::vec3(3.f, 3.f, 3.f));
    _core->addDrawable(spriteTree);
  }

  // draw textured Sprite, check alpha blending
  {
    auto texture = _core->createTexture("../assets/buterfly.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto sprite = _core->createSprite();
    auto materialColor = _core->createMaterialColor(MaterialTarget::SIMPLE);
    materialColor->setBaseColor({texture});
    sprite->setMaterial(materialColor);
    sprite->setDrawType(DrawType::WIREFRAME);
    sprite->setTranslate(glm::vec3(6.f, -2.f, -2.5f));
    sprite->setScale(glm::vec3(1.f, 1.f, 1.f));
    _core->addDrawable(sprite);
  }

  // draw textured Sprite normal
  {
    auto texture = _core->createTexture("../assets/buterfly.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto sprite = _core->createSprite();
    auto materialColor = _core->createMaterialColor(MaterialTarget::SIMPLE);
    materialColor->setBaseColor({texture});
    sprite->setMaterial(materialColor);
    sprite->setDrawType(DrawType::NORMAL);
    sprite->setScale(glm::vec3(1.f, 1.f, 1.f));
    sprite->setTranslate(glm::vec3(0.f, 0.5f, -2.5f));
    _core->addDrawable(sprite);
  }

  // draw textured Sprite tangent
  {
    auto texture = _core->createTexture("../assets/buterfly.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto sprite = _core->createSprite();
    auto materialColor = _core->createMaterialColor(MaterialTarget::SIMPLE);
    materialColor->setBaseColor({texture});
    sprite->setMaterial(materialColor);
    sprite->setDrawType(DrawType::TANGENT);
    sprite->setScale(glm::vec3(1.f, 1.f, 1.f));
    sprite->setTranslate(glm::vec3(0.f, 0.5f, -2.5f));
    _core->addDrawable(sprite);
  }

  // draw textured Sprite Phong without normals
  {
    auto textureColor = _core->createTexture("../assets/container.png", settings->getLoadTextureColorFormat(),
                                             mipMapLevels);
    auto textureSpecular = _core->createTexture("../assets/container_specular.png",
                                                settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto sprite = _core->createSprite();
    auto material = _core->createMaterialPhong(MaterialTarget::SIMPLE);
    material->setBaseColor({textureColor});
    material->setSpecular({textureSpecular});
    material->setCoefficients(glm::vec3{0.2f}, glm::vec3{0.2f}, glm::vec3{1.f}, 32.f);
    fillMaterialPhong(material);
    sprite->setMaterial(material);
    sprite->setScale(glm::vec3(1.f, 1.f, 1.f));
    sprite->setTranslate(glm::vec3(0.f, -4.f, -2.5f));
    _core->addDrawable(sprite);
  }

  // draw textured Sprite Phong without specular
  {
    auto textureColor = _core->createTexture("../assets/brickwall.jpg", settings->getLoadTextureColorFormat(),
                                             mipMapLevels);
    auto textureNormal = _core->createTexture("../assets/brickwall_normal.jpg",
                                              settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto sprite = _core->createSprite();
    auto material = _core->createMaterialPhong(MaterialTarget::SIMPLE);
    material->setBaseColor({textureColor});
    material->setNormal({textureNormal});
    fillMaterialPhong(material);
    sprite->setMaterial(material);
    sprite->setScale(glm::vec3(1.f, 1.f, 1.f));
    sprite->setTranslate(glm::vec3(2.f, -4.f, -2.5f));
    _core->addDrawable(sprite);
  }

  // draw textured Sprite PBR
  {
    auto textureColor = _core->createTexture("../assets/rustediron2_basecolor.png",
                                             settings->getLoadTextureColorFormat(), mipMapLevels);
    auto textureNormal = _core->createTexture("../assets/rustediron2_normal.png",
                                              settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto textureMetallic = _core->createTexture("../assets/rustediron2_metallic.png",
                                                settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto textureRoughness = _core->createTexture("../assets/rustediron2_roughness.png",
                                                 settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto sprite = _core->createSprite();
    auto material = _core->createMaterialPBR(MaterialTarget::SIMPLE);
    material->setBaseColor({textureColor});
    material->setNormal({textureNormal});
    material->setMetallic({textureMetallic});
    material->setRoughness({textureRoughness});
    fillMaterialPBR(material);
    sprite->setMaterial(material);
    sprite->setScale(glm::vec3(1.f, 1.f, 1.f));
    sprite->setTranslate(glm::vec3(4.f, -4.f, -2.5f));
    _core->addDrawable(sprite);
  }

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
  _gui->startWindow("Help");
  _gui->setWindowPosition({20, 20});
  _gui->drawText({"Limited FPS: " + std::to_string(FPSLimited)});
  _gui->drawText({"Maximum FPS: " + std::to_string(FPSReal)});
  _gui->drawText({"Press 'c' to turn cursor on/off"});

  std::map<std::string, int*> materialType;
  materialType["##Type"] = &_typeIndex;
  if (_gui->drawListBox({"Color", "Phong", "PBR"}, materialType, 3)) {
    switch (_typeIndex) {
      case 0:
        _spriteTree->setMaterial(_materialColor);
        break;
      case 1:
        _spriteTree->setMaterial(_materialPhong);
        break;
      case 2:
        _spriteTree->setMaterial(_materialPBR);
        break;
    }
  }
  _gui->endWindow();
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