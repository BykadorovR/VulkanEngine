#include <array>
#include <cstring>
#include <stdexcept>
#include "Graphic/LightManager.h"

import Shader;

struct ShadowParameters {
  std::array<int, 2> enabledDirectional{};
  std::array<int, 4> enabledPoint{};
  // 0 - simple, 1 - vsm
  int algorithmDirectional{};
  int algorithmPoint{};

  static constexpr std::size_t getSize() { return 2 * 16 + 4 * 16 + 4 + 4; }
};

LightManager::LightManager(std::shared_ptr<ResourceManager> resourceManager,
                           std::shared_ptr<EngineState> engineState,
                           const RenderGraph::CommandBuffer& commandBuffer) {
  _engineState = engineState;

  // update global descriptor for Phong and PBR (2 separate)
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/model/modelPhong_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/model/modelPhong_fragment.spv"));
    _descriptorSetLayoutGlobalPhong = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    _descriptorSetLayoutGlobalPhong->createCustom(shader->getDescriptorSetLayoutBindings().at(2));
  }
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/model/modelPBR_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/model/modelPBR_fragment.spv"));
    _descriptorSetLayoutGlobalPBR = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    _descriptorSetLayoutGlobalPBR->createCustom(shader->getDescriptorSetLayoutBindings().at(2));
  }
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/composition/terrainColor_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/composition/terrainPhong_fragment.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/composition/terrainColor_control.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(
        _engineState->getSettings()->getShadersPath() + "shaders/terrain/composition/terrainColor_evaluation.spv"));
    _descriptorSetLayoutGlobalTerrainPhong = std::make_shared<RenderGraph::DescriptorSetLayout>(
        *_engineState->getDevice());
    _descriptorSetLayoutGlobalTerrainPhong->createCustom(shader->getDescriptorSetLayoutBindings().at(1));
  }
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/composition/terrainColor_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/composition/terrainPBR_fragment.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/composition/terrainColor_control.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(
        _engineState->getSettings()->getShadersPath() + "shaders/terrain/composition/terrainColor_evaluation.spv"));
    _descriptorSetLayoutGlobalTerrainPBR = std::make_shared<RenderGraph::DescriptorSetLayout>(
        *_engineState->getDevice());
    _descriptorSetLayoutGlobalTerrainPBR->createCustom(shader->getDescriptorSetLayoutBindings().at(1));
  }

  _lightDirectionalSSBOViewProjection.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _lightPointSSBOViewProjection.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _lightDirectionalSSBO.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _lightPointSSBO.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _lightAmbientSSBO.resize(_engineState->getSettings()->getMaxFramesInFlight());

  {
    auto lightTmp = std::make_shared<DirectionalLight>();
    _lightDirectionalSSBOStub = std::make_shared<RenderGraph::Buffer>(
        sizeof(glm::vec4) + lightTmp->getSize(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());

    // fill only number of lights because it's stub
    int number = 0;
    std::memcpy(static_cast<std::byte*>(_lightDirectionalSSBOStub->getAllocationInfo().pMappedData), &number,
                static_cast<std::size_t>(sizeof(glm::vec4)));
  }
  {
    auto lightTmp = std::make_shared<PointLight>(_engineState);
    _lightPointSSBOStub = std::make_shared<RenderGraph::Buffer>(
        sizeof(glm::vec4) + lightTmp->getSize(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());

    // fill only number of lights because it's stub
    int number = 0;
    std::memcpy(static_cast<std::byte*>(_lightPointSSBOStub->getAllocationInfo().pMappedData), &number,
                static_cast<std::size_t>(sizeof(glm::vec4)));
  }
  {
    auto lightTmp = std::make_shared<AmbientLight>();
    _lightAmbientSSBOStub = std::make_shared<RenderGraph::Buffer>(
        sizeof(glm::vec4) + lightTmp->getSize(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());

    // fill only number of lights because it's stub
    int number = 0;
    std::memcpy(static_cast<std::byte*>(_lightAmbientSSBOStub->getAllocationInfo().pMappedData), &number,
                static_cast<std::size_t>(sizeof(glm::vec4)));
  }
  {
    _lightDirectionalSSBOViewProjectionStub = std::make_shared<RenderGraph::Buffer>(
        sizeof(glm::vec4) + sizeof(glm::mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());

    // fill only number of lights because it's stub
    int number = 0;
    std::memcpy(static_cast<std::byte*>(_lightDirectionalSSBOViewProjectionStub->getAllocationInfo().pMappedData),
                &number, static_cast<std::size_t>(sizeof(glm::vec4)));
  }
  {
    _lightPointSSBOViewProjectionStub = std::make_shared<RenderGraph::Buffer>(
        sizeof(glm::vec4) + sizeof(glm::mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());

    // fill only number of lights because it's stub
    int number = 0;
    std::memcpy(static_cast<std::byte*>(_lightPointSSBOViewProjectionStub->getAllocationInfo().pMappedData), &number,
                static_cast<std::size_t>(sizeof(glm::vec4)));
  }

  _shadowParametersBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _shadowParametersBuffer[i] = std::make_shared<RenderGraph::Buffer>(
        ShadowParameters::getSize(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());

  // stub texture
  _stubTexture = resourceManager->getTextureOne();
  _stubCubemap = resourceManager->getCubemapOne();

  _changed[LightType::DIRECTIONAL].resize(engineState->getSettings()->getMaxFramesInFlight(), false);
  _changed[LightType::POINT].resize(engineState->getSettings()->getMaxFramesInFlight(), false);
  _changed[LightType::AMBIENT].resize(engineState->getSettings()->getMaxFramesInFlight(), false);

  const auto frameCount = _engineState->getSettings()->getMaxFramesInFlight();
  _descriptorSetGlobalPhong.resize(frameCount);
  _descriptorSetGlobalPBR.resize(frameCount);
  _descriptorSetGlobalModelPhong.resize(frameCount);
  _descriptorSetGlobalModelPBR.resize(frameCount);
  _descriptorSetGlobalTerrainPhong.resize(frameCount);
  _descriptorSetGlobalTerrainPBR.resize(frameCount);

  for (int i = 0; i < frameCount; i++) {
    _reallocateDirectionalBuffers(i);
    _reallocatePointBuffers(i);
    _reallocateAmbientBuffers(i);
    _updateDirectionalBuffers(i);
    _updatePointBuffers(i);
    _updateAmbientBuffers(i);
    _updateShadowParametersBuffer(i);
    _setLightDescriptors(i, commandBuffer);
  }
}
std::shared_ptr<AmbientLight> LightManager::createAmbientLight() {
  auto light = std::make_shared<AmbientLight>();

  std::unique_lock<std::mutex> accessLock(_accessMutex);
  _ambientLights.push_back(light);

  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _changed[LightType::AMBIENT][i] = true;
  }

  return light;
}

std::vector<std::shared_ptr<AmbientLight>> LightManager::getAmbientLights() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _ambientLights;
}

void LightManager::_addGlobalResources(RenderGraph::DescriptorSet& descriptor,
                                       int currentFrame,
                                       bool includeAmbient) const {
  auto directionalViewProjection = _lightDirectionalSSBOViewProjectionStub.get();
  if (_lightDirectionalSSBOViewProjection.size() > currentFrame && _lightDirectionalSSBOViewProjection[currentFrame])
    directionalViewProjection = _lightDirectionalSSBOViewProjection[currentFrame].get();

  auto directional = _lightDirectionalSSBOStub.get();
  if (_lightDirectionalSSBO.size() > currentFrame && _lightDirectionalSSBO[currentFrame])
    directional = _lightDirectionalSSBO[currentFrame].get();

  auto point = _lightPointSSBOStub.get();
  if (_lightPointSSBO.size() > currentFrame && _lightPointSSBO[currentFrame])
    point = _lightPointSSBO[currentFrame].get();

  auto ambient = _lightAmbientSSBOStub.get();
  if (_lightAmbientSSBO.size() > currentFrame && _lightAmbientSSBO[currentFrame])
    ambient = _lightAmbientSSBO[currentFrame].get();

  std::vector<RenderGraph::Texture*> directionalTextures;
  directionalTextures.reserve(_engineState->getSettings()->getMaxDirectionalLights());
  for (int lightIndex = 0; lightIndex < _engineState->getSettings()->getMaxDirectionalLights(); ++lightIndex) {
    auto texture = _stubTexture;
    if (lightIndex < _directionalShadows.size() && _directionalShadows[lightIndex])
      texture = _directionalShadows[lightIndex]->getShadowMapTexture()[currentFrame];
    directionalTextures.push_back(texture.get());
  }

  std::vector<RenderGraph::Texture*> pointTextures;
  pointTextures.reserve(_engineState->getSettings()->getMaxPointLights());
  for (int lightIndex = 0; lightIndex < _engineState->getSettings()->getMaxPointLights(); ++lightIndex) {
    auto cubemap = _stubCubemap;
    if (lightIndex < _pointShadows.size() && _pointShadows[lightIndex])
      cubemap = _pointShadows[lightIndex]->getShadowMapCubemap()[currentFrame];
    pointTextures.push_back(cubemap->getTexture().get());
  }

  descriptor.add({directionalViewProjection});
  descriptor.add({directional});
  descriptor.add({point});
  if (includeAmbient) descriptor.add({ambient});
  descriptor.add(directionalTextures);
  descriptor.add(pointTextures);
  descriptor.add({_shadowParametersBuffer[currentFrame].get()});
}

void LightManager::_setLightDescriptors(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  const auto createDescriptor = [&](const std::shared_ptr<RenderGraph::DescriptorSetLayout>& layout,
                                    std::uint32_t firstSet, bool includeAmbient) {
    auto descriptor = std::make_shared<RenderGraph::DescriptorSet>(
        std::vector<RenderGraph::DescriptorSetLayout*>{(layout).get()}, *_engineState->getDescriptorPool(),
        *_engineState->getDevice(), firstSet);
    _addGlobalResources(*descriptor, currentFrame, includeAmbient);
    descriptor->initialize(commandBuffer);
    return descriptor;
  };

  _descriptorSetGlobalPhong[currentFrame] = createDescriptor(_descriptorSetLayoutGlobalPhong, 1, true);
  _descriptorSetGlobalPBR[currentFrame] = createDescriptor(_descriptorSetLayoutGlobalPBR, 1, false);
  _descriptorSetGlobalModelPhong[currentFrame] = createDescriptor(_descriptorSetLayoutGlobalPhong, 2, true);
  _descriptorSetGlobalModelPBR[currentFrame] = createDescriptor(_descriptorSetLayoutGlobalPBR, 2, false);
  _descriptorSetGlobalTerrainPhong[currentFrame] = createDescriptor(_descriptorSetLayoutGlobalTerrainPhong, 1, true);
  _descriptorSetGlobalTerrainPBR[currentFrame] = createDescriptor(_descriptorSetLayoutGlobalTerrainPBR, 1, false);
}

void LightManager::_reallocateAmbientBuffers(int currentFrame) {
  int size = 0;
  size += sizeof(glm::vec4);
  for (int i = 0; i < _ambientLights.size(); i++) {
    size += _ambientLights[i]->getSize();
  }

  if (!_ambientLights.empty()) {
    _lightAmbientSSBO[currentFrame] = std::make_shared<RenderGraph::Buffer>(
        size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());
  }
}

void LightManager::_updateAmbientBuffers(int currentFrame) {
  if (!_ambientLights.empty()) {
    int offset = 0;
    // we don't want to do multiple map/unmap that's why we don't use corresponding RenderGraph::Buffer method setData
    const int ambientNumber = static_cast<int>(_ambientLights.size());
    std::memcpy(static_cast<std::byte*>(_lightAmbientSSBO[currentFrame]->getAllocationInfo().pMappedData) + (offset),
                &ambientNumber, static_cast<std::size_t>(sizeof(glm::vec4)));
    offset += sizeof(glm::vec4);
    for (int i = 0; i < _ambientLights.size(); i++) {
      // we pass inverse bind matrices to shader via SSBO
      std::memcpy(static_cast<std::byte*>(_lightAmbientSSBO[currentFrame]->getAllocationInfo().pMappedData) + (offset),
                  _ambientLights[i]->getData(), static_cast<std::size_t>(_ambientLights[i]->getSize()));
      offset += _ambientLights[i]->getSize();
    }
  }
}

void LightManager::_reallocateDirectionalBuffers(int currentFrame) {
  int size = 0;
  // align is 16 bytes, so even for int because in our SSBO struct
  // we have fields 16 bytes size so the whole struct has 16 bytes allignment
  size += sizeof(glm::vec4);
  for (int i = 0; i < _directionalLights.size(); i++) {
    size += _directionalLights[i]->getSize();
  }

  if (!_directionalLights.empty()) {
    _lightDirectionalSSBO[currentFrame] = std::make_shared<RenderGraph::Buffer>(
        size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());
  }

  size = 0;
  size += sizeof(glm::vec4);
  for (int i = 0; i < _directionalLights.size(); i++) {
    size += sizeof(glm::mat4);
  }

  if (!_directionalLights.empty()) {
    _lightDirectionalSSBOViewProjection[currentFrame] = std::make_shared<RenderGraph::Buffer>(
        size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());
  }
}

void LightManager::_updateDirectionalBuffers(int currentFrame) {
  const int directionalNumber = static_cast<int>(_directionalLights.size());
  if (!_directionalLights.empty()) {
    int offset = 0;
    std::memcpy(
        static_cast<std::byte*>(_lightDirectionalSSBO[currentFrame]->getAllocationInfo().pMappedData) + (offset),
        &directionalNumber, static_cast<std::size_t>(sizeof(glm::vec4)));
    offset += sizeof(glm::vec4);
    for (int i = 0; i < _directionalLights.size(); i++) {
      // we pass inverse bind matrices to shader via SSBO
      std::memcpy(
          static_cast<std::byte*>(_lightDirectionalSSBO[currentFrame]->getAllocationInfo().pMappedData) + (offset),
          _directionalLights[i]->getData(), static_cast<std::size_t>(_directionalLights[i]->getSize()));
      offset += _directionalLights[i]->getSize();
    }
  }

  if (!_directionalLights.empty()) {
    std::vector<glm::mat4> directionalVP;
    for (int i = 0; i < _directionalLights.size(); i++) {
      glm::mat4 viewProjection = _directionalLights[i]->getCamera()->getProjection() *
                                 _directionalLights[i]->getCamera()->getView();
      directionalVP.push_back(viewProjection);
    }
    int offset = 0;
    std::memcpy(
        static_cast<std::byte*>(_lightDirectionalSSBOViewProjection[currentFrame]->getAllocationInfo().pMappedData) +
            (offset),
        &directionalNumber, static_cast<std::size_t>(sizeof(glm::vec4)));
    offset += sizeof(glm::vec4);
    std::memcpy(
        static_cast<std::byte*>(_lightDirectionalSSBOViewProjection[currentFrame]->getAllocationInfo().pMappedData) +
            (offset),
        directionalVP.data(), static_cast<std::size_t>(directionalVP.size() * sizeof(glm::mat4)));
  }
}

void LightManager::_reallocatePointBuffers(int currentFrame) {
  int size = 0;
  size += sizeof(glm::vec4);
  for (int i = 0; i < _pointLights.size(); i++) {
    size += _pointLights[i]->getSize();
  }

  if (!_pointLights.empty()) {
    _lightPointSSBO[currentFrame] = std::make_shared<RenderGraph::Buffer>(
        size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());
  }

  size = 0;
  size += sizeof(glm::vec4);
  for (int i = 0; i < _pointLights.size(); i++) {
    size += sizeof(glm::mat4);
  }

  if (!_pointLights.empty()) {
    _lightPointSSBOViewProjection[currentFrame] = std::make_shared<RenderGraph::Buffer>(
        size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());
  }
}

void LightManager::_updatePointBuffers(int currentFrame) {
  const int pointNumber = static_cast<int>(_pointLights.size());
  if (!_pointLights.empty()) {
    int offset = 0;
    std::memcpy(static_cast<std::byte*>(_lightPointSSBO[currentFrame]->getAllocationInfo().pMappedData) + (offset),
                &pointNumber, static_cast<std::size_t>(sizeof(glm::vec4)));
    offset += sizeof(glm::vec4);
    for (int i = 0; i < _pointLights.size(); i++) {
      // we pass inverse bind matrices to shader via SSBO
      std::memcpy(static_cast<std::byte*>(_lightPointSSBO[currentFrame]->getAllocationInfo().pMappedData) + (offset),
                  _pointLights[i]->getData(), static_cast<std::size_t>(_pointLights[i]->getSize()));
      offset += _pointLights[i]->getSize();
    }
  }

  if (!_pointLights.empty()) {
    std::vector<glm::mat4> pointVP;
    for (int i = 0; i < _pointLights.size(); i++) {
      glm::mat4 viewProjection = _pointLights[i]->getCamera()->getProjection() *
                                 _pointLights[i]->getCamera()->getView(0);
      pointVP.push_back(viewProjection);
    }
    int offset = 0;
    std::memcpy(static_cast<std::byte*>(_lightPointSSBOViewProjection[currentFrame]->getAllocationInfo().pMappedData) +
                    (offset),
                &pointNumber, static_cast<std::size_t>(sizeof(glm::vec4)));
    offset += sizeof(glm::vec4);
    std::memcpy(static_cast<std::byte*>(_lightPointSSBOViewProjection[currentFrame]->getAllocationInfo().pMappedData) +
                    (offset),
                pointVP.data(), static_cast<std::size_t>(pointVP.size() * sizeof(glm::mat4)));
  }
}

void LightManager::_updateShadowParametersBuffer(int currentFrame) {
  ShadowParameters shadowParameters{};
  for (std::size_t i = 0; i < shadowParameters.enabledDirectional.size(); ++i) {
    if (i < _directionalShadows.size() && _directionalShadows[i]) shadowParameters.enabledDirectional[i] = true;
  }
  for (std::size_t i = 0; i < shadowParameters.enabledPoint.size(); ++i) {
    if (i < _pointShadows.size() && _pointShadows[i]) shadowParameters.enabledPoint[i] = true;
  }
  shadowParameters.algorithmDirectional = static_cast<int>(_shadowAlgorithm[LightType::DIRECTIONAL]);
  shadowParameters.algorithmPoint = static_cast<int>(_shadowAlgorithm[LightType::POINT]);
  std::vector<uint8_t> buffer(ShadowParameters::getSize());
  std::size_t offset = 0;
  for (const int enabled : shadowParameters.enabledDirectional) {
    std::memcpy(buffer.data() + offset, &enabled, sizeof(enabled));
    offset += 16;
  }
  for (const int enabled : shadowParameters.enabledPoint) {
    std::memcpy(buffer.data() + offset, &enabled, sizeof(enabled));
    offset += 16;
  }
  std::memcpy(buffer.data() + offset, &shadowParameters.algorithmDirectional,
              sizeof(shadowParameters.algorithmDirectional));
  offset += sizeof(shadowParameters.algorithmDirectional);
  std::memcpy(buffer.data() + offset, &shadowParameters.algorithmPoint, sizeof(shadowParameters.algorithmPoint));
  std::memcpy(static_cast<std::byte*>(_shadowParametersBuffer[currentFrame]->getAllocationInfo().pMappedData),
              buffer.data(), static_cast<std::size_t>(_shadowParametersBuffer[currentFrame]->getSize()));
}

std::shared_ptr<PointLight> LightManager::createPointLight() {
  auto light = std::make_shared<PointLight>(_engineState);

  std::unique_lock<std::mutex> accessLock(_accessMutex);
  _pointLights.push_back(light);
  _pointShadows.push_back(nullptr);

  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _changed[LightType::POINT][i] = true;
  }

  return light;
}

std::shared_ptr<PointShadowResources> LightManager::createPointShadow(
    std::shared_ptr<PointLight> pointLight,
    const RenderGraph::CommandBuffer* commandBufferTransfer) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  auto shadow = std::make_shared<PointShadowResources>(commandBufferTransfer, _engineState);
  const auto lightIterator = std::find(_pointLights.begin(), _pointLights.end(), pointLight);
  if (lightIterator == _pointLights.end()) throw std::invalid_argument("Point light is not registered");
  const auto lightIndex = static_cast<std::size_t>(std::distance(_pointLights.begin(), lightIterator));
  _pointShadows.at(lightIndex) = shadow;

  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _changed[LightType::POINT][i] = true;
  }

  return shadow;
}

