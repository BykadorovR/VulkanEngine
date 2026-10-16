#include <array>
#include "Utility/ResourceManager.h"

ResourceManager::ResourceManager(std::shared_ptr<EngineState> engineState) { _engineState = engineState; }

void ResourceManager::initialize() {
  _imageManager = std::make_shared<ImageManager>(_engineState);
  _loaderGLTF = std::make_shared<LoaderGLTF>(_imageManager, _engineState);
#ifdef __ANDROID__
  _loaderGLTF->setAssetManager(_assetManager);
#endif
  const std::array texturePaths{"stubs/Texture1x1.png", "stubs/Texture1x1Black.png"};
  const std::array textureTargets{&_stubTextureOne, &_stubTextureZero};
  for (std::size_t i = 0; i < texturePaths.size(); ++i) {
    auto imageData = _imageManager->loadGPU<uint8_t>({_imageManager->loadCPU<uint8_t>(texturePaths[i])});
    *textureTargets[i] =
        _imageManager->createTexture(imageData, _engineState->getSettings()->getLoadTextureColorFormat(),
                                     VK_SAMPLER_ADDRESS_MODE_REPEAT, 1, VK_FILTER_LINEAR);
  }

  _stubCubemapZero = std::make_shared<Cubemap>(
      _imageManager->loadGPU<uint8_t>({_imageManager->loadCPU<uint8_t>("stubs/Texture1x1Black.png"),
                                       _imageManager->loadCPU<uint8_t>("stubs/Texture1x1Black.png"),
                                       _imageManager->loadCPU<uint8_t>("stubs/Texture1x1Black.png"),
                                       _imageManager->loadCPU<uint8_t>("stubs/Texture1x1Black.png"),
                                       _imageManager->loadCPU<uint8_t>("stubs/Texture1x1Black.png"),
                                       _imageManager->loadCPU<uint8_t>("stubs/Texture1x1Black.png")}),
      _engineState->getSettings()->getLoadTextureColorFormat(), 1, VK_IMAGE_ASPECT_COLOR_BIT,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FILTER_LINEAR, _engineState);

  _stubCubemapOne = std::make_shared<Cubemap>(
      _imageManager->loadGPU<uint8_t>({_imageManager->loadCPU<uint8_t>("stubs/Texture1x1.png"),
                                       _imageManager->loadCPU<uint8_t>("stubs/Texture1x1.png"),
                                       _imageManager->loadCPU<uint8_t>("stubs/Texture1x1.png"),
                                       _imageManager->loadCPU<uint8_t>("stubs/Texture1x1.png"),
                                       _imageManager->loadCPU<uint8_t>("stubs/Texture1x1.png"),
                                       _imageManager->loadCPU<uint8_t>("stubs/Texture1x1.png")}),
      _engineState->getSettings()->getLoadTextureColorFormat(), 1, VK_IMAGE_ASPECT_COLOR_BIT,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FILTER_LINEAR, _engineState);
}

#ifdef __ANDROID__
void ResourceManager::setAssetManager(AAssetManager* assetManager) { _assetManager = assetManager; }
#endif

std::shared_ptr<ModelGLTF> ResourceManager::loadModel(std::string path) { return _loaderGLTF->load(path); }

std::shared_ptr<RenderGraph::Texture> ResourceManager::getTextureZero() { return _stubTextureZero; }

std::shared_ptr<RenderGraph::Texture> ResourceManager::getTextureOne() { return _stubTextureOne; }

std::shared_ptr<Cubemap> ResourceManager::getCubemapZero() { return _stubCubemapZero; }

std::shared_ptr<Cubemap> ResourceManager::getCubemapOne() { return _stubCubemapOne; }
