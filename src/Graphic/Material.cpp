#include <cstring>
#include <algorithm>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "Graphic/Material.h"

void Material::_updateAlphaCutoffDescriptors(int currentFrame) {
  std::memcpy(static_cast<std::byte*>(_uniformBufferAlphaCutoff[currentFrame]->getAllocationInfo().pMappedData),
              &_alphaCutoff, static_cast<std::size_t>(_uniformBufferAlphaCutoff[currentFrame]->getSize()));
}

void Material::_updateDescriptor(int currentFrame,
                                 MaterialTexture type,
                                 const RenderGraph::CommandBuffer& commandBuffer) {
  std::vector<RenderGraph::Texture*> textures;
  textures.reserve(_textures[type].size());
  for (const auto& texture : _textures[type]) textures.push_back(texture.get());

  for (const auto& [descriptor, info] : _descriptorsUpdate) {
    if (info.frameInFlight != currentFrame) continue;
    if (const auto binding = info.materialInfo.find(type); binding != info.materialInfo.end()) {
      descriptor->update(binding->second, textures, commandBuffer);
    }
  }
}

void Material::update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  std::unique_lock<std::mutex> lock(_mutex);
  for (auto& [material, status] : _changedTextures) {
    if (status[currentFrame]) {
      _updateDescriptor(currentFrame, material, commandBuffer);
      status[currentFrame] = false;
    }
  }
  if (_changedAlpha[currentFrame]) {
    _updateAlphaCutoffDescriptors(currentFrame);
    _changedAlpha[currentFrame] = false;
  }
  if (_changedCoefficients[currentFrame]) {
    _updateCoefficientBuffer(currentFrame);
    _changedCoefficients[currentFrame] = false;
  }
}

Material::Material(std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;

  _uniformBufferAlphaCutoff.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _uniformBufferAlphaCutoff[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(AlphaCutoff), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *engineState->getMemoryAllocator());
  _changedAlpha.resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
}

void Material::setDoubleSided(bool doubleSided) { _doubleSided = doubleSided; }

void Material::setAlphaCutoff(bool alphaMask, float alphaCutoff) {
  std::unique_lock<std::mutex> lock(_mutex);
  _alphaCutoff = {.alphaMask = alphaMask, .alphaCutoff = alphaCutoff};
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) _changedAlpha[i] = true;
}

bool Material::getDoubleSided() { return _doubleSided; }

std::vector<std::shared_ptr<RenderGraph::Buffer>> Material::getBufferCoefficients() {
  return _uniformBufferCoefficients;
}

std::vector<std::shared_ptr<RenderGraph::Buffer>> Material::getBufferAlphaCutoff() { return _uniformBufferAlphaCutoff; }

void Material::registerUpdate(std::shared_ptr<RenderGraph::DescriptorHandler> descriptor, MaterialInfoUpdate info) {
  std::unique_lock<std::mutex> lock(_mutex);
  _descriptorsUpdate[std::move(descriptor)] = std::move(info);
}

void Material::unregisterUpdate(std::shared_ptr<RenderGraph::DescriptorHandler> descriptor) {
  std::unique_lock<std::mutex> lock(_mutex);
  _descriptorsUpdate.erase(descriptor);
}

MaterialPBR::MaterialPBR(MaterialTarget target, std::shared_ptr<EngineState> engineState) : Material(engineState) {
  _target = target;
  // define coefficients
  _uniformBufferCoefficients.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _uniformBufferCoefficients[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(Coefficients), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *engineState->getMemoryAllocator());
  _changedTextures[MaterialTexture::COLOR].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedTextures[MaterialTexture::NORMAL].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedTextures[MaterialTexture::METALLIC].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedTextures[MaterialTexture::ROUGHNESS].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedTextures[MaterialTexture::OCCLUSION].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedTextures[MaterialTexture::EMISSIVE].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedTextures[MaterialTexture::IBL_DIFFUSE].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedTextures[MaterialTexture::IBL_SPECULAR].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedTextures[MaterialTexture::BRDF_SPECULAR].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedCoefficients.resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  // set default coefficients
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _updateCoefficientBuffer(i);
  }
}

void MaterialPBR::_updateCoefficientBuffer(int currentFrame) {
  std::memcpy(static_cast<std::byte*>(_uniformBufferCoefficients[currentFrame]->getAllocationInfo().pMappedData),
              &_coefficients, static_cast<std::size_t>(_uniformBufferCoefficients[currentFrame]->getSize()));
}

const std::vector<std::shared_ptr<RenderGraph::Texture>> MaterialPBR::getBaseColor() {
  return _textures[MaterialTexture::COLOR];
}

const std::vector<std::shared_ptr<RenderGraph::Texture>> MaterialPBR::getNormal() {
  return _textures[MaterialTexture::NORMAL];
}

const std::vector<std::shared_ptr<RenderGraph::Texture>> MaterialPBR::getMetallic() {
  return _textures[MaterialTexture::METALLIC];
}

const std::vector<std::shared_ptr<RenderGraph::Texture>> MaterialPBR::getRoughness() {
  return _textures[MaterialTexture::ROUGHNESS];
}