void LightManager::removePointLight(std::shared_ptr<PointLight> pointLight) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  const auto lightIterator = std::find(_pointLights.begin(), _pointLights.end(), pointLight);
  if (lightIterator == _pointLights.end()) return;

  const auto lightIndex = std::distance(_pointLights.begin(), lightIterator);
  _pointLights.erase(lightIterator);
  _pointShadows.erase(_pointShadows.begin() + lightIndex);
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _changed[LightType::POINT][i] = true;
  }
}

void LightManager::removePointShadow(std::shared_ptr<PointShadowResources> pointShadow) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  const auto shadowIterator = std::find(_pointShadows.begin(), _pointShadows.end(), pointShadow);
  if (shadowIterator == _pointShadows.end()) return;

  *shadowIterator = nullptr;
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _changed[LightType::POINT][i] = true;
  }
}

std::shared_ptr<DirectionalLight> LightManager::createDirectionalLight() {
  auto light = std::make_shared<DirectionalLight>();

  std::unique_lock<std::mutex> accessLock(_accessMutex);
  _directionalLights.push_back(light);
  _directionalShadows.push_back(nullptr);

  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _changed[LightType::DIRECTIONAL][i] = true;
  }

  return light;
}

std::shared_ptr<DirectionalShadowResources> LightManager::createDirectionalShadow(
    std::shared_ptr<DirectionalLight> directionalLight,
    const RenderGraph::CommandBuffer* commandBufferTransfer) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  auto shadow = std::make_shared<DirectionalShadowResources>(commandBufferTransfer, _engineState);
  const auto lightIterator = std::find(_directionalLights.begin(), _directionalLights.end(), directionalLight);
  if (lightIterator == _directionalLights.end()) throw std::invalid_argument("Directional light is not registered");
  const auto lightIndex = static_cast<std::size_t>(std::distance(_directionalLights.begin(), lightIterator));
  _directionalShadows.at(lightIndex) = shadow;

  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _changed[LightType::DIRECTIONAL][i] = true;
  }
  return shadow;
}

