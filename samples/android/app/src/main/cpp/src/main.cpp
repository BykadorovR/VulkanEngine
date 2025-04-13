// Copyright 2022 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "DebugVisualization.h"
#include <Engine/Core.h>
#include <android/log.h>
#include <cassert>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <glm/gtc/random.hpp>
#include <random>
#include <vector>
#include <android/trace.h>
#include "Utility/PhysicsManager.h"

// Android log function wrappers
static const char* kTAG = "AndroidApplication";
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, kTAG, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, kTAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, kTAG, __VA_ARGS__))

// We will call this function the window is opened.
// This is where we will initialise everything
std::shared_ptr<Core> _core;
android_app* _app;
bool _initialized = false;
std::shared_ptr<CameraFly> _camera;
std::shared_ptr<PointLight> _pointLightVertical;
std::shared_ptr<PointLight> _pointLightHorizontal;
std::shared_ptr<DirectionalLight> _directionalLight;
std::shared_ptr<Shape3D> _cubeColoredLightVertical, _cubeColoredLightHorizontal, _cubeTextured, _cubeTexturedWireframe;
std::shared_ptr<Animation> _animationFish;
bool _showLoD = false, _showWireframe = false, _showNormals = false, _showPatches = false;
float _directionalValue = 0.5f, _pointVerticalValue = 1.f, _pointHorizontalValue = 10.f;
std::tuple<int, int> _resolution = {1080, 2400};
std::shared_ptr<DebugVisualization> _debugVisualization;
std::shared_ptr<Cubemap> _cubemapSkybox;
std::shared_ptr<IBL> _ibl;
std::shared_ptr<Equirectangular> _equirectangular;
std::shared_ptr<MaterialColor> _materialColor;
std::shared_ptr<MaterialPhong> _materialPhong;
std::shared_ptr<MaterialPBR> _materialPBR;
std::shared_ptr<TerrainGPU> _terrain;
std::shared_ptr<Model3D> _modelSimple;
std::shared_ptr<Shape3D> _boundingBox, _cubePlayer;
std::shared_ptr<Model3DPhysics> _model3DPhysics;
std::shared_ptr<Shape3DPhysics> _shape3DPhysics;
std::shared_ptr<PhysicsManager> _physicsManager;
std::shared_ptr<TerrainPhysics> _terrainPhysics;

int _typeIndex = 0;
int _patchX = 12, _patchY = 12;
float _heightScale = 64.f;
float _heightShift = 16.f;
std::array<float, 4> _heightLevels = {16, 128, 192, 256};
int _minTessellationLevel = 4, _maxTessellationLevel = 32;
float _minDistance = 30, _maxDistance = 100;
glm::vec3 _terrainPosition = glm::vec3(2.f, 0.f, 0.f);
glm::vec3 _terrainScale = glm::vec3(1.f);

void _createTerrainPhong() {
  _terrain = _core->createTerrainComposition(_core->loadImageCPU("heightmap.png"));
  _terrain->setPatchNumber(_patchX, _patchY);
  _terrain->initialize(_core->getCommandBufferApplication());
  _terrain->setMaterial(_materialPhong);
  _terrain->setScale(_terrainScale);
  _terrain->setTranslate(_terrainPosition);
  _terrain->setTessellationLevel(_minTessellationLevel, _maxTessellationLevel);
  _terrain->setTesselationDistance(_minDistance, _maxDistance);
  _terrain->setColorHeightLevels(_heightLevels);
  _terrain->setHeight(_heightScale, _heightShift);
}

void _createTerrainPBR() {
  _terrain = _core->createTerrainComposition(_core->loadImageCPU("heightmap.png"));
  _terrain->setPatchNumber(_patchX, _patchY);
  _terrain->initialize(_core->getCommandBufferApplication());
  _terrain->setMaterial(_materialPBR);
  _terrain->setScale(_terrainScale);
  _terrain->setTranslate(_terrainPosition);
  _terrain->setTessellationLevel(_minTessellationLevel, _maxTessellationLevel);
  _terrain->setTesselationDistance(_minDistance, _maxDistance);
  _terrain->setColorHeightLevels(_heightLevels);
  _terrain->setHeight(_heightScale, _heightShift);
}

