#include "Main.h"
#include <random>
#include <glm/gtc/random.hpp>
#include "Graphic/IBL.h"
#include "Primitive/Equirectangular.h"

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

void Main::_createTerrainPhong() {
  _core->removeDrawable(_terrain);
  _core->removeShadowable(_terrain);
  _terrain = _core->createTerrainComposition(_core->loadImageCPU("../../terrain/assets/heightmap.png"));
  _terrain->setPatchNumber(_patchX, _patchY);
  _terrain->initialize(_core->getCommandBufferApplication());
  _terrain->setMaterial(_materialPhong);
  _terrain->setScale(_terrainScale);
  _terrain->setTranslate(_terrainPosition);
  _terrain->setTessellationLevel(_minTessellationLevel, _maxTessellationLevel);
  _terrain->setTesselationDistance(_minDistance, _maxDistance);
  _terrain->setColorHeightLevels(_heightLevels);
  _terrain->setHeight(_heightScale, _heightShift);

  _core->addDrawable(_terrain);
  _core->addShadowable(_terrain);
}

void Main::_createTerrainPBR() {
  _core->removeDrawable(_terrain);
  _core->removeShadowable(_terrain);
  _terrain = _core->createTerrainComposition(_core->loadImageCPU("../../terrain/assets/heightmap.png"));
  _terrain->setPatchNumber(_patchX, _patchY);
  _terrain->initialize(_core->getCommandBufferApplication());
  _terrain->setMaterial(_materialPBR);
  _terrain->setScale(_terrainScale);
  _terrain->setTranslate(_terrainPosition);
  _terrain->setTessellationLevel(_minTessellationLevel, _maxTessellationLevel);
  _terrain->setTesselationDistance(_minDistance, _maxDistance);
  _terrain->setColorHeightLevels(_heightLevels);
  _terrain->setHeight(_heightScale, _heightShift);

  _core->addDrawable(_terrain);
  _core->addShadowable(_terrain);
}