void LightManager::removeDirectionalLight(std::shared_ptr<DirectionalLight> directionalLight) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  const auto lightIterator = std::find(_directionalLights.begin(), _directionalLights.end(), directionalLight);
  if (lightIterator == _directionalLights.end()) return;

  const auto lightIndex = std::distance(_directionalLights.begin(), lightIterator);
  _directionalLights.erase(lightIterator);
  _directionalShadows.erase(_directionalShadows.begin() + lightIndex);
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _changed[LightType::DIRECTIONAL][i] = true;
  }
}

void LightManager::removeDirectionalShadow(std::shared_ptr<DirectionalShadowResources> directionalShadow) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  const auto shadowIterator = std::find(_directionalShadows.begin(), _directionalShadows.end(), directionalShadow);
  if (shadowIterator == _directionalShadows.end()) return;

  *shadowIterator = nullptr;
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _changed[LightType::DIRECTIONAL][i] = true;
  }
}

void LightManager::setShadowAlgorithm(LightType lightType, ShadowAlgorithm shadowAlgorithm) {
  _shadowAlgorithm[lightType] = shadowAlgorithm;
}

const std::vector<std::shared_ptr<PointLight>>& LightManager::getPointLights() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _pointLights;
}

const std::vector<std::shared_ptr<PointShadowResources>>& LightManager::getPointShadows() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _pointShadows;
}

