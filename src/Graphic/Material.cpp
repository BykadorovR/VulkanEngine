#include "Material.h"

void Material::_updateAlphaCutoffDescriptors(int currentFrame) {
  void* data;
  vkMapMemory(_state->getDevice()->getLogicalDevice(),
              _uniformBufferAlphaCutoff->getBuffer()[currentFrame]->getMemory(), 0, sizeof(AlphaCutoff), 0, &data);
  memcpy(data, &_alphaCutoff, sizeof(AlphaCutoff));
  vkUnmapMemory(_state->getDevice()->getLogicalDevice(),
                _uniformBufferAlphaCutoff->getBuffer()[currentFrame]->getMemory());
}

Material::Material(std::shared_ptr<CommandBuffer> commandBufferTransfer, std::shared_ptr<State> state) {
  _state = state;

  _descriptorSetLayoutCoefficients = std::make_shared<DescriptorSetLayout>(state->getDevice());
  _descriptorSetLayoutCoefficients->createUniformBuffer(VK_SHADER_STAGE_FRAGMENT_BIT);

  _descriptorSetLayoutAlphaCutoff = std::make_shared<DescriptorSetLayout>(state->getDevice());
  _descriptorSetLayoutAlphaCutoff->createUniformBuffer(VK_SHADER_STAGE_FRAGMENT_BIT);

  _descriptorSetLayoutTextures = std::make_shared<DescriptorSetLayout>(state->getDevice());

  _stubTextureOne = std::make_shared<Texture>(
      "../data/Texture1x1.png", _state->getSettings()->getLoadTextureColorFormat(), VK_SAMPLER_ADDRESS_MODE_REPEAT, 1,
      commandBufferTransfer, _state->getSettings(), _state->getDevice());
  _stubTextureZero = std::make_shared<Texture>(
      "../data/Texture1x1Black.png", _state->getSettings()->getLoadTextureColorFormat(), VK_SAMPLER_ADDRESS_MODE_REPEAT,
      1, commandBufferTransfer, _state->getSettings(), _state->getDevice());

  _descriptorSetCoefficients = std::make_shared<DescriptorSet>(state->getSettings()->getMaxFramesInFlight(),
                                                               _descriptorSetLayoutCoefficients,
                                                               state->getDescriptorPool(), state->getDevice());

  _descriptorSetAlphaCutoff = std::make_shared<DescriptorSet>(state->getSettings()->getMaxFramesInFlight(),
                                                              _descriptorSetLayoutAlphaCutoff,
                                                              state->getDescriptorPool(), state->getDevice());
  _uniformBufferAlphaCutoff = std::make_shared<UniformBuffer>(_state->getSettings()->getMaxFramesInFlight(),
                                                              sizeof(AlphaCutoff), state->getDevice());
  _descriptorSetAlphaCutoff->createUniformBuffer(_uniformBufferAlphaCutoff);

  _changedTexture.resize(_state->getSettings()->getMaxFramesInFlight(), false);
  _changedCoefficients.resize(_state->getSettings()->getMaxFramesInFlight(), false);
}

void Material::setDoubleSided(bool doubleSided) { _doubleSided = doubleSided; }

void Material::setAlphaCutoff(bool alphaCutoff, float alphaMask) {
  _alphaCutoff.alphaCutoff = alphaCutoff;
  _alphaCutoff.alphaMask = alphaCutoff;
}

bool Material::getDoubleSided() { return _doubleSided; }

std::shared_ptr<DescriptorSet> Material::getDescriptorSetCoefficients(int currentFrame) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  if (_changedCoefficients[currentFrame]) {
    _updateCoefficientDescriptors(currentFrame);
    _changedCoefficients[currentFrame] = false;
  }
  return _descriptorSetCoefficients;
}

std::shared_ptr<DescriptorSetLayout> Material::getDescriptorSetLayoutCoefficients() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _descriptorSetLayoutCoefficients;
}

std::shared_ptr<DescriptorSet> Material::getDescriptorSetTextures(int currentFrame) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  if (_changedTexture[currentFrame]) {
    _updateTextureDescriptors(currentFrame);
    _changedTexture[currentFrame] = false;
  }
  return _descriptorSetTextures;
}

std::shared_ptr<DescriptorSetLayout> Material::getDescriptorSetLayoutTextures() {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  return _descriptorSetLayoutTextures;
}

std::shared_ptr<DescriptorSet> Material::getDescriptorSetAlphaCutoff() { return _descriptorSetAlphaCutoff; }

std::shared_ptr<DescriptorSetLayout> Material::getDescriptorSetLayoutAlphaCutoff() {
  return _descriptorSetLayoutAlphaCutoff;
}

