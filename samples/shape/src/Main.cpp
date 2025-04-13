#include <iostream>
#include <chrono>
#include <future>
#include "Main.h"
#include "Primitive/Line.h"

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
  settings->setName("Shape");
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
  _core->createGUI();
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

  auto meshCubePlain = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
  // cube colored light
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
  // lines
  auto lineVertical = _core->createLine();
  lineVertical->getMesh()->setColor({glm::vec3(1.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f)});
  lineVertical->getMesh()->setPosition({glm::vec3(-3.f, -0.5f, -3.f), glm::vec3(-3.f, 0.5f, -3.f)});
  _core->addDrawable(lineVertical);

  auto lineHorizontal = _core->createLine();
  lineHorizontal->getMesh()->setColor({glm::vec3(0.f, 0.f, 1.f), glm::vec3(1.f, 0.f, 0.f)});
  lineHorizontal->getMesh()->setPosition({glm::vec3(-2.5f, 0.f, -3.f), glm::vec3(-3.5f, 0.f, -3.f)});
  _core->addDrawable(lineHorizontal);

  // cube colored
  {
    auto meshCube = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
    meshCube->setColor(std::vector{meshCube->getVertexData().size(), glm::vec3(1.f, 0.f, 0.f)},
                       _core->getCommandBufferApplication());
    auto cubeColored = _core->createShape3D(ShapeType::CUBE, meshCube);
    cubeColored->setTranslate(glm::vec3(0.f, 3.f, -3.f));
    _core->addDrawable(cubeColored);
  }
  // color material
  auto cubemap = _core->createCubemap(
      std::vector<std::string>{"../assets/right.jpg", "../assets/left.jpg", "../assets/top.jpg", "../assets/bottom.jpg",
                               "../assets/front.jpg", "../assets/back.jpg"},
      settings->getLoadTextureColorFormat(), mipMapLevels);
  auto materialCubeTextured = _core->createMaterialColor(MaterialTarget::SIMPLE);
  materialCubeTextured->setBaseColor({cubemap->getTexture()});
  // Phong material
  auto cubemapColorPhong = _core->createCubemap(
      std::vector<std::string>{"../assets/brickwall.jpg", "../assets/brickwall.jpg", "../assets/brickwall.jpg",
                               "../assets/brickwall.jpg", "../assets/brickwall.jpg", "../assets/brickwall.jpg"},
      settings->getLoadTextureColorFormat(), mipMapLevels);
  auto cubemapNormalPhong = _core->createCubemap(
      std::vector<std::string>{"../assets/brickwall_normal.jpg", "../assets/brickwall_normal.jpg",
                               "../assets/brickwall_normal.jpg", "../assets/brickwall_normal.jpg",
                               "../assets/brickwall_normal.jpg", "../assets/brickwall_normal.jpg"},
      settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
  auto materialCubePhong = _core->createMaterialPhong(MaterialTarget::SIMPLE);
  materialCubePhong->setBaseColor({cubemapColorPhong->getTexture()});
  materialCubePhong->setNormal({cubemapNormalPhong->getTexture()});
  materialCubePhong->setSpecular({_core->getResourceManager()->getCubemapZero()->getTexture()});
  // cube textured
  {
    _cubeTextured = _core->createShape3D(ShapeType::CUBE, meshCubePlain);
    _cubeTextured->setMaterial(materialCubeTextured);
    _cubeTextured->setTranslate(glm::vec3(0.f, 0.f, -3.f));
    _core->addDrawable(_cubeTextured);
  }
  //  cube Phong
  {
    auto cubeTexturedPhong = _core->createShape3D(ShapeType::CUBE, meshCubePlain);
    cubeTexturedPhong->setMaterial(materialCubePhong);
    cubeTexturedPhong->setTranslate(glm::vec3(0.f, -3.f, -3.f));
    _core->addDrawable(cubeTexturedPhong);
  }
  // cube colored wireframe
  {
    auto meshCube = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
    meshCube->setColor(std::vector{meshCube->getVertexData().size(), glm::vec3(1.f, 0.f, 0.f)},
                       _core->getCommandBufferApplication());
    auto cubeColoredWireframe = _core->createShape3D(ShapeType::CUBE, meshCube);
    cubeColoredWireframe->setTranslate(glm::vec3(3.f, 3.f, -3.f));
    cubeColoredWireframe->setDrawType(DrawType::WIREFRAME);
    _core->addDrawable(cubeColoredWireframe);
  }
  {
    // cube texture wireframe
    _cubeTexturedWireframe = _core->createShape3D(ShapeType::CUBE, meshCubePlain);
    _cubeTexturedWireframe->setDrawType(DrawType::WIREFRAME);
    _cubeTexturedWireframe->setMaterial(materialCubeTextured);
    _cubeTexturedWireframe->setTranslate(glm::vec3(3.f, 0.f, -3.f));
    _core->addDrawable(_cubeTexturedWireframe);
  }
  {
    // cube Phong wireframe
    auto cubeTexturedWireframePhong = _core->createShape3D(ShapeType::CUBE, meshCubePlain);
    cubeTexturedWireframePhong->setDrawType(DrawType::WIREFRAME);
    cubeTexturedWireframePhong->setMaterial(materialCubePhong);
    cubeTexturedWireframePhong->setTranslate(glm::vec3(3.f, -3.f, -3.f));
    _core->addDrawable(cubeTexturedWireframePhong);
  }
  {
    // cube Phong mesh normal
    auto cubeTexturedPhongNormalMesh = _core->createShape3D(ShapeType::CUBE, meshCubePlain);
    cubeTexturedPhongNormalMesh->setDrawType(DrawType::NORMAL);
    cubeTexturedPhongNormalMesh->setTranslate(glm::vec3(-3.f, -3.f, -3.f));
    _core->addDrawable(cubeTexturedPhongNormalMesh);
  }
  {
    // cube Color fragment normal
    auto materialCubeTexturedNormalFragment = _core->createMaterialColor(MaterialTarget::SIMPLE);
    materialCubeTexturedNormalFragment->setBaseColor({cubemapNormalPhong->getTexture()});
    auto cubeTexturedNormalFragment = _core->createShape3D(ShapeType::CUBE, meshCubePlain);
    cubeTexturedNormalFragment->setMaterial(materialCubeTexturedNormalFragment);
    cubeTexturedNormalFragment->setTranslate(glm::vec3(-3.f, -3.f, -3.f));
    _core->addDrawable(cubeTexturedNormalFragment);
  }
  {
    // cube Phong: specular without normal
    auto cubemapColorPhongContainer = _core->createCubemap(
        std::vector<std::string>{"../assets/container.png", "../assets/container.png", "../assets/container.png",
                                 "../assets/container.png", "../assets/container.png", "../assets/container.png"},
        settings->getLoadTextureColorFormat(), mipMapLevels);

    auto cubemapSpecularPhong = _core->createCubemap(
        std::vector<std::string>{"../assets/container_specular.png", "../assets/container_specular.png",
                                 "../assets/container_specular.png", "../assets/container_specular.png",
                                 "../assets/container_specular.png", "../assets/container_specular.png"},
        settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto materialCubePhongSpecular = _core->createMaterialPhong(MaterialTarget::SIMPLE);
    materialCubePhongSpecular->setBaseColor({cubemapColorPhongContainer->getTexture()});
    materialCubePhongSpecular->setNormal({_core->getResourceManager()->getCubemapZero()->getTexture()});
    materialCubePhongSpecular->setSpecular({cubemapSpecularPhong->getTexture()});
    materialCubePhongSpecular->setCoefficients(glm::vec3(0.f), glm::vec3(0.2f), glm::vec3(1.f), 32);

    auto cubeTexturedPhongSpecular = _core->createShape3D(ShapeType::CUBE, meshCubePlain);
    cubeTexturedPhongSpecular->setMaterial(materialCubePhongSpecular);
    cubeTexturedPhongSpecular->setTranslate(glm::vec3(0.f, -6.f, -3.f));
    _core->addDrawable(cubeTexturedPhongSpecular);
  }
  // TODO: fix mipmaping
  auto cubemapColorPBR = _core->createCubemap(
      std::vector<std::string>{"../assets/rustediron2_basecolor.png", "../assets/rustediron2_basecolor.png",
                               "../assets/rustediron2_basecolor.png", "../assets/rustediron2_basecolor.png",
                               "../assets/rustediron2_basecolor.png", "../assets/rustediron2_basecolor.png"},
      settings->getLoadTextureColorFormat(), mipMapLevels);
  auto cubemapNormalPBR = _core->createCubemap(
      std::vector<std::string>{"../assets/rustediron2_normal.png", "../assets/rustediron2_normal.png",
                               "../assets/rustediron2_normal.png", "../assets/rustediron2_normal.png",
                               "../assets/rustediron2_normal.png", "../assets/rustediron2_normal.png"},
      settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
  auto cubemapMetallicPBR = _core->createCubemap(
      std::vector<std::string>{"../assets/rustediron2_metallic.png", "../assets/rustediron2_metallic.png",
                               "../assets/rustediron2_metallic.png", "../assets/rustediron2_metallic.png",
                               "../assets/rustediron2_metallic.png", "../assets/rustediron2_metallic.png"},
      settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
  auto cubemapRoughnessPBR = _core->createCubemap(
      std::vector<std::string>{"../assets/rustediron2_roughness.png", "../assets/rustediron2_roughness.png",
                               "../assets/rustediron2_roughness.png", "../assets/rustediron2_roughness.png",
                               "../assets/rustediron2_roughness.png", "../assets/rustediron2_roughness.png"},
      settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
  auto materialCubePBR = _core->createMaterialPBR(MaterialTarget::SIMPLE);
  // material can't have default state because it can be either cubemap or texture2D
  materialCubePBR->setBaseColor({cubemapColorPBR->getTexture()});
  materialCubePBR->setNormal({cubemapNormalPBR->getTexture()});
  materialCubePBR->setMetallic({cubemapMetallicPBR->getTexture()});
  materialCubePBR->setRoughness({cubemapRoughnessPBR->getTexture()});
  materialCubePBR->setEmissive({_core->getResourceManager()->getCubemapZero()->getTexture()});
  materialCubePBR->setOccluded({_core->getResourceManager()->getCubemapZero()->getTexture()});
  materialCubePBR->setDiffuseIBL(_core->getResourceManager()->getCubemapZero()->getTexture());
  materialCubePBR->setSpecularIBL(_core->getResourceManager()->getCubemapZero()->getTexture(),
                                  _core->getResourceManager()->getTextureZero());
  {
    // cube PBR
    auto cubeTexturedPBR = _core->createShape3D(ShapeType::CUBE, meshCubePlain);
    cubeTexturedPBR->setMaterial(materialCubePBR);
    cubeTexturedPBR->setTranslate(glm::vec3(0.f, 6.f, -3.f));
    _core->addDrawable(cubeTexturedPBR);
  }
  {
    // cube PBR wireframe
    auto cubeWireframePBR = _core->createShape3D(ShapeType::CUBE, meshCubePlain);
    cubeWireframePBR->setMaterial(materialCubePBR);
    cubeWireframePBR->setDrawType(DrawType::WIREFRAME);
    cubeWireframePBR->setTranslate(glm::vec3(3.f, 6.f, -3.f));
    _core->addDrawable(cubeWireframePBR);
  }
  auto meshSpherePlain = std::make_shared<MeshSphere>(_core->getCommandBufferApplication(), _core->getEngineState());
  {
    // sphere colored
    auto meshSpere = std::make_shared<MeshSphere>(_core->getCommandBufferApplication(), _core->getEngineState());
    meshSpere->setColor(std::vector{meshSpere->getVertexData().size(), glm::vec3(0.f, 1.f, 0.f)},
                        _core->getCommandBufferApplication());
    auto sphereColored = _core->createShape3D(ShapeType::SPHERE, meshSpere);
    sphereColored->setTranslate(glm::vec3(0.f, 3.f, 3.f));
    _core->addDrawable(sphereColored);
  }
  auto sphereTexture = _core->createTexture("../assets/right.jpg", settings->getLoadTextureColorFormat(), mipMapLevels);
  auto materialSphereTextured = _core->createMaterialColor(MaterialTarget::SIMPLE);
  materialSphereTextured->setBaseColor({sphereTexture});
  auto sphereTexturePhong = _core->createTexture("../assets/brickwall.jpg", settings->getLoadTextureColorFormat(),
                                                 mipMapLevels);
  auto sphereNormalPhong = _core->createTexture("../assets/brickwall_normal.jpg",
                                                settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
  auto materialSpherePhong = _core->createMaterialPhong(MaterialTarget::SIMPLE);
  materialSpherePhong->setBaseColor({sphereTexturePhong});
  materialSpherePhong->setNormal({sphereNormalPhong});
  materialSpherePhong->setSpecular({_core->getResourceManager()->getTextureZero()});
  {
    // sphere textured
    auto sphereTextured = _core->createShape3D(ShapeType::SPHERE, meshSpherePlain);
    sphereTextured->setMaterial(materialSphereTextured);
    sphereTextured->setTranslate(glm::vec3(0.f, 0.f, 3.f));
    _core->addDrawable(sphereTextured);
  }
  {
    // sphere Phong
    auto sphereTexturedPhong = _core->createShape3D(ShapeType::SPHERE, meshSpherePlain);
    sphereTexturedPhong->setMaterial(materialSpherePhong);
    sphereTexturedPhong->setTranslate(glm::vec3(0.f, -3.f, 3.f));
    _core->addDrawable(sphereTexturedPhong);
  }
  {
    // sphere colored wireframe
    auto meshSphere = std::make_shared<MeshSphere>(_core->getCommandBufferApplication(), _core->getEngineState());
    meshSphere->setColor(std::vector{meshSphere->getVertexData().size(), glm::vec3(0.f, 1.f, 0.f)},
                         _core->getCommandBufferApplication());

    auto sphereColoredWireframe = _core->createShape3D(ShapeType::SPHERE, meshSphere);
    sphereColoredWireframe->setDrawType(DrawType::WIREFRAME);
    sphereColoredWireframe->setTranslate(glm::vec3(3.f, 3.f, 3.f));
    _core->addDrawable(sphereColoredWireframe);
  }
  {
    // sphere texture wireframe
    auto sphereTexturedWireframe = _core->createShape3D(ShapeType::SPHERE, meshSpherePlain);
    sphereTexturedWireframe->setDrawType(DrawType::WIREFRAME);
    sphereTexturedWireframe->setMaterial(materialSphereTextured);
    sphereTexturedWireframe->setTranslate(glm::vec3(3.f, 0.f, 3.f));
    _core->addDrawable(sphereTexturedWireframe);
  }
  {
    // sphere Phong wireframe
    auto sphereTexturedWireframePhong = _core->createShape3D(ShapeType::SPHERE, meshSpherePlain);
    sphereTexturedWireframePhong->setDrawType(DrawType::WIREFRAME);
    sphereTexturedWireframePhong->setMaterial(materialSpherePhong);
    sphereTexturedWireframePhong->setTranslate(glm::vec3(3.f, -3.f, 3.f));
    _core->addDrawable(sphereTexturedWireframePhong);
  }
  {
    // sphere Phong mesh normal
    auto sphereTexturedPhongNormalMesh = _core->createShape3D(ShapeType::SPHERE, meshSpherePlain);
    sphereTexturedPhongNormalMesh->setDrawType(DrawType::NORMAL);
    sphereTexturedPhongNormalMesh->setTranslate(glm::vec3(-3.f, -3.f, 3.f));
    _core->addDrawable(sphereTexturedPhongNormalMesh);
  }
  {
    // sphere Color fragment normal
    auto materialSphereTexturedNormalFragment = _core->createMaterialColor(MaterialTarget::SIMPLE);
    materialSphereTexturedNormalFragment->setBaseColor({sphereNormalPhong});
    auto sphereTexturedNormalFragment = _core->createShape3D(ShapeType::SPHERE, meshSpherePlain);
    sphereTexturedNormalFragment->setMaterial(materialSphereTexturedNormalFragment);
    sphereTexturedNormalFragment->setTranslate(glm::vec3(-3.f, -3.f, 3.f));
    _core->addDrawable(sphereTexturedNormalFragment);
  }
  {
    // cube Phong: specular without normal
    auto sphereColorPhongContainer = _core->createTexture("../assets/container.png",
                                                          settings->getLoadTextureColorFormat(), mipMapLevels);
    auto sphereSpecularPhongContainer = _core->createTexture("../assets/container_specular.png",
                                                             settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto materialSpherePhongSpecular = _core->createMaterialPhong(MaterialTarget::SIMPLE);
    materialSpherePhongSpecular->setBaseColor({sphereColorPhongContainer});
    materialSpherePhongSpecular->setNormal({_core->getResourceManager()->getTextureZero()});
    materialSpherePhongSpecular->setSpecular({sphereSpecularPhongContainer});
    materialSpherePhongSpecular->setCoefficients(glm::vec3(0.f), glm::vec3(0.2f), glm::vec3(1.f), 32);

    auto sphereTexturedPhongSpecular = _core->createShape3D(ShapeType::SPHERE, meshSpherePlain);
    sphereTexturedPhongSpecular->setMaterial(materialSpherePhongSpecular);
    sphereTexturedPhongSpecular->setTranslate(glm::vec3(0.f, -6.f, 3.f));
    _core->addDrawable(sphereTexturedPhongSpecular);
  }
  // TODO: color is not so bright in comparison with cube
  auto sphereColorPBR = _core->createTexture("../assets/rustediron2_basecolor.png",
                                             settings->getLoadTextureColorFormat(), mipMapLevels);
  auto sphereNormalPBR = _core->createTexture("../assets/rustediron2_normal.png",
                                              settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
  auto sphereMetallicPBR = _core->createTexture("../assets/rustediron2_metallic.png",
                                                settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
  auto sphereRoughnessPBR = _core->createTexture("../assets/rustediron2_roughness.png",
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
    auto sphereTexturedPBR = _core->createShape3D(ShapeType::SPHERE, meshSpherePlain);
    sphereTexturedPBR->setMaterial(materialSpherePBR);
    sphereTexturedPBR->setTranslate(glm::vec3(0.f, 6.f, 3.f));
    _core->addDrawable(sphereTexturedPBR);
  }
  {
    // sphere PBR wireframe
    auto sphereWireframePBR = _core->createShape3D(ShapeType::SPHERE, meshSpherePlain);
    sphereWireframePBR->setMaterial(materialSpherePBR);
    sphereWireframePBR->setDrawType(DrawType::WIREFRAME);
    sphereWireframePBR->setTranslate(glm::vec3(3.f, 6.f, 3.f));
    _core->addDrawable(sphereWireframePBR);
  }

  _core->registerUpdate(std::bind(&Main::update, this));
  // can be lambda passed that calls reset
  _core->registerReset(std::bind(&Main::reset, this, std::placeholders::_1, std::placeholders::_2));
}

void Main::update() {
  static float i = 0;
  _cubeTextured->setTranslate(glm::vec3(0.f, 0.f, -3.f));
  _cubeTextured->setRotate(glm::quat(glm::vec3(glm::radians(i), 0.f, 0.f)));
  _cubeTexturedWireframe->setTranslate(glm::vec3(3.f, 0.f, -3.f));
  _cubeTexturedWireframe->setRotate(glm::quat(glm::vec3(glm::radians(i), 0.f, 0.f)));

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
  _core->getGUI()->startWindow("Help");
  _core->getGUI()->setWindowPosition({20, 20});
  _core->getGUI()->drawText({"Limited FPS: " + std::to_string(FPSLimited)});
  _core->getGUI()->drawText({"Maximum FPS: " + std::to_string(FPSReal)});
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