const std::vector<std::shared_ptr<RenderGraph::Texture>> MaterialPBR::getOccluded() {
  return _textures[MaterialTexture::OCCLUSION];
}

const std::vector<std::shared_ptr<RenderGraph::Texture>> MaterialPBR::getEmissive() {
  return _textures[MaterialTexture::EMISSIVE];
}

std::shared_ptr<RenderGraph::Texture> MaterialPBR::getDiffuseIBL() {
  return _textures[MaterialTexture::IBL_DIFFUSE][0];
}

std::shared_ptr<RenderGraph::Texture> MaterialPBR::getSpecularIBL() {
  return _textures[MaterialTexture::IBL_SPECULAR][0];
}

std::shared_ptr<RenderGraph::Texture> MaterialPBR::getSpecularBRDF() {
  return _textures[MaterialTexture::BRDF_SPECULAR][0];
}

void MaterialPBR::setBaseColor(std::vector<std::shared_ptr<RenderGraph::Texture>> color) {
  std::unique_lock<std::mutex> lock(_mutex);
  if (static_cast<int>(_target) != color.size())
    throw std::invalid_argument("expected size of vector is " + std::to_string(static_cast<int>(_target)));
  _textures[MaterialTexture::COLOR] = color;
  std::fill(_changedTextures[MaterialTexture::COLOR].begin(), _changedTextures[MaterialTexture::COLOR].end(), true);
}

void MaterialPBR::setNormal(std::vector<std::shared_ptr<RenderGraph::Texture>> normal) {
  std::unique_lock<std::mutex> lock(_mutex);
  if (static_cast<int>(_target) != normal.size())
    throw std::invalid_argument("expected size of vector is " + std::to_string(static_cast<int>(_target)));
  _textures[MaterialTexture::NORMAL] = normal;
  std::fill(_changedTextures[MaterialTexture::NORMAL].begin(), _changedTextures[MaterialTexture::NORMAL].end(), true);
}

void MaterialPBR::setMetallic(std::vector<std::shared_ptr<RenderGraph::Texture>> metallic) {
  std::unique_lock<std::mutex> lock(_mutex);
  if (static_cast<int>(_target) != metallic.size())
    throw std::invalid_argument("expected size of vector is " + std::to_string(static_cast<int>(_target)));
  _textures[MaterialTexture::METALLIC] = metallic;
  std::fill(_changedTextures[MaterialTexture::METALLIC].begin(), _changedTextures[MaterialTexture::METALLIC].end(),
            true);
}

void MaterialPBR::setRoughness(std::vector<std::shared_ptr<RenderGraph::Texture>> roughness) {
  std::unique_lock<std::mutex> lock(_mutex);
  if (static_cast<int>(_target) != roughness.size())
    throw std::invalid_argument("expected size of vector is " + std::to_string(static_cast<int>(_target)));
  _textures[MaterialTexture::ROUGHNESS] = roughness;
  std::fill(_changedTextures[MaterialTexture::ROUGHNESS].begin(), _changedTextures[MaterialTexture::ROUGHNESS].end(),
            true);
}

void MaterialPBR::setOccluded(std::vector<std::shared_ptr<RenderGraph::Texture>> occluded) {
  std::unique_lock<std::mutex> lock(_mutex);
  if (static_cast<int>(_target) != occluded.size())
    throw std::invalid_argument("expected size of vector is " + std::to_string(static_cast<int>(_target)));
  _textures[MaterialTexture::OCCLUSION] = occluded;
  std::fill(_changedTextures[MaterialTexture::OCCLUSION].begin(), _changedTextures[MaterialTexture::OCCLUSION].end(),
            true);
}

void MaterialPBR::setEmissive(std::vector<std::shared_ptr<RenderGraph::Texture>> emissive) {
  std::unique_lock<std::mutex> lock(_mutex);
  if (static_cast<int>(_target) != emissive.size())
    throw std::invalid_argument("expected size of vector is " + std::to_string(static_cast<int>(_target)));
  _textures[MaterialTexture::EMISSIVE] = emissive;
  std::fill(_changedTextures[MaterialTexture::EMISSIVE].begin(), _changedTextures[MaterialTexture::EMISSIVE].end(),
            true);
}

void MaterialPBR::setDiffuseIBL(std::shared_ptr<RenderGraph::Texture> diffuseIBL) {
  std::unique_lock<std::mutex> lock(_mutex);
  _textures[MaterialTexture::IBL_DIFFUSE] = {diffuseIBL};
  std::fill(_changedTextures[MaterialTexture::IBL_DIFFUSE].begin(),
            _changedTextures[MaterialTexture::IBL_DIFFUSE].end(), true);
}

void MaterialPBR::setSpecularIBL(std::shared_ptr<RenderGraph::Texture> specularIBL,
                                 std::shared_ptr<RenderGraph::Texture> specularBRDF) {
  std::unique_lock<std::mutex> lock(_mutex);
  _textures[MaterialTexture::IBL_SPECULAR] = {specularIBL};
  _textures[MaterialTexture::BRDF_SPECULAR] = {specularBRDF};
  std::fill(_changedTextures[MaterialTexture::IBL_SPECULAR].begin(),
            _changedTextures[MaterialTexture::IBL_SPECULAR].end(), true);
  std::fill(_changedTextures[MaterialTexture::BRDF_SPECULAR].begin(),
            _changedTextures[MaterialTexture::BRDF_SPECULAR].end(), true);
}