void _createTerrainColor() {
  _terrain = _core->createTerrainComposition(_core->loadImageCPU("heightmap.png"));
  _terrain->setPatchNumber(_patchX, _patchY);
  _terrain->initialize(_core->getCommandBufferApplication());
  _terrain->setMaterial(_materialColor);
  _terrain->setScale(_terrainScale);
  _terrain->setTranslate(_terrainPosition);
  _terrain->setTessellationLevel(_minTessellationLevel, _maxTessellationLevel);
  _terrain->setTesselationDistance(_minDistance, _maxDistance);
  _terrain->setColorHeightLevels(_heightLevels);
  _terrain->setHeight(_heightScale, _heightShift);
}

void update() {
  float radius = 15.f;
  static float angleHorizontal = 90.f;
  glm::vec3 lightPositionHorizontal = glm::vec3(radius * cos(glm::radians(angleHorizontal)), radius,
                                                radius * sin(glm::radians(angleHorizontal)));
  _pointLightHorizontal->getCamera()->setPosition(lightPositionHorizontal);

  angleHorizontal += 0.05f;

  auto [FPSLimited, FPSReal] = _core->getFPS();
  auto [widthScreen, heightScreen] = _core->getEngineState()->getSettings()->getResolution();
  _core->getGUI()->startWindow("Help", 3.f);
  _core->getGUI()->setWindowPosition({20, 120});
  if (_core->getGUI()->startTree("Main", false)) {
    _core->getGUI()->drawText({"Limited FPS: " + std::to_string(FPSLimited)});
    _core->getGUI()->drawText({"Maximum FPS: " + std::to_string(FPSReal)});
    if (_core->getGUI()->drawButton("Reset")) {
      _shape3DPhysics->setTranslate(glm::vec3(0.f, 50.f, 0.f));
      _model3DPhysics->setTranslate(glm::vec3(-4.f, -1.f, -3.f));
    }
    _core->getGUI()->endTree();
  }
  if (_core->getGUI()->startTree("Terrain", false)) {
    std::map<std::string, int*> terrainType;
    terrainType["##Type"] = &_typeIndex;
    if (_core->getGUI()->drawListBox({"Color", "Phong", "PBR"}, terrainType, 3)) {
      _core->removeDrawable(_terrain);
      switch (_typeIndex) {
        case 0:
          _createTerrainColor();
          break;
        case 1:
          //_core->removeShadowable(_terrain);
          _createTerrainPhong();
          //_core->addShadowable(_terrain);
          break;
        case 2:
          //_core->removeShadowable(_terrain);
          _createTerrainPBR();
          //_core->addShadowable(_terrain);
          break;
      }
      _core->addDrawable(_terrain);
    }
    _core->getGUI()->endTree();
  }

  _debugVisualization->update();
  if (_core->getGUI()->startTree("Debug", false)) {
    _debugVisualization->draw();
    _core->getGUI()->endTree();
  }
  _core->getGUI()->endWindow();

  _cubePlayer->setTranslate(_shape3DPhysics->getTranslate());
  _cubePlayer->setRotate(_shape3DPhysics->getRotate());

  _boundingBox->setTranslate(_model3DPhysics->getTranslate());
  _boundingBox->setRotate(_model3DPhysics->getRotate());

  _modelSimple->setTranslate(_model3DPhysics->getTranslate());
  _modelSimple->setRotate(_model3DPhysics->getRotate());

  // Step the world
  _physicsManager->update();
  _model3DPhysics->postUpdate();
}