void Main::_createTerrainColor() {
  _core->removeDrawable(_terrain);
  _core->removeShadowable(_terrain);
  _terrain = _core->createTerrainComposition(_core->loadImageCPU("../../terrain/assets/heightmap.png"));
  _terrain->setPatchNumber(_patchX, _patchY);
  _terrain->initialize(_core->getCommandBufferApplication());
  _terrain->setMaterial(_materialColor);
  _terrain->setScale(_terrainScale);
  _terrain->setTranslate(_terrainPosition);
  _terrain->setTessellationLevel(_minTessellationLevel, _maxTessellationLevel);
  _terrain->setTesselationDistance(_minDistance, _maxDistance);
  _terrain->setColorHeightLevels(_heightLevels);
  _terrain->setHeight(_heightScale, _heightShift);

  _core->addDrawable(_terrain);
}

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

  // start transfer command buffer
  auto commandBufferTransfer = _core->getCommandBufferApplication();
  _camera = std::make_shared<CameraFly>(_core->getEngineState());
  _camera->setSpeed(0.05f, 0.01f);
  _camera->setProjectionParameters(60.f, 0.1f, 100.f);
  _core->getEngineState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_camera));
  _inputHandler = std::make_shared<InputHandler>(_core);
  _core->getEngineState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_inputHandler));
  _core->setCamera(_camera);

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

  auto postprocessing = _core->createPostprocessing();
  _core->createBloomBlur();

  _pointLightHorizontal = _core->createPointLight();
  _core->createPointShadow(_pointLightHorizontal);
  _pointLightHorizontal->setColor(glm::vec3(1.f, 1.f, 1.f));
  _pointLightHorizontal->getCamera()->setPosition({3.f, 4.f, 0.f});

  auto ambientLight = _core->createAmbientLight();
  // calculate ambient color with default gamma
  float ambientColor = std::pow(0.05f, postprocessing->getGamma());
  ambientLight->setColor(glm::vec3(ambientColor, ambientColor, ambientColor));

  _directionalLight = _core->createDirectionalLight();
  _core->createDirectionalShadow(_directionalLight);
  _directionalLight->setColor(glm::vec3(1.f, 1.f, 1.f));
  _directionalLight->getCamera()->setArea({-10.f, 10.f, -10.f, 10.f}, 0.1f, 40.f);
  _directionalLight->getCamera()->setPosition({0.f, 15.f, 0.f});

  _debugVisualization = std::make_shared<DebugVisualization>(_camera, _core);

  {
    auto texture = _core->createTexture("../../sprite/assets/brickwall.jpg", settings->getLoadTextureColorFormat(), 1);
    auto normalMap = _core->createTexture("../../sprite/assets/brickwall_normal.jpg",
                                          settings->getLoadTextureAuxilaryFormat(), 1);

    auto material = _core->createMaterialPhong(MaterialTarget::SIMPLE);
    material->setBaseColor({texture});
    material->setNormal({normalMap});
    material->setSpecular({_core->getResourceManager()->getTextureZero()});

    auto spriteForward = _core->createSprite();
    spriteForward->setMaterial(material);
    spriteForward->setTranslate(glm::vec3(0.f, 0.f, 1.f));
    _core->addDrawable(spriteForward);

    auto spriteBackward = _core->createSprite();
    spriteBackward->setMaterial(material);
    spriteBackward->setRotate(glm::vec3(0.f, glm::radians(180.f), 0.f));
    _core->addDrawable(spriteBackward);

    auto spriteLeft = _core->createSprite();
    spriteLeft->setMaterial(material);
    spriteLeft->setTranslate(glm::vec3(-0.5f, 0.f, 0.5f));
    spriteLeft->setRotate(glm::vec3(0.f, glm::radians(-90.f), 0.f));
    _core->addDrawable(spriteLeft);

    auto spriteRight = _core->createSprite();
    spriteRight->setMaterial(material);
    spriteRight->setTranslate(glm::vec3(0.5f, 0.f, 0.5f));
    spriteRight->setRotate(glm::vec3(0.f, glm::radians(90.f), 0.f));
    _core->addDrawable(spriteRight);

    auto spriteTop = _core->createSprite();
    spriteTop->setMaterial(material);
    spriteTop->setTranslate(glm::vec3(0.f, 0.5f, 0.5f));
    spriteTop->setRotate(glm::vec3(glm::radians(-90.f), 0.f, 0.f));
    _core->addDrawable(spriteTop);

    auto spriteBot = _core->createSprite();
    spriteBot->setMaterial(material);
    spriteBot->setTranslate(glm::vec3(0.f, -0.5f, 0.5f));
    spriteBot->setRotate(glm::vec3(glm::radians(90.f), 0.f, 0.f));
    _core->addDrawable(spriteBot);
  }
  auto modelGLTF = _core->createModelGLTF("../../IBL/assets/DamagedHelmet/DamagedHelmet.gltf");
  auto modelGLTFPhong = _core->createModel3D(modelGLTF);
  modelGLTFPhong->setMaterial(modelGLTF->getMaterialsPhong());
  modelGLTFPhong->setScale(glm::vec3(1.f, 1.f, 1.f));
  modelGLTFPhong->setTranslate(glm::vec3(-2.f, 2.f, -5.f));
  _core->addDrawable(modelGLTFPhong);
  _core->addShadowable(modelGLTFPhong);

  auto modelGLTFPBR = _core->createModel3D(modelGLTF);
  auto pbrMaterial = modelGLTF->getMaterialsPBR();
  for (auto& material : pbrMaterial) {
    fillMaterialPBR(material);
  }
  modelGLTFPBR->setMaterial(pbrMaterial);
  modelGLTFPBR->setTranslate(glm::vec3(-2.f, 2.f, -3.f));
  modelGLTFPBR->setScale(glm::vec3(1.f, 1.f, 1.f));
  _core->addDrawable(modelGLTFPBR);
  _core->addShadowable(modelGLTFPBR);

  auto fillMaterialPhongTerrain = [core = _core](std::shared_ptr<MaterialPhong> material) {
    if (material->getBaseColor().size() == 0)
      material->setBaseColor(std::vector{4, core->getResourceManager()->getTextureOne()});
    if (material->getNormal().size() == 0)
      material->setNormal(std::vector{4, core->getResourceManager()->getTextureZero()});
    if (material->getSpecular().size() == 0)
      material->setSpecular(std::vector{4, core->getResourceManager()->getTextureZero()});
  };

  auto fillMaterialPBRTerrain = [core = _core](std::shared_ptr<MaterialPBR> material) {
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
    auto tile0 = _core->createTexture("../../terrain/assets/desert/albedo.png", settings->getLoadTextureColorFormat(),
                                      mipMapLevels);
    auto tile1 = _core->createTexture("../../terrain/assets/rock/albedo.png", settings->getLoadTextureColorFormat(),
                                      mipMapLevels);
    auto tile2 = _core->createTexture("../../terrain/assets/grass/albedo.png", settings->getLoadTextureColorFormat(),
                                      mipMapLevels);
    auto tile3 = _core->createTexture("../../terrain/assets/ground/albedo.png", settings->getLoadTextureColorFormat(),
                                      mipMapLevels);
    _materialColor = _core->createMaterialColor(MaterialTarget::TERRAIN);
    _materialColor->setBaseColor({tile0, tile1, tile2, tile3});
  }

  {
    auto tile0Color = _core->createTexture("../../terrain/assets/desert/albedo.png",
                                           settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile0Normal = _core->createTexture("../../terrain/assets/desert/normal.png",
                                            settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile1Color = _core->createTexture("../../terrain/assets/rock/albedo.png",
                                           settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile1Normal = _core->createTexture("../../terrain/assets/rock/normal.png",
                                            settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile2Color = _core->createTexture("../../terrain/assets/grass/albedo.png",
                                           settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile2Normal = _core->createTexture("../../terrain/assets/grass/normal.png",
                                            settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile3Color = _core->createTexture("../../terrain/assets/ground/albedo.png",
                                           settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile3Normal = _core->createTexture("../../terrain/assets/ground/normal.png",
                                            settings->getLoadTextureAuxilaryFormat(), mipMapLevels);

    _materialPhong = _core->createMaterialPhong(MaterialTarget::TERRAIN);
    _materialPhong->setBaseColor({tile0Color, tile1Color, tile2Color, tile3Color});
    _materialPhong->setNormal({tile0Normal, tile1Normal, tile2Normal, tile3Normal});
    fillMaterialPhongTerrain(_materialPhong);
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

    _materialPBR = _core->createMaterialPBR(MaterialTarget::TERRAIN);
    _materialPBR->setBaseColor({tile0Color, tile1Color, tile2Color, tile3Color});
    _materialPBR->setNormal({tile0Normal, tile1Normal, tile2Normal, tile3Normal});
    _materialPBR->setMetallic({tile0Metallic, tile1Metallic, tile2Metallic, tile3Metallic});
    _materialPBR->setRoughness({tile0Roughness, tile1Roughness, tile2Roughness, tile3Roughness});
    _materialPBR->setOccluded({tile0AO, tile1AO, tile2AO, tile3AO});
    fillMaterialPBRTerrain(_materialPBR);
  }

  _createTerrainColor();

  auto particleTexture = _core->createTexture("../../Particles/assets/gradient.png",
                                              settings->getLoadTextureAuxilaryFormat(), 1);

  std::default_random_engine rndEngine((unsigned)time(nullptr));
  std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);
  // Initial particle positions on a circle
  std::vector<Particle> particles(360);
  float r = 0.05f;
  for (int i = 0; i < particles.size(); i++) {
    auto& particle = particles[i];
    particle.position = glm::sphericalRand(r);
    particle.radius = r;

    particle.minColor = glm::vec4(0.2f, 0.4f, 0.9f, 1.f);
    particle.maxColor = glm::vec4(0.3, 0.5f, 1.f, 1.f);
    particle.color = glm::vec4(particle.minColor.r + (particle.maxColor.r - particle.minColor.r) * rndDist(rndEngine),
                               particle.minColor.g + (particle.maxColor.g - particle.minColor.g) * rndDist(rndEngine),
                               particle.minColor.b + (particle.maxColor.b - particle.minColor.b) * rndDist(rndEngine),
                               particle.minColor.a + (particle.maxColor.a - particle.minColor.a) * rndDist(rndEngine));

    particle.minLife = 0.f;
    particle.maxLife = 1.f;
    particle.life = particle.minLife + (particle.maxLife - particle.minLife) * rndDist(rndEngine);
    particle.iteration = -1.f;
    particle.minVelocity = 0.f;
    particle.maxVelocity = 1.f;
    particle.velocity = particle.minVelocity + (particle.maxVelocity - particle.minVelocity) * rndDist(rndEngine);

    glm::vec3 lightPositionHorizontal = glm::vec3(cos(glm::radians((float)i)), 0, sin(glm::radians((float)i)));
    particle.velocityDirection = glm::normalize(lightPositionHorizontal);
  }
  auto particleSystem = _core->createParticleSystem(particles, particleTexture);
  particleSystem->setTranslate(glm::vec3(0.f, 0.f, 2.f));
  particleSystem->setScale(glm::vec3(0.5f, 0.5f, 0.5f));

  std::vector<std::shared_ptr<Shape3D>> spheres(6);
  // non HDR
  auto mesh0 = std::make_shared<MeshSphere>(_core->getCommandBufferApplication(), _core->getEngineState());
  mesh0->setColor({{0.f, 0.f, 0.1f}}, commandBufferTransfer);
  spheres[0] = _core->createShape3D(ShapeType::SPHERE, mesh0);
  spheres[0]->setTranslate(glm::vec3(0.f, 5.f, 0.f));
  auto mesh1 = std::make_shared<MeshSphere>(_core->getCommandBufferApplication(), _core->getEngineState());
  mesh1->setColor({{0.f, 0.f, 0.5f}}, commandBufferTransfer);
  spheres[1] = _core->createShape3D(ShapeType::SPHERE, mesh1);
  spheres[1]->setTranslate(glm::vec3(2.f, 5.f, 0.f));
  auto mesh2 = std::make_shared<MeshSphere>(_core->getCommandBufferApplication(), _core->getEngineState());
  mesh2->setColor({{0.f, 0.f, 10.f}}, commandBufferTransfer);
  spheres[2] = _core->createShape3D(ShapeType::SPHERE, mesh2);
  spheres[2]->setTranslate(glm::vec3(-2.f, 5.f, 0.f));
  // HDR
  auto mesh3 = std::make_shared<MeshSphere>(_core->getCommandBufferApplication(), _core->getEngineState());
  mesh3->setColor({{5.f, 0.f, 0.f}}, commandBufferTransfer);
  spheres[3] = _core->createShape3D(ShapeType::SPHERE, mesh3);
  spheres[3]->setTranslate(glm::vec3(0.f, 5.f, 2.f));
  auto mesh4 = std::make_shared<MeshSphere>(_core->getCommandBufferApplication(), _core->getEngineState());
  mesh4->setColor({{0.f, 5.f, 0.f}}, commandBufferTransfer);
  spheres[4] = _core->createShape3D(ShapeType::SPHERE, mesh4);
  spheres[4]->setTranslate(glm::vec3(-4.f, 7.f, -2.f));
  auto mesh5 = std::make_shared<MeshSphere>(_core->getCommandBufferApplication(), _core->getEngineState());
  mesh5->setColor({{0.f, 0.f, 20.f}}, commandBufferTransfer);
  spheres[5] = _core->createShape3D(ShapeType::SPHERE, mesh5);
  spheres[5]->setTranslate(glm::vec3(-4.f, 5.f, -2.f));
  for (auto& sphere : spheres) {
    _core->addDrawable(sphere);
  }

  auto [width, height] = settings->getResolution();
  _cubemapSkybox = _core->createCubemap(
      {"../assets/Skybox/right.jpg", "../assets/Skybox/left.jpg", "../assets/Skybox/top.jpg",
       "../assets/Skybox/bottom.jpg", "../assets/Skybox/front.jpg", "../assets/Skybox/back.jpg"},
      settings->getLoadTextureColorFormat(), 1);

  auto meshCube = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
  auto cube = _core->createShape3D(ShapeType::CUBE, meshCube);
  auto materialColor = _core->createMaterialColor(MaterialTarget::SIMPLE);
  materialColor->setBaseColor({_cubemapSkybox->getTexture()});
  cube->setMaterial(materialColor);
  cube->setTranslate(glm::vec3(0.f, 3.f, 0.f));
  _core->addDrawable(cube);

  auto equiCube = _core->createShape3D(ShapeType::CUBE, meshCube, VK_CULL_MODE_NONE);
  equiCube->setTranslate(glm::vec3(0.f, 3.f, -3.f));
  _core->addDrawable(equiCube);

  auto diffuseCube = _core->createShape3D(ShapeType::CUBE, meshCube, VK_CULL_MODE_NONE);
  diffuseCube->setTranslate(glm::vec3(2.f, 3.f, -3.f));
  _core->addDrawable(diffuseCube);

  auto specularCube = _core->createShape3D(ShapeType::CUBE, meshCube, VK_CULL_MODE_NONE);
  specularCube->setTranslate(glm::vec3(4.f, 3.f, -3.f));
  _core->addDrawable(specularCube);

  _ibl = _core->createIBL();
  _equirectangular = _core->createEquirectangular("../../IBL/assets/newport_loft.hdr");
  auto cubemapConverted = _equirectangular->getCubemap();
  auto materialColorEq = _core->createMaterialColor(MaterialTarget::SIMPLE);
  materialColorEq->setBaseColor({cubemapConverted->getTexture()});
  _ibl->setMaterial(materialColorEq);
  auto materialColorCM = _core->createMaterialColor(MaterialTarget::SIMPLE);
  auto materialColorDiffuse = _core->createMaterialColor(MaterialTarget::SIMPLE);
  auto materialColorSpecular = _core->createMaterialColor(MaterialTarget::SIMPLE);
  materialColorCM->setBaseColor({cubemapConverted->getTexture()});
  equiCube->setMaterial(materialColorCM);

  _ibl->drawDiffuse();
  _ibl->drawSpecular();
  _ibl->drawSpecularBRDF();

  // display specular as texture
  materialColorSpecular->setBaseColor({_ibl->getCubemapSpecular()->getTexture()});
  specularCube->setMaterial(materialColorSpecular);

  // display diffuse as texture
  materialColorDiffuse->setBaseColor({_ibl->getCubemapDiffuse()->getTexture()});
  diffuseCube->setMaterial(materialColorDiffuse);

  // set diffuse to material
  for (auto& material : pbrMaterial) {
    material->setDiffuseIBL(_ibl->getCubemapDiffuse()->getTexture());
  }

  // set specular to material
  for (auto& material : pbrMaterial) {
    material->setSpecularIBL(_ibl->getCubemapSpecular()->getTexture(), _ibl->getTextureSpecularBRDF());
  }

  auto materialBRDF = _core->createMaterialPhong(MaterialTarget::SIMPLE);
  materialBRDF->setBaseColor({_ibl->getTextureSpecularBRDF()});
  materialBRDF->setNormal({_core->getResourceManager()->getTextureZero()});
  materialBRDF->setSpecular({_core->getResourceManager()->getTextureZero()});
  materialBRDF->setCoefficients(glm::vec3(1.f), glm::vec3(0.f), glm::vec3(0.f), 0.f);
  auto spriteBRDF = _core->createSprite();
  spriteBRDF->enableLighting(false);
  spriteBRDF->enableShadow(false);
  spriteBRDF->enableDepth(false);
  spriteBRDF->setMaterial(materialBRDF);
  spriteBRDF->setTranslate(glm::vec3(5.f, 3.f, 1.f));
  _core->addDrawable(spriteBRDF);

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
  _pointLightHorizontal->getCamera()->setPosition(lightPositionHorizontal);

  angleHorizontal += 0.05f;

  auto [FPSLimited, FPSReal] = _core->getFPS();
  auto [widthScreen, heightScreen] = _core->getEngineState()->getSettings()->getResolution();
  _gui->startWindow("Help");
  _gui->setWindowPosition({20, 20});
  if (_gui->startTree("Main")) {
    _gui->drawText({"Limited FPS: " + std::to_string(FPSLimited)});
    _gui->drawText({"Maximum FPS: " + std::to_string(FPSReal)});
    _gui->drawText({"Press 'c' to turn cursor on/off"});
    _gui->endTree();
  }
  if (_gui->startTree("Terrain", false)) {
    std::map<std::string, int*> terrainType;
    terrainType["##Type"] = &_typeIndex;
    if (_gui->drawListBox({"Color", "Phong", "PBR"}, terrainType, 3)) {
      switch (_typeIndex) {
        case 0:
          _terrain->setMaterial(_materialColor);
          break;
        case 1:
          _terrain->setMaterial(_materialPhong);
          break;
        case 2:
          _terrain->setMaterial(_materialPBR);
          break;
      }
    }
    _gui->endTree();
  }

  _debugVisualization->update();
  if (_gui->startTree("Debug", false)) {
    _debugVisualization->draw();
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