const std::vector<std::shared_ptr<DirectionalLight>>& LightManager::getDirectionalLights() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _directionalLights;
}

const std::vector<std::shared_ptr<DirectionalShadowResources>>& LightManager::getDirectionalShadows() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _directionalShadows;
}

std::shared_ptr<RenderGraph::DescriptorSetLayout> LightManager::getDSLGlobalPhong() {
  return _descriptorSetLayoutGlobalPhong;
}
std::shared_ptr<RenderGraph::DescriptorSetLayout> LightManager::getDSLGlobalPBR() {
  return _descriptorSetLayoutGlobalPBR;
}
std::shared_ptr<RenderGraph::DescriptorSetLayout> LightManager::getDSLGlobalTerrainPhong() {
  return _descriptorSetLayoutGlobalTerrainPhong;
}
std::shared_ptr<RenderGraph::DescriptorSetLayout> LightManager::getDSLGlobalTerrainPBR() {
  return _descriptorSetLayoutGlobalTerrainPBR;
}

std::shared_ptr<RenderGraph::DescriptorSet> LightManager::getDSGlobalPhong() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _descriptorSetGlobalPhong[_engineState->getFrameInFlight()];
}

std::shared_ptr<RenderGraph::DescriptorSet> LightManager::getDSGlobalPBR() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _descriptorSetGlobalPBR[_engineState->getFrameInFlight()];
}

