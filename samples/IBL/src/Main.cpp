#include <iostream>
#include <chrono>
#include <future>
#include "Main.h"
#include <random>
#include <glm/gtc/random.hpp>
#include "Primitive/Model.h"
#include "Primitive/Equirectangular.h"
#include "Graphic/IBL.h"

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
  // for HDR, linear 16 bit per channel to represent values outside of 0-1 range (UNORM - float [0, 1], SFLOAT - float)
  // https://registry.khronos.org/vulkan/specs/1.1/html/vkspec.html#_identification_of_formats
  settings->setGraphicColorFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
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
  _equirectangular = _core->createEquirectangular("../assets/newport_loft.hdr");
  auto cubemapConverted = _equirectangular->getCubemap();
  auto materialSkybox = _core->createMaterialColor(MaterialTarget::SIMPLE);
  materialSkybox->setBaseColor({cubemapConverted->getTexture()});
  auto skybox = _core->createSkybox();
  skybox->setMaterial(materialSkybox);
  _core->addSkybox(skybox);

  _ibl = _core->createIBL();
  _ibl->setMaterial(materialSkybox);
  _ibl->drawDiffuse();
  _ibl->drawSpecular();
  _ibl->drawSpecularBRDF();

  {
    auto modelGLTF = _core->createModelGLTF("../assets/DamagedHelmet/DamagedHelmet.gltf");
    auto modelPBR = _core->createModel3D(modelGLTF);
    auto materialDamagedHelmet = modelGLTF->getMaterialsPBR();
    for (auto& material : materialDamagedHelmet) {
      material->setSpecularIBL(_ibl->getCubemapSpecular()->getTexture(), _ibl->getTextureSpecularBRDF());
      material->setDiffuseIBL(_ibl->getCubemapDiffuse()->getTexture());
    }
    modelPBR->setMaterial(materialDamagedHelmet);
    modelPBR->setTranslate(glm::vec3(-2.f, 0.f, -3.f));
    modelPBR->setScale(glm::vec3(1.f, 1.f, 1.f));
    _core->addDrawable(modelPBR);
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