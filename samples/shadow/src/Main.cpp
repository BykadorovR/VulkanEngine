#include <iostream>
#include <chrono>
#include <future>
#include "Main.h"
#include <random>
#include <glm/gtc/random.hpp>
#include "Primitive/Line.h"
#include "Primitive/Sprite.h"
#include "Primitive/Model.h"

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
  settings->setName("Shadow");
  settings->setClearColor({0.01f, 0.01f, 0.01f, 1.f});
  // TODO: fullscreen if resolution is {0, 0}
  // TODO: validation layers complain if resolution is {2560, 1600}
  settings->setResolution(std::tuple{1920, 1080});
  // settings->setDepthResolution(std::tuple{1024, 1024});
  //  for HDR, linear 16 bit per channel to represent values outside of 0-1 range (UNORM - float [0, 1], SFLOAT - float)
  //  https://registry.khronos.org/vulkan/specs/1.1/html/vkspec.html#_identification_of_formats
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

  auto commandBufferTransfer = _core->getCommandBufferApplication();
  _camera = std::make_shared<CameraFly>(_core->getEngineState());
  _camera->setProjectionParameters(60.f, 0.1f, 100.f);
  _core->getEngineState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_camera));
  _inputHandler = std::make_shared<InputHandler>(_core);
  _core->getEngineState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_inputHandler));
  _core->setCamera(_camera);

  _pointLightVertical = _core->createPointLight();
  _pointLightVertical->setColor(glm::vec3(_pointVerticalValue, _pointVerticalValue, _pointVerticalValue));
  _core->createPointShadow(_pointLightVertical);
  _pointLightHorizontal = _core->createPointLight();
  _pointLightHorizontal->setColor(glm::vec3(_pointHorizontalValue, _pointHorizontalValue, _pointHorizontalValue));
  _core->createPointShadow(_pointLightHorizontal);
  _directionalLight = _core->createDirectionalLight();
  _directionalLight->setColor(glm::vec3(_directionalValue, _directionalValue, _directionalValue));
  _directionalLight->getCamera()->setPosition(glm::vec3(0.f, 10.f, 0.f));
  _core->createDirectionalShadow(_directionalLight, true);

  {
    auto meshCube = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
    meshCube->setColor(std::vector{meshCube->getVertexData().size(), glm::vec3(1.f, 1.f, 1.f)}, commandBufferTransfer);
    _cubeColoredLightVertical = _core->createShape3D(ShapeType::CUBE, meshCube);
    _cubeColoredLightVertical->setScale(glm::vec3(0.3f, 0.3f, 0.3f));
    _core->addDrawable(_cubeColoredLightVertical);

    _cubeColoredLightHorizontal = _core->createShape3D(ShapeType::CUBE, meshCube);
    _cubeColoredLightHorizontal->setScale(glm::vec3(0.3f, 0.3f, 0.3f));
    _core->addDrawable(_cubeColoredLightHorizontal);

    auto cubeColoredLightDirectional = _core->createShape3D(ShapeType::CUBE, meshCube);
    cubeColoredLightDirectional->setScale(glm::vec3(0.3f, 0.3f, 0.3f));
    cubeColoredLightDirectional->setTranslate(_directionalLight->getCamera()->getPosition());
    _core->addDrawable(cubeColoredLightDirectional);
  }
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
  auto fillMaterialTerrainPhong = [core = _core](std::shared_ptr<MaterialPhong> material) {
    if (material->getBaseColor().size() == 0)
      material->setBaseColor(std::vector{4, core->getResourceManager()->getTextureOne()});
    if (material->getNormal().size() == 0)
      material->setNormal(std::vector{4, core->getResourceManager()->getTextureZero()});
    if (material->getSpecular().size() == 0)
      material->setSpecular(std::vector{4, core->getResourceManager()->getTextureZero()});
  };

  auto fillMaterialTerrainPBR = [core = _core](std::shared_ptr<MaterialPBR> material) {
    if (material->getBaseColor().size() == 0)
      material->setBaseColor(std::vector{4, core->getResourceManager()->getTextureOne()});
    if (material->getNormal().size() == 0)
      material->setNormal(std::vector{4, core->getResourceManager()->getTextureZero()});
    if (material->getMetallic().size() == 0)
      material->setMetallic(std::vector{4, core->getResourceManager()->getTextureZero()});
    if (material->getRoughness().size() == 0)
      material->setRoughness(std::vector{4, core->getResourceManager()->getTextureZero()});
    if (material->getOccluded().size() == 0)
      material->setOccluded(std::vector{4, core->getResourceManager()->getTextureZero()});
    if (material->getEmissive().size() == 0)
      material->setEmissive(std::vector{4, core->getResourceManager()->getTextureZero()});
    material->setDiffuseIBL(core->getResourceManager()->getCubemapZero()->getTexture());
    material->setSpecularIBL(core->getResourceManager()->getCubemapZero()->getTexture(),
                             core->getResourceManager()->getTextureZero());
  };
  {
    //  cube Phong
    auto cubemapColorPhong = _core->createCubemap(
        std::vector<std::string>{"../../shape/assets/brickwall.jpg", "../../shape/assets/brickwall.jpg",
                                 "../../shape/assets/brickwall.jpg", "../../shape/assets/brickwall.jpg",
                                 "../../shape/assets/brickwall.jpg", "../../shape/assets/brickwall.jpg"},
        settings->getLoadTextureColorFormat(), mipMapLevels);
    auto cubemapNormalPhong = _core->createCubemap(
        std::vector<std::string>{"../../shape/assets/brickwall_normal.jpg", "../../shape/assets/brickwall_normal.jpg",
                                 "../../shape/assets/brickwall_normal.jpg", "../../shape/assets/brickwall_normal.jpg",
                                 "../../shape/assets/brickwall_normal.jpg", "../../shape/assets/brickwall_normal.jpg"},
        settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto materialCubePhong = _core->createMaterialPhong(MaterialTarget::SIMPLE);
    materialCubePhong->setBaseColor({cubemapColorPhong->getTexture()});
    materialCubePhong->setNormal({cubemapNormalPhong->getTexture()});
    materialCubePhong->setSpecular({_core->getResourceManager()->getCubemapZero()->getTexture()});

    auto meshCube = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
    auto cubeTexturedPhong = _core->createShape3D(ShapeType::CUBE, meshCube);
    cubeTexturedPhong->setMaterial(materialCubePhong);
    cubeTexturedPhong->setTranslate(glm::vec3(0.f, -3.f, -3.f));
    _core->addDrawable(cubeTexturedPhong);
    _core->addShadowable(cubeTexturedPhong);
  }
  {
    // sphere colored
    auto meshSphere = std::make_shared<MeshSphere>(_core->getCommandBufferApplication(), _core->getEngineState());
    meshSphere->setColor(std::vector{meshSphere->getVertexData().size(), glm::vec3(0.f, 1.f, 0.f)},
                         commandBufferTransfer);
    auto sphereColored = _core->createShape3D(ShapeType::SPHERE, meshSphere);
    sphereColored->setTranslate(glm::vec3(0.f, 0.f, -5.f));
    _core->addDrawable(sphereColored);
    _core->addShadowable(sphereColored);
  }
  {
    auto gltfModelDancing = _core->createModelGLTF("../../model/assets/BrainStem/BrainStem.gltf");
    auto modelDancing = _core->createModel3D(gltfModelDancing);
    auto materialModelDancing = gltfModelDancing->getMaterialsPBR();
    for (auto& material : materialModelDancing) {
      fillMaterialPBR(material);
    }
    modelDancing->setMaterial(materialModelDancing);
    auto animationDancing = _core->createAnimation(gltfModelDancing);
    // set animation to model, so joints will be passed to shader
    modelDancing->setAnimation(animationDancing);
    modelDancing->setTranslate(glm::vec3(-5.f, -1.f, -3.f));
    modelDancing->setScale(glm::vec3(1.f, 1.f, 1.f));
    _core->addDrawable(modelDancing);
    _core->addShadowable(modelDancing);
  }
  {
    auto tile0Color = _core->createTexture("../../terrain/assets/desert/albedo.png",
                                           settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile1Color = _core->createTexture("../../terrain/assets/rock/albedo.png",
                                           settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile2Color = _core->createTexture("../../terrain/assets/grass/albedo.png",
                                           settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile3Color = _core->createTexture("../../terrain/assets/ground/albedo.png",
                                           settings->getLoadTextureColorFormat(), mipMapLevels);
    auto terrainPhong = _core->createTerrainInterpolation(_core->loadImageCPU("../../terrain/assets/heightmap.png"));
    terrainPhong->setPatchNumber(12, 12);
    terrainPhong->initialize(_core->getCommandBufferApplication());
    auto materialTerrainPhong = _core->createMaterialPhong(MaterialTarget::TERRAIN);
    materialTerrainPhong->setBaseColor({tile0Color, tile1Color, tile2Color, tile3Color});
    fillMaterialTerrainPhong(materialTerrainPhong);
    terrainPhong->setMaterial(materialTerrainPhong);
    terrainPhong->setScale(glm::vec3(0.1f, 0.1f, 0.1f));
    terrainPhong->setTranslate(glm::vec3(0.f, -7.f, 0.f));
    _core->addDrawable(terrainPhong, AlphaType::OPAQUE);
  }
  // draw textured Sprite Phong without specular
  {
    auto textureColor = _core->createTexture("../../sprite/assets/brickwall.jpg", settings->getLoadTextureColorFormat(),
                                             mipMapLevels);
    auto textureNormal = _core->createTexture("../../sprite/assets/brickwall_normal.jpg",
                                              settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto sprite = _core->createSprite();
    auto material = _core->createMaterialPhong(MaterialTarget::SIMPLE);
    material->setBaseColor({textureColor});
    material->setNormal({textureNormal});
    fillMaterialPhong(material);
    sprite->setMaterial(material);
    sprite->setTranslate(glm::vec3(3.f, 0.f, -3.f));
    sprite->setRotate(glm::vec3(glm::radians(-90.f), 0.f, 0.f));
    sprite->setScale(glm::vec3(1.f, 1.f, 1.f));
    _core->addDrawable(sprite);
    _core->addShadowable(sprite);
  }

  {
    auto tile0Color = _core->createTexture("../../terrain/assets/desert/albedo.png",
                                           settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile0Normal = _core->createTexture("../../terrain/assets/desert/normal.png",
                                            settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile0Metallic = _core->createTexture("../../terrain/assets/desert/metallic.png",
                                              settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile0Roughness = _core->createTexture("../../terrain/assets/desert/roughness.png",
                                               settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile0AO = _core->createTexture("../../terrain/assets/desert/ao.png", settings->getLoadTextureAuxilaryFormat(),
                                        mipMapLevels);

    auto tile1Color = _core->createTexture("../../terrain/assets/rock/albedo.png",
                                           settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile1Normal = _core->createTexture("../../terrain/assets/rock/normal.png",
                                            settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile1Metallic = _core->createTexture("../../terrain/assets/rock/metallic.png",
                                              settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile1Roughness = _core->createTexture("../../terrain/assets/rock/roughness.png",
                                               settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile1AO = _core->createTexture("../../terrain/assets/rock/ao.png", settings->getLoadTextureAuxilaryFormat(),
                                        mipMapLevels);

    auto tile2Color = _core->createTexture("../../terrain/assets/grass/albedo.png",
                                           settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile2Normal = _core->createTexture("../../terrain/assets/grass/normal.png",
                                            settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile2Metallic = _core->createTexture("../../terrain/assets/grass/metallic.png",
                                              settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile2Roughness = _core->createTexture("../../terrain/assets/grass/roughness.png",
                                               settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile2AO = _core->createTexture("../../terrain/assets/grass/ao.png", settings->getLoadTextureAuxilaryFormat(),
                                        mipMapLevels);

    auto tile3Color = _core->createTexture("../../terrain/assets/ground/albedo.png",
                                           settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile3Normal = _core->createTexture("../../terrain/assets/ground/normal.png",
                                            settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile3Metallic = _core->createTexture("../../terrain/assets/ground/metallic.png",
                                              settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile3Roughness = _core->createTexture("../../terrain/assets/ground/roughness.png",
                                               settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile3AO = _core->createTexture("../../terrain/assets/ground/ao.png", settings->getLoadTextureAuxilaryFormat(),
                                        mipMapLevels);

    auto terrainPBR = _core->createTerrainInterpolation(_core->loadImageCPU("../../terrain/assets/heightmap.png"));
    terrainPBR->setPatchNumber(12, 12);
    terrainPBR->initialize(_core->getCommandBufferApplication());
    auto materialPBR = _core->createMaterialPBR(MaterialTarget::TERRAIN);
    materialPBR->setBaseColor({tile0Color, tile1Color, tile2Color, tile3Color});
    materialPBR->setNormal({tile0Normal, tile1Normal, tile2Normal, tile3Normal});
    materialPBR->setMetallic({tile0Metallic, tile1Metallic, tile2Metallic, tile3Metallic});
    materialPBR->setRoughness({tile0Roughness, tile1Roughness, tile2Roughness, tile3Roughness});
    materialPBR->setOccluded({tile0AO, tile1AO, tile2AO, tile3AO});
    fillMaterialTerrainPBR(materialPBR);
    terrainPBR->setMaterial(materialPBR);
    terrainPBR->setTranslate(glm::vec3(3.f, -2.f, 3.f));
    terrainPBR->setScale(glm::vec3(0.01f, 0.01f, 0.01f));
    _core->addDrawable(terrainPBR);
    _core->addShadowable(terrainPBR);
  }

  // draw skeletal textured model with multiple animations
  auto gltfModelFish = _core->createModelGLTF("../../model/assets/Fish/scene.gltf");
  {
    auto modelFish = _core->createModel3D(gltfModelFish);
    auto materialModelFish = gltfModelFish->getMaterialsPhong();
    for (auto& material : materialModelFish) {
      fillMaterialPhong(material);
    }
    modelFish->setMaterial(materialModelFish);
    auto animationFish = _core->createAnimation(gltfModelFish);
    animationFish->setAnimation("swim");
    // set animation to model, so joints will be passed to shader
    modelFish->setAnimation(animationFish);
    modelFish->setTranslate(glm::vec3(3.f, 3.f, 3.f));
    modelFish->setScale(glm::vec3(5.f, 5.f, 5.f));
    _core->addDrawable(modelFish);
    _core->addShadowable(modelFish);
  }

  // draw textured Sprite with PBR
  {
    auto textureTree = _core->createTexture("../../sprite/assets/tree.png", settings->getLoadTextureColorFormat(),
                                            mipMapLevels);
    auto spriteTree = _core->createSprite();
    auto materialPBR = _core->createMaterialPBR(MaterialTarget::SIMPLE);
    materialPBR->setBaseColor({textureTree});
    fillMaterialPBR(materialPBR);
    spriteTree->setMaterial(materialPBR);
    spriteTree->setTranslate(glm::vec3(-3.f, -5.f, -5.f));
    spriteTree->setScale(glm::vec3(1.f, 1.f, 1.f));
    _core->addDrawable(spriteTree);
    _core->addShadowable(spriteTree);
  }

  // draw textured Sprite with PBR horizontal
  {
    auto textureTree = _core->createTexture("../../sprite/assets/tree.png", settings->getLoadTextureColorFormat(),
                                            mipMapLevels);
    auto spriteTree = _core->createSprite();
    auto materialPBR = _core->createMaterialPBR(MaterialTarget::SIMPLE);
    materialPBR->setBaseColor({textureTree});
    fillMaterialPBR(materialPBR);
    spriteTree->setMaterial(materialPBR);
    spriteTree->setTranslate(glm::vec3(3.f, 0.f, -6.f));
    spriteTree->setRotate(glm::vec3(glm::radians(-90.f), 0.f, 0.f));
    spriteTree->setScale(glm::vec3(1.f, 1.f, 1.f));
    _core->addDrawable(spriteTree);
    _core->addShadowable(spriteTree);
  }

  _core->registerUpdate(std::bind(&Main::update, this));
  // can be lambda passed that calls reset
  _core->registerReset(std::bind(&Main::reset, this, std::placeholders::_1, std::placeholders::_2));
}

void Main::update() {
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
  angleHorizontal += 0.05f;
  angleVertical += 0.1f;

  auto [FPSLimited, FPSReal] = _core->getFPS();
  auto [widthScreen, heightScreen] = _core->getEngineState()->getSettings()->getResolution();
  _gui->startWindow("Help");
  _gui->setWindowPosition({20, 20});
  _gui->drawText({"Limited FPS: " + std::to_string(FPSLimited)});
  _gui->drawText({"Maximum FPS: " + std::to_string(FPSReal)});
  if (_gui->drawSlider(
          {{"Directional", &_directionalValue},
           {"Point horizontal", &_pointHorizontalValue},
           {"Point vertical", &_pointVerticalValue}},
          {{"Directional", {0.f, 20.f}}, {"Point horizontal", {0.f, 20.f}}, {"Point vertical", {0.f, 20.f}}})) {
    _directionalLight->setColor(glm::vec3(_directionalValue, _directionalValue, _directionalValue));
    _pointLightHorizontal->setColor(glm::vec3(_pointHorizontalValue, _pointHorizontalValue, _pointHorizontalValue));
    _pointLightVertical->setColor(glm::vec3(_pointVerticalValue, _pointVerticalValue, _pointVerticalValue));
  }
  _gui->drawText({"Press 'c' to turn cursor on/off"});
  auto eye = _camera->getEye();
  auto direction = _camera->getDirection();
  if (_gui->startTree("Coordinates")) {
    _gui->drawText({std::string("eye x: ") + std::format("{:.2f}", eye.x),
                    std::string("eye y: ") + std::format("{:.2f}", eye.y),
                    std::string("eye z: ") + std::format("{:.2f}", eye.z)});
    _gui->endTree();
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