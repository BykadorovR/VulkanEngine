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
#include <Core.h>
#include <android/log.h>
#include <cassert>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <glm/gtc/random.hpp>
#include <random>
#include <vector>

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
std::shared_ptr<Terrain> _terrainColor, _terrainPhong, _terrainPBR;
bool _showLoD = false, _showWireframe = false, _showNormals = false, _showPatches = false;
float _directionalValue = 0.5f, _pointVerticalValue = 1.f, _pointHorizontalValue = 10.f;
std::tuple<int, int> _resolution = {1080, 2400};
std::shared_ptr<DebugVisualization> _debugVisualization;

void update() {
  // update light position
  float radius = 15.f;
  static float angleHorizontal = 90.f;
  glm::vec3 lightPositionHorizontal = glm::vec3(radius * cos(glm::radians(angleHorizontal)), radius,
                                                radius * sin(glm::radians(angleHorizontal)));
  _pointLightHorizontal->setPosition(lightPositionHorizontal);

  angleHorizontal += 0.05f;

  auto [FPSLimited, FPSReal] = _core->getFPS();
  auto [widthScreen, heightScreen] = _core->getState()->getSettings()->getResolution();
  _core->getGUI()->startWindow("Help", {20, 120}, {widthScreen / 5, 0}, 3.f);
  if (_core->getGUI()->startTree("Main", false)) {
    _core->getGUI()->drawText({"Limited FPS: " + std::to_string(FPSLimited)});
    _core->getGUI()->drawText({"Maximum FPS: " + std::to_string(FPSReal)});
    _core->getGUI()->endTree();
  }
  if (_core->getGUI()->startTree("Debug", false)) {
    _debugVisualization->update();
    _core->getGUI()->endTree();
  }
  _core->getGUI()->endWindow();
}