void initialize() {
  int mipMapLevels = 4;
  ;
  auto settings = std::make_shared<Settings>();
  settings->setName("Sprite");
  settings->setClearColor({0.01f, 0.01f, 0.01f, 1.f});
  // TODO: fullscreen if resolution is {0, 0}
  // TODO: validation layers complain if resolution is {2560, 1600}
  settings->setResolution(_resolution);
  settings->setShadowMapResolution({512, 512});
  // for HDR, linear 16 bit per channel to represent values outside of 0-1 range
  // (UNORM - float [0, 1], SFLOAT - float)
  // https://registry.khronos.org/vulkan/specs/1.1/html/vkspec.html#_identification_of_formats
  settings->setGraphicColorFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
  settings->setSwapchainColorFormat(VK_FORMAT_R8G8B8A8_UNORM);
  // SRGB the same as UNORM but + gamma conversion out of box (!)
  settings->setLoadTextureColorFormat(VK_FORMAT_R8G8B8A8_SRGB);
  settings->setLoadTextureAuxilaryFormat(VK_FORMAT_R8G8B8A8_UNORM);
  settings->setAnisotropicSamples(0);
  settings->setDepthFormat(VK_FORMAT_D32_SFLOAT);
  settings->setMaxFramesInFlight(2);
  settings->setThreadsInPool(6);
  settings->setDesiredFPS(60);
  settings->setAssetsPath("");
  settings->setShadersPath("");

  _core = std::make_shared<Core>(settings);
  _core->setAssetManager(_app->activity->assetManager);
  _core->setNativeWindow(_app->window);
  _core->initialize();
  _core->createGUI();
  _core->createPostprocessing();

  auto commandBufferTransfer = _core->getCommandBufferApplication();

  _camera = std::make_shared<CameraFly>(_core->getEngineState());
  _camera->setProjectionParameters(60.f, 0.1f, 100.f);
  _camera->setSpeed(0.1f, 0.05f);
  _core->getEngineState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_camera));
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

  auto postprocessing = _core->getPostprocessing();

  _pointLightHorizontal = _core->createPointLight();
  _core->createPointShadow(_pointLightHorizontal);
  _pointLightHorizontal->setColor(glm::vec3(1.f, 1.f, 1.f));
  _pointLightHorizontal->getCamera()->setPosition({3.f, 4.f, 0.f});

  auto ambientLight = _core->createAmbientLight();
  // calculate ambient color with default gamma
  float ambientColor = std::pow(0.05f, postprocessing->getGamma());
  ambientLight->setColor(glm::vec3(ambientColor, ambientColor, ambientColor));

  _directionalLight = _core->createDirectionalLight();
  _directionalLight->setColor(glm::vec3(0.1f, 0.1f, 0.1f));
  _directionalLight->getCamera()->setArea({-10.f, 10.f, -10.f, 10.f}, 0.1f, 40.f);
  _directionalLight->getCamera()->setPosition({0.f, 10.f, 0.f});
  _core->createDirectionalShadow(_directionalLight);
  _debugVisualization = std::make_shared<DebugVisualization>(_camera, _core);

  {
    auto texture = _core->createTexture("brickwall.jpg", settings->getLoadTextureColorFormat(), 1);
    auto normalMap = _core->createTexture("brickwall_normal.jpg", settings->getLoadTextureAuxilaryFormat(), 1);

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
    spriteTop->setTranslate(glm::vec3(0.0f, 0.5f, 0.5f));
    spriteTop->setRotate(glm::vec3(glm::radians(-90.f), 0.f, 0.f));
    _core->addDrawable(spriteTop);

    auto spriteBot = _core->createSprite();
    spriteBot->setMaterial(material);
    spriteBot->setTranslate(glm::vec3(0.0f, -0.5f, 0.5f));
    spriteBot->setRotate(glm::vec3(glm::radians(90.f), 0.f, 0.f));
    _core->addDrawable(spriteBot);
  }
  auto modelGLTF = _core->createModelGLTF("BrainStem.gltf");
  auto modelGLTFPhong = _core->createModel3D(modelGLTF);
  auto phongMaterial = modelGLTF->getMaterialsPhong();
  for (auto& material : phongMaterial) {
    fillMaterialPhong(material);
  }
  modelGLTFPhong->setMaterial(phongMaterial);
  auto animationDancing = _core->createAnimation(modelGLTF);
  // set animation to model, so joints will be passed to shader
  modelGLTFPhong->setAnimation(animationDancing);
  modelGLTFPhong->setScale(glm::vec3(1.f, 1.f, 1.f));
  modelGLTFPhong->setTranslate(glm::vec3(-2.f, 2.f, -5.f));
  _core->addDrawable(modelGLTFPhong);
  _core->addShadowable(modelGLTFPhong);

  auto modelGLTFHelmet = _core->createModelGLTF("DamagedHelmet/DamagedHelmet.gltf");
  auto modelGLTFPBR = _core->createModel3D(modelGLTFHelmet);
  auto pbrMaterial = modelGLTFHelmet->getMaterialsPBR();
  for (auto& material : pbrMaterial) {
    fillMaterialPBR(material);
  }
  modelGLTFPBR->setMaterial(pbrMaterial);
  modelGLTFPBR->setTranslate(glm::vec3(-2.f, 2.f, -3.f));
  modelGLTFPBR->setScale(glm::vec3(1.f, 1.f, 1.f));
  _core->addDrawable(modelGLTFPBR);
  _core->addShadowable(modelGLTFPBR);
  // terrain
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
    auto tile0 = _core->createTexture("desert/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile1 = _core->createTexture("rock/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile2 = _core->createTexture("grass/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile3 = _core->createTexture("ground/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    _materialColor = _core->createMaterialColor(MaterialTarget::TERRAIN);
    _materialColor->setBaseColor({tile0, tile1, tile2, tile3});
  }

  {
    auto tile0Color = _core->createTexture("desert/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile0Normal = _core->createTexture("desert/normal.png", settings->getLoadTextureAuxilaryFormat(),
                                            mipMapLevels);
    auto tile1Color = _core->createTexture("rock/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile1Normal = _core->createTexture("rock/normal.png", settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile2Color = _core->createTexture("grass/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile2Normal = _core->createTexture("grass/normal.png", settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile3Color = _core->createTexture("ground/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile3Normal = _core->createTexture("ground/normal.png", settings->getLoadTextureAuxilaryFormat(),
                                            mipMapLevels);

    _materialPhong = _core->createMaterialPhong(MaterialTarget::TERRAIN);
    _materialPhong->setBaseColor({tile0Color, tile1Color, tile2Color, tile3Color});
    _materialPhong->setNormal({tile0Normal, tile1Normal, tile2Normal, tile3Normal});
    fillMaterialPhongTerrain(_materialPhong);
  }

  {
    auto tile0Color = _core->createTexture("desert/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile0Normal = _core->createTexture("desert/normal.png", settings->getLoadTextureAuxilaryFormat(),
                                            mipMapLevels);
    auto tile0Metallic = _core->createTexture("desert/metallic.png", settings->getLoadTextureAuxilaryFormat(),
                                              mipMapLevels);
    auto tile0Roughness = _core->createTexture("desert/roughness.png", settings->getLoadTextureAuxilaryFormat(),
                                               mipMapLevels);
    auto tile0AO = _core->createTexture("desert/ao.png", settings->getLoadTextureAuxilaryFormat(), mipMapLevels);

    auto tile1Color = _core->createTexture("rock/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile1Normal = _core->createTexture("rock/normal.png", settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile1Metallic = _core->createTexture("rock/metallic.png", settings->getLoadTextureAuxilaryFormat(),
                                              mipMapLevels);
    auto tile1Roughness = _core->createTexture("rock/roughness.png", settings->getLoadTextureAuxilaryFormat(),
                                               mipMapLevels);
    auto tile1AO = _core->createTexture("rock/ao.png", settings->getLoadTextureAuxilaryFormat(), mipMapLevels);

    auto tile2Color = _core->createTexture("grass/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile2Normal = _core->createTexture("grass/normal.png", settings->getLoadTextureAuxilaryFormat(), mipMapLevels);
    auto tile2Metallic = _core->createTexture("grass/metallic.png", settings->getLoadTextureAuxilaryFormat(),
                                              mipMapLevels);
    auto tile2Roughness = _core->createTexture("grass/roughness.png", settings->getLoadTextureAuxilaryFormat(),
                                               mipMapLevels);
    auto tile2AO = _core->createTexture("grass/ao.png", settings->getLoadTextureAuxilaryFormat(), mipMapLevels);

    auto tile3Color = _core->createTexture("ground/albedo.png", settings->getLoadTextureColorFormat(), mipMapLevels);
    auto tile3Normal = _core->createTexture("ground/normal.png", settings->getLoadTextureAuxilaryFormat(),
                                            mipMapLevels);
    auto tile3Metallic = _core->createTexture("ground/metallic.png", settings->getLoadTextureAuxilaryFormat(),
                                              mipMapLevels);
    auto tile3Roughness = _core->createTexture("ground/roughness.png", settings->getLoadTextureAuxilaryFormat(),
                                               mipMapLevels);
    auto tile3AO = _core->createTexture("ground/ao.png", settings->getLoadTextureAuxilaryFormat(), mipMapLevels);

    _materialPBR = _core->createMaterialPBR(MaterialTarget::TERRAIN);
    _materialPBR->setBaseColor({tile0Color, tile1Color, tile2Color, tile3Color});
    _materialPBR->setNormal({tile0Normal, tile1Normal, tile2Normal, tile3Normal});
    _materialPBR->setMetallic({tile0Metallic, tile1Metallic, tile2Metallic, tile3Metallic});
    _materialPBR->setRoughness({tile0Roughness, tile1Roughness, tile2Roughness, tile3Roughness});
    _materialPBR->setOccluded({tile0AO, tile1AO, tile2AO, tile3AO});
    fillMaterialPBRTerrain(_materialPBR);
  }

  _createTerrainColor();
  _core->addDrawable(_terrain);
  //_core->addShadowable(_terrain);

  auto particleTexture = _core->createTexture("gradient.png", settings->getLoadTextureAuxilaryFormat(), 1);

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
  particleSystem->setScale(glm::vec3(0.5f, 0.5f, 0.5f));
  particleSystem->setTranslate(glm::vec3(0.f, 0.f, 2.f));

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
  _cubemapSkybox = _core->createCubemap({"right.jpg", "left.jpg", "top.jpg", "bottom.jpg", "front.jpg", "back.jpg"},
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
  _equirectangular = _core->createEquirectangular("newport_loft.hdr");
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

  {
    auto heightmapCPU = _core->loadImageCPU("heightmap.png");

    _physicsManager = _core->createPhysicsManager();
    _terrainPhysics = std::make_shared<TerrainPhysics>(heightmapCPU, _terrainPosition, _terrainScale,
                                                       std::tuple{64, 16}, _physicsManager, _core->getGameState(),
                                                       _core->getEngineState());

    auto meshCube = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
    meshCube->setColor(std::vector{meshCube->getVertexData().size(), glm::vec3(0.f, 0.f, 1.f)},
                       _core->getCommandBufferApplication());
    _cubePlayer = _core->createShape3D(ShapeType::CUBE, meshCube);
    _cubePlayer->setTranslate(glm::vec3(0.f, 2.f, 0.f));

    _core->addDrawable(_cubePlayer);

    _shape3DPhysics = std::make_shared<Shape3DPhysics>(glm::vec3(0.f, 50.f, 0.f), glm::vec3(0.5f, 0.5f, 0.5f),
                                                       _physicsManager);

    auto fillMaterialColor = [core = _core](std::shared_ptr<MaterialColor> material) {
      if (material->getBaseColor().size() == 0) material->setBaseColor({core->getResourceManager()->getTextureOne()});
    };

    auto gltfModelSimple = _core->createModelGLTF("BrainStem.gltf");
    _modelSimple = _core->createModel3D(gltfModelSimple);
    auto materialModelSimple = gltfModelSimple->getMaterialsColor();
    for (auto& material : materialModelSimple) {
      fillMaterialColor(material);
    }
    _modelSimple->setMaterial(materialModelSimple);

    auto aabb = _modelSimple->getAABBPositions();
    auto min = aabb->getMin();
    auto max = aabb->getMax();

    _modelSimple->setTranslate(glm::vec3(-4.f, -1.f, -3.f));
    _modelSimple->setOriginShift(glm::vec3(0.f, -((max - min) / 2.f).y, 0.f));
    _core->addDrawable(_modelSimple);

    auto meshCubeBB = std::make_shared<MeshCube>(_core->getCommandBufferApplication(), _core->getEngineState());
    meshCubeBB->setColor(std::vector{meshCube->getVertexData().size(), glm::vec3(1.f, 0.f, 0.f)},
                         _core->getCommandBufferApplication());
    _boundingBox = _core->createShape3D(ShapeType::CUBE, meshCubeBB);
    _boundingBox->setDrawType(DrawType::WIREFRAME);
    _boundingBox->setScale(max - min);
    _core->addDrawable(_boundingBox);
    _model3DPhysics = std::make_shared<Model3DPhysics>(glm::vec3(-4.f, -1.f, -3.f), (max - min) / 2.f, _physicsManager);
  }

  _core->registerUpdate(std::bind(&update));

  _initialized = true;
}

void terminate(void) {}

bool _move = false;
void handle_input(android_app* app) {
  if (_move) {
    LOGI("Movement process");
    _core->getEngineState()->getInput()->keyHandler(87, 0, 1, 0);
  }

  auto* inputBuffer = android_app_swap_input_buffers(app);
  if (!inputBuffer) {
    // no inputs yet.
    return;
  }

  // handle motion events (motionEventsCounts can be 0).
  for (auto i = 0; i < inputBuffer->motionEventsCount; i++) {
    auto& motionEvent = inputBuffer->motionEvents[i];
    auto action = motionEvent.action;

    // Find the pointer index, mask and bitshift to turn it into a readable
    // value.
    auto pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    LOGI("Pointer(s): ");

    // get the x and y position of this event if it is not ACTION_MOVE.
    auto& pointer = motionEvent.pointers[pointerIndex];
    auto x = GameActivityPointerAxes_getX(&pointer);
    auto y = GameActivityPointerAxes_getY(&pointer);

    // determine the action type and process the event accordingly.
    switch (action & AMOTION_EVENT_ACTION_MASK) {
      case AMOTION_EVENT_ACTION_DOWN:
      case AMOTION_EVENT_ACTION_POINTER_DOWN:
        LOGI("( %d, %f, %f ) Pointer Down", pointer.id, x, y);
        if (pointer.id == 1) {
          LOGI("Start movement");
          _move = true;
        }
        if (pointer.id == 0) {
          // 0 - left button, 1 - press, 0 = mod, doesn't matter
          _core->getEngineState()->getInput()->cursorHandler(x, y);
          _core->getEngineState()->getInput()->mouseHandler(0, 1, 0);
        }
        break;

      case AMOTION_EVENT_ACTION_CANCEL:
        // treat the CANCEL as an UP event: doing nothing in the app, except
        // removing the pointer from the cache if pointers are locally saved.
        // code pass through on purpose.
      case AMOTION_EVENT_ACTION_UP:
      case AMOTION_EVENT_ACTION_POINTER_UP:
        LOGI("( %d, %f, %f ) Pointer Up", pointer.id, x, y);
        if (pointer.id == 1) {
          _move = false;
          LOGI("End movement");
          _core->getEngineState()->getInput()->keyHandler(87, 0, 0, 0);
        }
        if (pointer.id == 0) {
          // 0 - left button, 0 - release, 0 = mod, doesn't matter
          _core->getEngineState()->getInput()->cursorHandler(x, y);
          _core->getEngineState()->getInput()->mouseHandler(0, 0, 0);
        }
        break;

      case AMOTION_EVENT_ACTION_MOVE:
        // There is no pointer index for ACTION_MOVE, only a snapshot of
        // all active pointers; app needs to cache previous active pointers
        // to figure out which ones are actually moved.
        for (auto index = 0; index < motionEvent.pointerCount; index++) {
          pointer = motionEvent.pointers[index];
          x = GameActivityPointerAxes_getX(&pointer);
          y = GameActivityPointerAxes_getY(&pointer);
          if (pointer.id == 0) {
            _core->getEngineState()->getInput()->cursorHandler(x, y);
          }
        }
        break;
      default:
        LOGI("Unknown MotionEvent Action: %d", action);
    }
  }
  // clear the motion input count in this render_buffer for main thread to
  // re-use.
  android_app_clear_motion_events(inputBuffer);

  // handle input key events.
  for (auto i = 0; i < inputBuffer->keyEventsCount; i++) {
    auto& keyEvent = inputBuffer->keyEvents[i];
    LOGI("Key: %d", keyEvent.keyCode);
    switch (keyEvent.action) {
      case AKEY_EVENT_ACTION_DOWN:
        LOGI("Key down");
        break;
      case AKEY_EVENT_ACTION_UP:
        LOGI("Key up");
        break;
      case AKEY_EVENT_ACTION_MULTIPLE:
        // Deprecated since Android API level 29.
        LOGI("Mulptiple key actions");
        ;
        break;
      default:
        LOGI("Unknown key event %d", keyEvent.action);
    }
  }
  // clear the key input count too.
  android_app_clear_key_events(inputBuffer);
}

// typical Android NativeActivity entry function
void android_main(android_app* app) {
  _app = app;
  app->onAppCmd = [](android_app* app, int32_t cmd) {
    switch (cmd) {
      case APP_CMD_INIT_WINDOW:
        // The window is being shown, get it ready.
        _resolution = {ANativeWindow_getWidth(app->window), ANativeWindow_getHeight(app->window)};
        initialize();
        break;
      case APP_CMD_TERM_WINDOW:
        // The window is being hidden or closed, clean it up.
        terminate();
        break;
      default:
        LOGI("event not handled: %d", cmd);
    }
  };

  int events;
  android_poll_source* source;
  do {
    auto number = ALooper_pollOnce(_initialized ? 1 : 0, nullptr, &events, (void**)&source);
    if (number >= 0) {
      if (source != NULL) source->process(app, source);
    }

    handle_input(app);

    char* customEventName = new char[32];
    sprintf(customEventName, "draw call");
    ATrace_beginSection(customEventName);

    if (_initialized) _core->draw();

    ATrace_endSection();
  } while (app->destroyRequested == 0);
}