std::shared_ptr<RenderGraph::DescriptorSet> LightManager::getDSGlobalModelPhong() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _descriptorSetGlobalModelPhong[_engineState->getFrameInFlight()];
}

std::shared_ptr<RenderGraph::DescriptorSet> LightManager::getDSGlobalModelPBR() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _descriptorSetGlobalModelPBR[_engineState->getFrameInFlight()];
}

std::shared_ptr<RenderGraph::DescriptorSet> LightManager::getDSGlobalTerrainPhong() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _descriptorSetGlobalTerrainPhong[_engineState->getFrameInFlight()];
}

std::shared_ptr<RenderGraph::DescriptorSet> LightManager::getDSGlobalTerrainPBR() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _descriptorSetGlobalTerrainPBR[_engineState->getFrameInFlight()];
}

void LightManager::draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);

  // change lights configuration on demand only
  bool updateLightDescriptors = false;
  if (_changed[LightType::DIRECTIONAL][currentFrame]) {
    _changed[LightType::DIRECTIONAL][currentFrame] = false;
    _reallocateDirectionalBuffers(currentFrame);
    updateLightDescriptors = true;
  }

  if (_changed[LightType::POINT][currentFrame]) {
    _changed[LightType::POINT][currentFrame] = false;
    _reallocatePointBuffers(currentFrame);
    updateLightDescriptors = true;
  }

  if (_changed[LightType::AMBIENT][currentFrame]) {
    _changed[LightType::AMBIENT][currentFrame] = false;
    _reallocateAmbientBuffers(currentFrame);
    updateLightDescriptors = true;
  }

  // light parameters can be changed on per-frame basis
  _updateDirectionalBuffers(currentFrame);
  _updatePointBuffers(currentFrame);
  _updateAmbientBuffers(currentFrame);

  if (updateLightDescriptors) {
    // specifies whether shadows are enabled for some specific light or not + algorithm for shadowing.
    // changes when shadows/lights are changed
    _updateShadowParametersBuffer(currentFrame);
    _setLightDescriptors(currentFrame, commandBuffer);
  }
}

void LightManager::update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  for (const auto& pointShadow : _pointShadows) {
    if (!pointShadow) continue;
    const auto& image = pointShadow->getShadowMapCubemap().at(currentFrame)->getTexture()->getImageView().getImage();
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image.getImage(),
        .subresourceRange =
            {
                .aspectMask = image.getAspectMask(),
                .baseMipLevel = 0,
                .levelCount = static_cast<uint32_t>(image.getMipMapNumber()),
                .baseArrayLayer = 0,
                .layerCount = static_cast<uint32_t>(image.getLayerNumber()),
            },
    };
    const VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(commandBuffer.getCommandBuffer(), &dependencyInfo);
  }
}