void initialize() {
  if (!InitVulkan()) {
    LOGE("Vulkan is unavailable, install vulkan and re-start");
  }
  int mipMapLevels = 4;

  auto settings = std::make_shared<Settings>();
  settings->setName("Sprite");
  settings->setClearColor({0.01f, 0.01f, 0.01f, 1.f});
  // TODO: fullscreen if resolution is {0, 0}
  // TODO: validation layers complain if resolution is {2560, 1600}
  settings->setResolution(_resolution);
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
  settings->setThreadsInPool(0);
  settings->setDesiredFPS(60);

  _core = std::make_shared<Core>(settings);
  _core->setAssetManager(_app->activity->assetManager);
  _core->setNativeWindow(_app->window);
  _core->initialize();
  auto commandBufferTransfer = _core->getCommandBufferTransfer();

  _core->startRecording();
  _camera = std::make_shared<CameraFly>(_core->getState());
  _camera->setProjectionParameters(60.f, 0.1f, 100.f);
  _camera->setSpeed(0.1f, 0.05f);
  _core->getState()->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_camera));
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

  _pointLightHorizontal = _core->createPointLight(settings->getDepthResolution());
  _pointLightHorizontal->setColor(glm::vec3(1.f, 1.f, 1.f));
  _pointLightHorizontal->setPosition({3.f, 4.f, 0.f});

  auto ambientLight = _core->createAmbientLight();
  // calculate ambient color with default gamma
  float ambientColor = std::pow(0.05f, postprocessing->getGamma());
  ambientLight->setColor(glm::vec3(ambientColor, ambientColor, ambientColor));

  _directionalLight = _core->createDirectionalLight(settings->getDepthResolution());
  _directionalLight->setColor(glm::vec3(0.1f, 0.1f, 0.1f));
  _directionalLight->setPosition({0.f, 35.f, 0.f});
  _directionalLight->setCenter({0.f, 0.f, 0.f});
  _directionalLight->setUp({0.f, 0.f, -1.f});

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
    {
      glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, 1.f));
      spriteForward->setModel(model);
    }
    _core->addDrawable(spriteForward);

    auto spriteBackward = _core->createSprite();
    spriteBackward->setMaterial(material);
    {
      auto model = glm::rotate(glm::mat4(1.f), glm::radians(180.f), glm::vec3(0.f, 1.f, 0.f));
      spriteBackward->setModel(model);
    }
    _core->addDrawable(spriteBackward);

    auto spriteLeft = _core->createSprite();
    spriteLeft->setMaterial(material);
    {
      glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(-0.5f, 0.f, 0.5f));
      model = glm::rotate(model, glm::radians(-90.f), glm::vec3(0.f, 1.f, 0.f));
      spriteLeft->setModel(model);
    }
    _core->addDrawable(spriteLeft);

    auto spriteRight = _core->createSprite();
    spriteRight->setMaterial(material);
    {
      glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(0.5f, 0.f, 0.5f));
      model = glm::rotate(model, glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f));
      spriteRight->setModel(model);
    }
    _core->addDrawable(spriteRight);

    auto spriteTop = _core->createSprite();
    spriteTop->setMaterial(material);
    {
      glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(0.0f, 0.5f, 0.5f));
      model = glm::rotate(model, glm::radians(-90.f), glm::vec3(1.f, 0.f, 0.f));
      spriteTop->setModel(model);
    }
    _core->addDrawable(spriteTop);

    auto spriteBot = _core->createSprite();
    spriteBot->setMaterial(material);
    {
      glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(0.0f, -0.5f, 0.5f));
      model = glm::rotate(model, glm::radians(90.f), glm::vec3(1.f, 0.f, 0.f));
      spriteBot->setModel(model);
    }
    _core->addDrawable(spriteBot);
  }
  auto modelGLTF = _core->createModelGLTF("DamagedHelmet/DamagedHelmet.gltf");
  auto modelGLTFPhong = _core->createModel3D(modelGLTF);
  modelGLTFPhong->setMaterial(modelGLTF->getMaterialsPhong());
  {
    glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(-2.f, 2.f, -5.f));
    model = glm::scale(model, glm::vec3(1.f, 1.f, 1.f));
    modelGLTFPhong->setModel(model);
  }
  _core->addDrawable(modelGLTFPhong);
  _core->addShadowable(modelGLTFPhong);

  auto modelGLTFPBR = _core->createModel3D(modelGLTF);
  auto pbrMaterial = modelGLTF->getMaterialsPBR();
  for (auto& material : pbrMaterial) {
    fillMaterialPBR(material);
  }
  modelGLTFPBR->setMaterial(pbrMaterial);
  {
    glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(-2.f, 2.f, -3.f));
    model = glm::scale(model, glm::vec3(1.f, 1.f, 1.f));
    modelGLTFPBR->setModel(model);
  }
  _core->addDrawable(modelGLTFPBR);
  _core->addShadowable(modelGLTFPBR);

  auto tile0 = _core->createTexture("dirt.jpg", settings->getLoadTextureColorFormat(), mipMapLevels);
  auto tile1 = _core->createTexture("grass.jpg", settings->getLoadTextureColorFormat(), mipMapLevels);
  auto tile2 = _core->createTexture("rock_gray.png", settings->getLoadTextureColorFormat(), mipMapLevels);
  auto tile3 = _core->createTexture("snow.png", settings->getLoadTextureColorFormat(), mipMapLevels);

  auto terrain = _core->createTerrain("heightmap.png", std::pair{12, 12});
  auto materialTerrain = _core->createMaterialPhong(MaterialTarget::TERRAIN);
  materialTerrain->setBaseColor({tile0, tile1, tile2, tile3});
  auto fillMaterialTerrainPhong = [core = _core](std::shared_ptr<MaterialPhong> material) {
    if (material->getBaseColor().size() == 0)
      material->setBaseColor(std::vector{4, core->getResourceManager()->getTextureOne()});
    if (material->getNormal().size() == 0)
      material->setNormal(std::vector{4, core->getResourceManager()->getTextureZero()});
    if (material->getSpecular().size() == 0)
      material->setSpecular(std::vector{4, core->getResourceManager()->getTextureZero()});
  };
  fillMaterialTerrainPhong(materialTerrain);
  terrain->setMaterial(materialTerrain);
  {
    auto scaleMatrix = glm::scale(glm::mat4(1.f), glm::vec3(0.1f, 0.1f, 0.1f));
    auto translateMatrix = glm::translate(scaleMatrix, glm::vec3(2.f, -6.f, 0.f));
    terrain->setModel(translateMatrix);
  }
  _core->addDrawable(terrain, AlphaType::OPAQUE);
  _core->addShadowable(terrain);

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
  {
    auto matrix = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, 2.f));
    matrix = glm::scale(matrix, glm::vec3(0.5f, 0.5f, 0.5f));

    particleSystem->setModel(matrix);
  }
  _core->addParticleSystem(particleSystem);

  std::vector<std::shared_ptr<Shape3D>> spheres(6);
  // non HDR
  spheres[0] = _core->createShape3D(ShapeType::SPHERE);
  spheres[0]->getMesh()->setColor({{0.f, 0.f, 0.1f}}, commandBufferTransfer);
  {
    auto model = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 5.f, 0.f));
    spheres[0]->setModel(model);
  }
  spheres[1] = _core->createShape3D(ShapeType::SPHERE);
  spheres[1]->getMesh()->setColor({{0.f, 0.f, 0.5f}}, commandBufferTransfer);
  {
    auto model = glm::translate(glm::mat4(1.f), glm::vec3(2.f, 5.f, 0.f));
    spheres[1]->setModel(model);
  }
  spheres[2] = _core->createShape3D(ShapeType::SPHERE);
  spheres[2]->getMesh()->setColor({{0.f, 0.f, 10.f}}, commandBufferTransfer);
  {
    auto model = glm::translate(glm::mat4(1.f), glm::vec3(-2.f, 5.f, 0.f));
    spheres[2]->setModel(model);
  }
  // HDR
  spheres[3] = _core->createShape3D(ShapeType::SPHERE);
  spheres[3]->getMesh()->setColor({{5.f, 0.f, 0.f}}, commandBufferTransfer);
  {
    auto model = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 5.f, 2.f));
    spheres[3]->setModel(model);
  }
  spheres[4] = _core->createShape3D(ShapeType::SPHERE);
  spheres[4]->getMesh()->setColor({{0.f, 5.f, 0.f}}, commandBufferTransfer);
  {
    auto model = glm::translate(glm::mat4(1.f), glm::vec3(-4.f, 7.f, -2.f));
    spheres[4]->setModel(model);
  }
  spheres[5] = _core->createShape3D(ShapeType::SPHERE);
  spheres[5]->getMesh()->setColor({{0.f, 0.f, 20.f}}, commandBufferTransfer);
  {
    auto model = glm::translate(glm::mat4(1.f), glm::vec3(-4.f, 5.f, -2.f));
    spheres[5]->setModel(model);
  }

  for (auto& sphere : spheres) {
    _core->addDrawable(sphere);
  }

  auto [width, height] = settings->getResolution();
  auto cubemap = _core->createCubemap({"right.jpg", "left.jpg", "top.jpg", "bottom.jpg", "front.jpg", "back.jpg"},
                                      settings->getLoadTextureColorFormat(), 1);

  auto cube = _core->createShape3D(ShapeType::CUBE);
  auto materialColor = _core->createMaterialColor(MaterialTarget::SIMPLE);
  materialColor->setBaseColor({cubemap->getTexture()});
  cube->setMaterial(materialColor);
  {
    auto model = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 3.f, 0.f));
    cube->setModel(model);
  }
  _core->addDrawable(cube);

  auto ibl = _core->createIBL();

  auto equiCube = _core->createShape3D(ShapeType::CUBE, VK_CULL_MODE_NONE);
  {
    auto model = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 3.f, -3.f));
    equiCube->setModel(model);
  }
  _core->addDrawable(equiCube);

  auto diffuseCube = _core->createShape3D(ShapeType::CUBE, VK_CULL_MODE_NONE);
  {
    auto model = glm::translate(glm::mat4(1.f), glm::vec3(2.f, 3.f, -3.f));
    diffuseCube->setModel(model);
  }
  _core->addDrawable(diffuseCube);

  auto specularCube = _core->createShape3D(ShapeType::CUBE, VK_CULL_MODE_NONE);
  {
    auto model = glm::translate(glm::mat4(1.f), glm::vec3(4.f, 3.f, -3.f));
    specularCube->setModel(model);
  }
  _core->addDrawable(specularCube);

  auto equirectangular = _core->createEquirectangular("newport_loft.hdr");
  auto cubemapConverted = equirectangular->getCubemap();
  auto materialColorEq = _core->createMaterialColor(MaterialTarget::SIMPLE);
  materialColorEq->setBaseColor({cubemapConverted->getTexture()});
  ibl->setMaterial(materialColorEq);
  auto materialColorCM = _core->createMaterialColor(MaterialTarget::SIMPLE);
  auto materialColorDiffuse = _core->createMaterialColor(MaterialTarget::SIMPLE);
  auto materialColorSpecular = _core->createMaterialColor(MaterialTarget::SIMPLE);
  materialColorCM->setBaseColor({cubemapConverted->getTexture()});
  equiCube->setMaterial(materialColorCM);

  ibl->drawDiffuse();
  ibl->drawSpecular();
  ibl->drawSpecularBRDF();

  // display specular as texture
  materialColorSpecular->setBaseColor({ibl->getCubemapSpecular()->getTexture()});
  specularCube->setMaterial(materialColorSpecular);

  // display diffuse as texture
  materialColorDiffuse->setBaseColor({ibl->getCubemapDiffuse()->getTexture()});
  diffuseCube->setMaterial(materialColorDiffuse);

  // set diffuse to material
  for (auto& material : pbrMaterial) {
    material->setDiffuseIBL(ibl->getCubemapDiffuse()->getTexture());
  }

  // set specular to material
  for (auto& material : pbrMaterial) {
    material->setSpecularIBL(ibl->getCubemapSpecular()->getTexture(), ibl->getTextureSpecularBRDF());
  }

  auto materialBRDF = _core->createMaterialPhong(MaterialTarget::SIMPLE);
  materialBRDF->setBaseColor({ibl->getTextureSpecularBRDF()});
  materialBRDF->setNormal({_core->getResourceManager()->getTextureZero()});
  materialBRDF->setSpecular({_core->getResourceManager()->getTextureZero()});
  materialBRDF->setCoefficients(glm::vec3(1.f), glm::vec3(0.f), glm::vec3(0.f), 0.f);
  auto spriteBRDF = _core->createSprite();
  spriteBRDF->enableLighting(false);
  spriteBRDF->enableShadow(false);
  spriteBRDF->enableDepth(false);
  spriteBRDF->setMaterial(materialBRDF);
  {
    glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(5.f, 3.f, 1.f));
    spriteBRDF->setModel(model);
  }
  _core->addDrawable(spriteBRDF);

  _core->endRecording();
  _core->registerUpdate(std::bind(&update));

  _initialized = true;
}

void terminate(void) {}

bool _move = false;
void handle_input(android_app* app) {
  if (_move) {
    LOGI("Movement process");
    _core->getState()->getInput()->keyHandler(87, 0, 1, 0);
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
          _core->getState()->getInput()->cursorHandler(x, y);
          _core->getState()->getInput()->mouseHandler(0, 1, 0);
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
          _core->getState()->getInput()->keyHandler(87, 0, 0, 0);
        }
        if (pointer.id == 0) {
          // 0 - left button, 0 - release, 0 = mod, doesn't matter
          _core->getState()->getInput()->cursorHandler(x, y);
          _core->getState()->getInput()->mouseHandler(0, 0, 0);
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
            _core->getState()->getInput()->cursorHandler(x, y);
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
    auto number = ALooper_pollAll(_initialized ? 1 : 0, nullptr, &events, (void**)&source);
    if (number >= 0) {
      if (source != NULL) source->process(app, source);
    }

    handle_input(app);

    if (_initialized) _core->draw();
  } while (app->destroyRequested == 0);
}