void MaterialPBR::_updateTextureDescriptors(int currentFrame) {
  std::map<int, VkDescriptorImageInfo> images;
  VkDescriptorImageInfo colorTextureInfo{};
  colorTextureInfo.imageLayout = _textureColor->getImageView()->getImage()->getImageLayout();
  colorTextureInfo.imageView = _textureColor->getImageView()->getImageView();
  colorTextureInfo.sampler = _textureColor->getSampler()->getSampler();
  images[0] = colorTextureInfo;
  VkDescriptorImageInfo normalTextureInfo{};
  normalTextureInfo.imageLayout = _textureNormal->getImageView()->getImage()->getImageLayout();
  normalTextureInfo.imageView = _textureNormal->getImageView()->getImageView();
  normalTextureInfo.sampler = _textureNormal->getSampler()->getSampler();
  images[1] = normalTextureInfo;
  VkDescriptorImageInfo metallicRoughnessTextureInfo{};
  metallicRoughnessTextureInfo.imageLayout = _textureMetallicRoughness->getImageView()->getImage()->getImageLayout();
  metallicRoughnessTextureInfo.imageView = _textureMetallicRoughness->getImageView()->getImageView();
  metallicRoughnessTextureInfo.sampler = _textureMetallicRoughness->getSampler()->getSampler();
  images[2] = metallicRoughnessTextureInfo;
  VkDescriptorImageInfo occludedTextureInfo{};
  occludedTextureInfo.imageLayout = _textureOccluded->getImageView()->getImage()->getImageLayout();
  occludedTextureInfo.imageView = _textureOccluded->getImageView()->getImageView();
  occludedTextureInfo.sampler = _textureOccluded->getSampler()->getSampler();
  images[3] = occludedTextureInfo;
  VkDescriptorImageInfo emissiveTextureInfo{};
  emissiveTextureInfo.imageLayout = _textureEmissive->getImageView()->getImage()->getImageLayout();
  emissiveTextureInfo.imageView = _textureEmissive->getImageView()->getImageView();
  emissiveTextureInfo.sampler = _textureEmissive->getSampler()->getSampler();
  images[4] = emissiveTextureInfo;

  _descriptorSetTextures->createCustom(currentFrame, {}, images);
}

