#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include "Utility/EngineState.h"
#include "Utility/Loader.h"
#include "Primitive/Cubemap.h"

import Texture;

class ResourceManager {
 private:
  std::shared_ptr<LoaderGLTF> _loaderGLTF;
  std::shared_ptr<ImageManager> _imageManager;
  std::shared_ptr<RenderGraph::Texture> _stubTextureZero, _stubTextureOne;
  std::shared_ptr<Cubemap> _stubCubemapZero, _stubCubemapOne;
  std::shared_ptr<EngineState> _engineState;
#ifdef __ANDROID__
  AAssetManager* _assetManager;
#endif
 public:
  ResourceManager(std::shared_ptr<EngineState> engineState);
  void initialize();
#ifdef __ANDROID__
  void setAssetManager(AAssetManager* assetManager);
#endif
  ImageManager& getImageManager() { return *_imageManager; }
  std::shared_ptr<ModelGLTF> loadModel(std::string path);
  std::shared_ptr<RenderGraph::Texture> getTextureZero();
  std::shared_ptr<RenderGraph::Texture> getTextureOne();
  std::shared_ptr<Cubemap> getCubemapZero();
  std::shared_ptr<Cubemap> getCubemapOne();
};
