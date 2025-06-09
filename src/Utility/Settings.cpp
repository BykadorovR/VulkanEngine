#include "Utility/Settings.h"

void Settings::setName(std::string name) { _name = name; }

void Settings::setResolution(std::tuple<int, int> resolution) { _resolution = resolution; }

void Settings::setShadowMapResolution(std::tuple<int, int> shadowMapResolution) {
  _shadowMapResolution = shadowMapResolution;
}

void Settings::setGraphicColorFormat(VkFormat format) { _graphicColorFormat = format; }

void Settings::setLoadTextureColorFormat(VkFormat format) { _loadTextureColorFormat = format; }

void Settings::setLoadTextureAuxilaryFormat(VkFormat format) { _loadTextureAuxilaryFormat = format; }

void Settings::setSwapchainColorFormat(VkFormat format) { _swapchainColorFormat = format; }

void Settings::setDepthFormat(VkFormat format) { _depthFormat = format; }

void Settings::setMaxFramesInFlight(int maxFramesInFlight) { _maxFramesInFlight = maxFramesInFlight; }

void Settings::setAnisotropicSamples(int number) { _anisotropicSamples = number; }

void Settings::setDesiredFPS(int fps) { _desiredFPS = fps; }

void Settings::setPoolSize(int poolSizeDescriptorSets,
                           int poolSizeUBO,
                           int poolSizeSampler,
                           int poolSizeSSBO,
                           int poolSizeComputeImage) {
  _poolSizeUBO = poolSizeUBO;
  _poolSizeSampler = poolSizeSampler;
  _poolSizeSSBO = poolSizeSSBO;
  _poolSizeComputeImage = poolSizeComputeImage;
  _poolSizeDescriptorSets = poolSizeDescriptorSets;
}

void Settings::setClearColor(VkClearColorValue clearColor) { _clearColor = clearColor; }

void Settings::setThreadsInPool(int threadsInPool) { _threadsInPool = threadsInPool; }

void Settings::setAssetsPath(std::string path) { _pathAssets = path; }

void Settings::setShadersPath(std::string path) { _pathShaders = path; }

void Settings::setVerticalSync(bool verticalSync) { _verticalSync = verticalSync; }

std::string Settings::getName() { return _name; }

const std::tuple<int, int>& Settings::getResolution() { return _resolution; }

const std::tuple<int, int>& Settings::getShadowMapResolution() { return _shadowMapResolution; }

int Settings::getMaxFramesInFlight() { return _maxFramesInFlight; }

VkFormat Settings::getSwapchainColorFormat() { return _swapchainColorFormat; }

VkFormat Settings::getGraphicColorFormat() { return _graphicColorFormat; }

VkFormat Settings::getLoadTextureColorFormat() { return _loadTextureColorFormat; }

VkFormat Settings::getLoadTextureAuxilaryFormat() { return _loadTextureAuxilaryFormat; }

VkFormat Settings::getDepthFormat() { return _depthFormat; }

VkFormat Settings::getShadowMapFormat() { return _shadowMapFormat; }

int Settings::getMaxDirectionalLights() { return _maxDirectionalLights; }

int Settings::getMaxPointLights() { return _maxPointLights; }

std::vector<std::tuple<int, float>> Settings::getAttenuations() { return _attenuations; }

int Settings::getThreadsInPool() { return _threadsInPool; }

VkClearColorValue Settings::getClearColor() { return _clearColor; }

int Settings::getAnisotropicSamples() { return _anisotropicSamples; }

int Settings::getDesiredFPS() { return _desiredFPS; }

std::tuple<int, int> Settings::getDiffuseIBLResolution() { return _diffuseIBLResolution; }

std::tuple<int, int> Settings::getSpecularIBLResolution() { return _specularIBLResolution; }

int Settings::getSpecularMipMap() { return _specularIBLMipMap; }

float Settings::getDepthBiasConstant() { return _depthBiasConstant; }

float Settings::getDepthBiasSlope() { return _depthBiasSlope; }

int Settings::getPoolSizeUBO() { return _poolSizeUBO; }

int Settings::getPoolSizeSampler() { return _poolSizeSampler; }

int Settings::getPoolSizeSSBO() { return _poolSizeSSBO; }

int Settings::getPoolSizeComputeImage() { return _poolSizeComputeImage; }

int Settings::getPoolSizeDescriptorSets() { return _poolSizeDescriptorSets; }

std::string Settings::getAssetsPath() { return _pathAssets; }

std::string Settings::getShadersPath() { return _pathShaders; }

bool Settings::getVerticalSync() { return _verticalSync; }