void MaterialPBR::setCoefficients(float metallicFactor,
                                  float roughnessFactor,
                                  float occlusionStrength,
                                  glm::vec3 emissiveFactor) {
  std::unique_lock<std::mutex> lock(_mutex);
  _coefficients = {.metallicFactor = metallicFactor,
                   .roughnessFactor = roughnessFactor,
                   .occlusionStrength = occlusionStrength,
                   .emissiveFactor = emissiveFactor};
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) _changedCoefficients[i] = true;
}

MaterialPhong::MaterialPhong(MaterialTarget target, std::shared_ptr<EngineState> engineState) : Material(engineState) {
  _target = target;

  _uniformBufferCoefficients.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _uniformBufferCoefficients[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(Coefficients), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *engineState->getMemoryAllocator());
  _changedTextures[MaterialTexture::COLOR].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedTextures[MaterialTexture::NORMAL].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedTextures[MaterialTexture::SPECULAR].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedCoefficients.resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  // set default coefficients
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _updateCoefficientBuffer(i);
  }
}

void MaterialPhong::_updateCoefficientBuffer(int currentFrame) {
  std::memcpy(static_cast<std::byte*>(_uniformBufferCoefficients[currentFrame]->getAllocationInfo().pMappedData),
              &_coefficients, static_cast<std::size_t>(_uniformBufferCoefficients[currentFrame]->getSize()));
}

const std::vector<std::shared_ptr<RenderGraph::Texture>> MaterialPhong::getBaseColor() {
  return _textures[MaterialTexture::COLOR];
}

const std::vector<std::shared_ptr<RenderGraph::Texture>> MaterialPhong::getNormal() {
  return _textures[MaterialTexture::NORMAL];
}

const std::vector<std::shared_ptr<RenderGraph::Texture>> MaterialPhong::getSpecular() {
  return _textures[MaterialTexture::SPECULAR];
}

void MaterialPhong::setBaseColor(std::vector<std::shared_ptr<RenderGraph::Texture>> color) {
  std::unique_lock<std::mutex> lock(_mutex);
  if (static_cast<int>(_target) != color.size())
    throw std::invalid_argument("expected size of vector is " + std::to_string(static_cast<int>(_target)));
  _textures[MaterialTexture::COLOR] = color;
  std::fill(_changedTextures[MaterialTexture::COLOR].begin(), _changedTextures[MaterialTexture::COLOR].end(), true);
}

void MaterialPhong::setSpecular(std::vector<std::shared_ptr<RenderGraph::Texture>> specular) {
  std::unique_lock<std::mutex> lock(_mutex);
  if (static_cast<int>(_target) != specular.size())
    throw std::invalid_argument("expected size of vector is " + std::to_string(static_cast<int>(_target)));
  _textures[MaterialTexture::SPECULAR] = specular;
  std::fill(_changedTextures[MaterialTexture::SPECULAR].begin(), _changedTextures[MaterialTexture::SPECULAR].end(),
            true);
}

void MaterialPhong::setNormal(std::vector<std::shared_ptr<RenderGraph::Texture>> normal) {
  std::unique_lock<std::mutex> lock(_mutex);
  if (static_cast<int>(_target) != normal.size())
    throw std::invalid_argument("expected size of vector is " + std::to_string(static_cast<int>(_target)));
  _textures[MaterialTexture::NORMAL] = normal;
  std::fill(_changedTextures[MaterialTexture::NORMAL].begin(), _changedTextures[MaterialTexture::NORMAL].end(), true);
}

void MaterialPhong::setCoefficients(glm::vec3 ambient, glm::vec3 diffuse, glm::vec3 specular, float shininess) {
  std::unique_lock<std::mutex> lock(_mutex);
  _coefficients = {._ambient = ambient, ._diffuse = diffuse, ._specular = specular, ._shininess = shininess};
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) _changedCoefficients[i] = true;
}

MaterialColor::MaterialColor(MaterialTarget target, std::shared_ptr<EngineState> engineState) : Material(engineState) {
  _target = target;
  _changedCoefficients.resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedTextures[MaterialTexture::COLOR].resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
}

void MaterialColor::_updateCoefficientBuffer(int) {}

const std::vector<std::shared_ptr<RenderGraph::Texture>> MaterialColor::getBaseColor() {
  return _textures[MaterialTexture::COLOR];
}

void MaterialColor::setBaseColor(std::vector<std::shared_ptr<RenderGraph::Texture>> color) {
  std::unique_lock<std::mutex> lock(_mutex);
  if (static_cast<int>(_target) != color.size())
    throw std::invalid_argument("expected size of vector is " + std::to_string(static_cast<int>(_target)));
  _textures[MaterialTexture::COLOR] = color;
  std::fill(_changedTextures[MaterialTexture::COLOR].begin(), _changedTextures[MaterialTexture::COLOR].end(), true);
}
