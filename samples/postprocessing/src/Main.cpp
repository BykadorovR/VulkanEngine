#include <iostream>
#include <chrono>
#include <future>
#include "Main.h"
#include "Primitive/Line.h"
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
  settings->setName("Postprocessing");
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
  _camera->setProjectionParameters(60.f, 0.1f, 100.f);
  _camera->setSpeed(0.05f, 0.01f);
  _core->getEngineState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_camera));
  _inputHandler = std::make_shared<InputHandler>(_core);
  _core->getEngineState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_inputHandler));
  _core->setCamera(_camera);

  _pointLightVertical = _core->createPointLight();
  _pointLightVertical->setColor(glm::vec3(_pointVerticalValue, _pointVerticalValue, _pointVerticalValue));
  _pointLightHorizontal = _core->createPointLight();
  _pointLightHorizontal->setColor(glm::vec3(_pointHorizontalValue, _pointHorizontalValue, _pointHorizontalValue));
  _directionalLight = _core->createDirectionalLight();
  _directionalLight->setColor(glm::vec3(_directionalValue, _directionalValue, _directionalValue));
  _directionalLight->getCamera()->setPosition(glm::vec3(0.f, 20.f, 0.f));

  {
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
  }

  auto fillMaterialPhong = [core = _core](std::shared_ptr<MaterialPhong> material) {
    if (material->getBaseColor().size() == 0)
      material->setBaseColor(std::vector{4, core->getResourceManager()->getTextureOne()});
    if (material->getNormal().size() == 0)
      material->setNormal(std::vector{4, core->getResourceManager()->getTextureZero()});
    if (material->getSpecular().size() == 0)
      material->setSpecular(std::vector{4, core->getResourceManager()->getTextureZero()});
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

  {
    // cube colored
    auto meshCube = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
    meshCube->setColor(std::vector{meshCube->getVertexData().size(), glm::vec3(1.f, 0.f, 0.f)},
                       _core->getCommandBufferApplication());
    auto cubeColored = _core->createShape3D(ShapeType::CUBE, meshCube);
    cubeColored->setTranslate(glm::vec3(0.f, 3.f, -3.f));

    _core->addDrawable(cubeColored);
  }
  // TODO: color is not so bright in comparison with cube
  auto sphereColorPBR = _core->createTexture("../../shape/assets/rustediron2_basecolor.png",
                                             settings->getLoadTextureColorFormat(), mipMapLevels);
  auto sphereNormalPBR = _core->createTexture("../../shape/assets/rustediron2_normal.png",
                                              settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
  auto sphereMetallicPBR = _core->createTexture("../../shape/assets/rustediron2_metallic.png",
                                                settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
  auto sphereRoughnessPBR = _core->createTexture("../../shape/assets/rustediron2_roughness.png",
                                                 settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
  auto materialSpherePBR = _core->createMaterialPBR(MaterialTarget::SIMPLE);
  // material can't have default state because it can be either cubemap or texture2D
  materialSpherePBR->setBaseColor({sphereColorPBR});
  materialSpherePBR->setNormal({sphereNormalPBR});
  materialSpherePBR->setMetallic({sphereMetallicPBR});
  materialSpherePBR->setRoughness({sphereRoughnessPBR});
  materialSpherePBR->setEmissive({_core->getResourceManager()->getTextureZero()});
  materialSpherePBR->setOccluded({_core->getResourceManager()->getTextureZero()});
  materialSpherePBR->setDiffuseIBL(_core->getResourceManager()->getCubemapZero()->getTexture());
  materialSpherePBR->setSpecularIBL(_core->getResourceManager()->getCubemapZero()->getTexture(),
                                    _core->getResourceManager()->getTextureZero());
  {
    // sphere PBR
    auto meshSphere = std::make_shared<MeshSphere>(_core->getCommandBufferApplication(), _core->getEngineState());
    auto sphereTexturedPBR = _core->createShape3D(ShapeType::SPHERE, meshSphere);
    sphereTexturedPBR->setMaterial(materialSpherePBR);
    sphereTexturedPBR->setTranslate(glm::vec3(-3.f, 3.f, -3.f));
    _core->addDrawable(sphereTexturedPBR);
  }
  // draw skeletal dancing model with one animation
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
    modelDancing->setTranslate(glm::vec3(-4.f, -1.f, -3.f));
    modelDancing->setScale(glm::vec3(1.f, 1.f, 1.f));
    _core->addDrawable(modelDancing);
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
    auto terrainPhong = _core->createTerrainComposition(_core->loadImageCPU("../../terrain/assets/heightmap.png"));
    terrainPhong->setPatchNumber(12, 12);
    terrainPhong->initialize(_core->getCommandBufferApplication());
    auto materialPhong = _core->createMaterialPhong(MaterialTarget::TERRAIN);
    materialPhong->setBaseColor({tile0Color, tile1Color, tile2Color, tile3Color});
    fillMaterialPhong(materialPhong);
    terrainPhong->setMaterial(materialPhong);
    terrainPhong->setTranslate(glm::vec3(0.f, -5.f, 0.f));
    terrainPhong->setScale(glm::vec3(0.3f, 0.3f, 0.3f));

    _core->addDrawable(terrainPhong);
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
  _core->getGUI()->startWindow("Help");
  _core->getGUI()->setWindowPosition({20, 20});
  _core->getGUI()->drawText({"Limited FPS: " + std::to_string(FPSLimited)});
  _core->getGUI()->drawText({"Maximum FPS: " + std::to_string(FPSReal)});
  if (_core->getGUI()->drawSlider(
          {{"Directional", &_directionalValue},
           {"Point horizontal", &_pointHorizontalValue},
           {"Point vertical", &_pointVerticalValue}},
          {{"Directional", {0.f, 20.f}}, {"Point horizontal", {0.f, 20.f}}, {"Point vertical", {0.f, 20.f}}})) {
    _directionalLight->setColor(glm::vec3(_directionalValue, _directionalValue, _directionalValue));
    _pointLightHorizontal->setColor(glm::vec3(_pointHorizontalValue, _pointHorizontalValue, _pointHorizontalValue));
    _pointLightVertical->setColor(glm::vec3(_pointVerticalValue, _pointVerticalValue, _pointVerticalValue));
  }
  float gamma = _core->getPostprocessing()->getGamma();
  if (_core->getGUI()->drawInputFloat({{"gamma", &gamma}})) _core->getPostprocessing()->setGamma(gamma);
  float exposure = _core->getPostprocessing()->getExposure();
  if (_core->getGUI()->drawInputFloat({{"exposure", &exposure}})) _core->getPostprocessing()->setExposure(exposure);
  _core->getGUI()->drawText({"Press 'c' to turn cursor on/off"});
  _core->getGUI()->endWindow();
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