MaterialPBR::MaterialPBR(std::shared_ptr<CommandBuffer> commandBufferTransfer, std::shared_ptr<State> state)
    : Material(commandBufferTransfer, state) {
  // define textures
  std::vector<VkDescriptorSetLayoutBinding> layoutTextures(5);
  layoutTextures[0].binding = 0;
  layoutTextures[0].descriptorCount = 1;
  layoutTextures[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  layoutTextures[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  layoutTextures[1].binding = 1;
  layoutTextures[1].descriptorCount = 1;
  layoutTextures[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  layoutTextures[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  layoutTextures[2].binding = 2;
  layoutTextures[2].descriptorCount = 1;
  layoutTextures[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  layoutTextures[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  layoutTextures[3].binding = 3;
  layoutTextures[3].descriptorCount = 1;
  layoutTextures[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  layoutTextures[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  layoutTextures[4].binding = 4;
  layoutTextures[4].descriptorCount = 1;
  layoutTextures[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  layoutTextures[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  _descriptorSetLayoutTextures->createCustom(layoutTextures);
  _descriptorSetTextures = std::make_shared<DescriptorSet>(state->getSettings()->getMaxFramesInFlight(),
                                                           _descriptorSetLayoutTextures, state->getDescriptorPool(),
                                                           state->getDevice());
  // define coefficients
  _uniformBufferCoefficients = std::make_shared<UniformBuffer>(_state->getSettings()->getMaxFramesInFlight(),
                                                               sizeof(Coefficients), state->getDevice());
  _descriptorSetCoefficients->createUniformBuffer(_uniformBufferCoefficients);

  // initialize with empty/default data
  _textureColor = _stubTextureOne;
  _textureNormal = _stubTextureZero;
  _textureMetallicRoughness = _stubTextureZero;
  _textureEmissive = _stubTextureZero;
  _textureOccluded = _stubTextureZero;

  // update layouts
  for (int i = 0; i < _state->getSettings()->getMaxFramesInFlight(); i++) {
    _updateTextureDescriptors(i);
    _updateCoefficientDescriptors(i);
  }
}

void MaterialPBR::_updateCoefficientDescriptors(int currentFrame) {
  void* data;
  vkMapMemory(_state->getDevice()->getLogicalDevice(),
              _uniformBufferCoefficients->getBuffer()[currentFrame]->getMemory(), 0, sizeof(Coefficients), 0, &data);
  memcpy(data, &_coefficients, sizeof(Coefficients));
  vkUnmapMemory(_state->getDevice()->getLogicalDevice(),
                _uniformBufferCoefficients->getBuffer()[currentFrame]->getMemory());
}

void MaterialPBR::setBaseColor(std::shared_ptr<Texture> color) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  _textureColor = color;
  for (int i = 0; i < _changedTexture.size(); i++) _changedTexture[i] = true;
}

void MaterialPBR::setNormal(std::shared_ptr<Texture> normal) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  _textureNormal = normal;
  for (int i = 0; i < _changedTexture.size(); i++) _changedTexture[i] = true;
}

void MaterialPBR::setMetallicRoughness(std::shared_ptr<Texture> metallicRoughness) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  _textureMetallicRoughness = metallicRoughness;
  for (int i = 0; i < _changedTexture.size(); i++) _changedTexture[i] = true;
}

void MaterialPBR::setOccluded(std::shared_ptr<Texture> occluded) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  _textureOccluded = occluded;
  for (int i = 0; i < _changedTexture.size(); i++) _changedTexture[i] = true;
}

void MaterialPBR::setEmissive(std::shared_ptr<Texture> emissive) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  _textureEmissive = emissive;
  for (int i = 0; i < _changedTexture.size(); i++) _changedTexture[i] = true;
}

void MaterialPBR::setCoefficients(float metallicFactor,
                                  float roughnessFactor,
                                  float occlusionStrength,
                                  glm::vec3 emissiveFactor) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  _coefficients.metallicFactor = metallicFactor;
  _coefficients.roughnessFactor = roughnessFactor;
  _coefficients.emissiveFactor = emissiveFactor;
  for (int i = 0; i < _changedCoefficients.size(); i++) _changedCoefficients[i] = true;
}

MaterialPhong::MaterialPhong(std::shared_ptr<CommandBuffer> commandBufferTransfer, std::shared_ptr<State> state)
    : Material(commandBufferTransfer, state) {
  std::vector<VkDescriptorSetLayoutBinding> layoutTextures(2);
  layoutTextures[0].binding = 0;
  layoutTextures[0].descriptorCount = 1;
  layoutTextures[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  layoutTextures[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  layoutTextures[1].binding = 1;
  layoutTextures[1].descriptorCount = 1;
  layoutTextures[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  layoutTextures[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  _descriptorSetLayoutTextures->createCustom(layoutTextures);

  _descriptorSetTextures = std::make_shared<DescriptorSet>(state->getSettings()->getMaxFramesInFlight(),
                                                           _descriptorSetLayoutTextures, state->getDescriptorPool(),
                                                           state->getDevice());

  _uniformBufferCoefficients = std::make_shared<UniformBuffer>(_state->getSettings()->getMaxFramesInFlight(),
                                                               sizeof(Coefficients), state->getDevice());
  _descriptorSetCoefficients->createUniformBuffer(_uniformBufferCoefficients);

  // initialize with empty/default data
  _textureColor = _stubTextureOne;
  _textureNormal = _stubTextureZero;
  for (int i = 0; i < _state->getSettings()->getMaxFramesInFlight(); i++) {
    _updateTextureDescriptors(i);
    _updateCoefficientDescriptors(i);
  }
}

void MaterialPhong::_updateCoefficientDescriptors(int currentFrame) {
  void* data;
  vkMapMemory(_state->getDevice()->getLogicalDevice(),
              _uniformBufferCoefficients->getBuffer()[currentFrame]->getMemory(), 0, sizeof(Coefficients), 0, &data);
  memcpy(data, &_coefficients, sizeof(Coefficients));
  vkUnmapMemory(_state->getDevice()->getLogicalDevice(),
                _uniformBufferCoefficients->getBuffer()[currentFrame]->getMemory());
}

void MaterialPhong::_updateTextureDescriptors(int currentFrame) {
  std::map<int, VkDescriptorImageInfo> images;
  VkDescriptorImageInfo colorTextureInfo{};
  colorTextureInfo.imageLayout = _textureColor->getImageView()->getImage()->getImageLayout();
  colorTextureInfo.imageView = _textureColor->getImageView()->getImageView();
  colorTextureInfo.sampler = _textureColor->getSampler()->getSampler();
  images[0] = colorTextureInfo;
  VkDescriptorImageInfo normalTextureInfo{};
  normalTextureInfo.imageLayout = _textureNormal->getImageView()->getImage()->getImageLayout();
  normalTextureInfo.imageView = _textureNormal->getImageView()->getImageView();
  normalTextureInfo.sampler = _textureNormal->getSampler()->getSampler();
  images[1] = normalTextureInfo;
  _descriptorSetTextures->createCustom(currentFrame, {}, images);
}

void MaterialPhong::setBaseColor(std::shared_ptr<Texture> color) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  _textureColor = color;
  for (int i = 0; i < _changedTexture.size(); i++) _changedTexture[i] = true;
}

void MaterialPhong::setNormal(std::shared_ptr<Texture> normal) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  _textureNormal = normal;
  for (int i = 0; i < _changedTexture.size(); i++) _changedTexture[i] = true;
}

void MaterialPhong::setCoefficients(glm::vec3 ambient, glm::vec3 diffuse, glm::vec3 specular, float shininess) {
  std::unique_lock<std::mutex> accessLock(_accessMutex);
  _coefficients._ambient = ambient;
  _coefficients._diffuse = diffuse;
  _coefficients._specular = specular;
  _coefficients._shininess = shininess;
  for (int i = 0; i < _changedCoefficients.size(); i++) _changedCoefficients[i] = true;
}