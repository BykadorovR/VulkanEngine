#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
#ifdef __ANDROID__
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#endif
#include "tiny_gltf.h"
#include "Utility/EngineState.h"
#include "Graphic/Buffer.h"
#include "Graphic/Material.h"
#include "Primitive/Mesh.h"

import Texture;

template <class T>
class ImageCPU {
 private:
  std::shared_ptr<T[]> _data;
  std::tuple<int, int> _resolution;
  int _channels;

 public:
  void setData(std::shared_ptr<T[]> data) { _data = data; }
  void setResolution(std::tuple<int, int> resolution) { _resolution = resolution; }
  void setChannels(int channels) { _channels = channels; }

  std::shared_ptr<T[]> getData() { return _data; }
  std::tuple<int, int> getResolution() { return _resolution; }
  int getChannels() { return _channels; }
};

class ImageManager {
 private:
  std::shared_ptr<EngineState> _engineState;

 public:
  ImageManager(std::shared_ptr<EngineState> engineState);
  std::shared_ptr<RenderGraph::Texture> createTexture(std::shared_ptr<ImageDataBuffer> data,
                                                       VkFormat format,
                                                       VkSamplerAddressMode mode,
                                                       int mipMapLevels,
                                                       VkFilter filter);
  void updateTexture(const RenderGraph::Texture& texture, const std::shared_ptr<ImageDataBuffer>& data);

  template <class T>
  std::shared_ptr<ImageCPU<T>> loadCPU(std::string path);

  // have to support vector of inputs for cubemap
  template <class T>
  std::shared_ptr<ImageDataBuffer> loadGPU(std::vector<std::shared_ptr<ImageCPU<T>>> imagesCPU) {
    auto [width, height] = imagesCPU[0]->getResolution();
    int channels = imagesCPU[0]->getChannels();
    auto imageDataBuffer = std::make_shared<ImageDataBuffer>(
        std::tuple{width, height}, channels, static_cast<int>(imagesCPU.size()), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());
    for (int i = 0; i < imagesCPU.size(); i++) {
      auto pixels = imagesCPU[i]->getData();
      VkDeviceSize imageSize = width * height * channels;
      imageDataBuffer->setData(pixels.get(), imageSize * sizeof(T), imageSize * sizeof(T) * i);
    }

    return imageDataBuffer;
  }
};

// A node represents an object in the glTF scene graph
struct NodeGLTF {
  std::shared_ptr<NodeGLTF> parent;
  // node index
  uint32_t index;
  std::vector<std::shared_ptr<NodeGLTF>> children;
  // index in MeshStatic3D vector
  int mesh = -1;

  glm::vec3 translation{};
  glm::vec3 scale{1.0f};
  glm::quat rotation{};
  // either transforms above or matrix
  glm::mat4 matrix;

  int32_t skin = -1;

  /* Get a node's local matrix from the current translation, rotation and scale values.
  These are calculated from the current animation and need to be calculated dynamically
  We multiply everything because it's either transforms are unit vectores or matrix.
  */
  glm::mat4 getLocalMatrix() {
    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) *
           matrix;
  }
};

struct MaterialGLTF {
  // PBR info
  glm::vec4 baseColorFactor = glm::vec4(1.0f);  // default [1,1,1,1]
};

struct SkinGLTF {
  std::string name;
  std::vector<glm::mat4> inverseBindMatrices;
  std::vector<std::shared_ptr<NodeGLTF>> joints;
};

struct AnimationSamplerGLTF {
  std::string interpolation;
  std::vector<float> inputs;
  std::vector<glm::vec4> outputsVec4;
};

struct AnimationChannelGLTF {
  std::string path;
  std::shared_ptr<NodeGLTF> node;
  uint32_t samplerIndex;
};

struct AnimationGLTF {
  std::string name;
  std::vector<AnimationSamplerGLTF> samplers;
  std::vector<AnimationChannelGLTF> channels;
  float start = std::numeric_limits<float>::max();
  float end = std::numeric_limits<float>::min();
  float currentTime = 0.0f;
};

class ModelGLTF {
 private:
  std::vector<std::shared_ptr<NodeGLTF>> _nodes;
  std::vector<std::shared_ptr<MaterialPhong>> _materialsPhong;
  std::vector<std::shared_ptr<MaterialColor>> _materialsColor;
  std::vector<std::shared_ptr<MaterialPBR>> _materialsPBR;
  std::vector<std::shared_ptr<SkinGLTF>> _skins;
  std::vector<std::shared_ptr<AnimationGLTF>> _animations;
  std::vector<std::shared_ptr<MeshStatic3D>> _meshes;

 public:
  void setMaterialsColor(std::vector<std::shared_ptr<MaterialColor>>& materialsColor);
  void setMaterialsPhong(std::vector<std::shared_ptr<MaterialPhong>>& materialsPhong);
  void setMaterialsPBR(std::vector<std::shared_ptr<MaterialPBR>>& materialsPBR);
  void setSkins(std::vector<std::shared_ptr<SkinGLTF>>& skins);
  void setAnimations(std::vector<std::shared_ptr<AnimationGLTF>>& animations);
  void setNodes(std::vector<std::shared_ptr<NodeGLTF>>& nodes);
  void setMeshes(std::vector<std::shared_ptr<MeshStatic3D>>& meshes);

  const std::vector<std::shared_ptr<MaterialColor>>& getMaterialsColor();
  const std::vector<std::shared_ptr<MaterialPhong>>& getMaterialsPhong();
  const std::vector<std::shared_ptr<MaterialPBR>>& getMaterialsPBR();
  const std::vector<std::shared_ptr<SkinGLTF>>& getSkins();
  const std::vector<std::shared_ptr<AnimationGLTF>>& getAnimations();
  // one mesh - one node
  const std::vector<std::shared_ptr<NodeGLTF>>& getNodes();
  const std::vector<std::shared_ptr<MeshStatic3D>>& getMeshes();
};

class LoaderGLTF {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<ImageManager> _imageManager;
  tinygltf::TinyGLTF _loader;
  std::map<std::string, std::shared_ptr<ModelGLTF>> _models;

  std::shared_ptr<RenderGraph::Texture> _loadTexture(int imageIndex,
                                                     VkFormat format,
                                                     const tinygltf::Model& modelInternal,
                                                     std::vector<std::shared_ptr<RenderGraph::Texture>>& textures);
  void _loadMaterials(const tinygltf::Model& modelInternal,
                      std::vector<std::shared_ptr<MaterialGLTF>>& materialGLTF,
                      std::shared_ptr<ModelGLTF> modelExternal);
  void _loadAnimations(const tinygltf::Model& modelInternal,
                       const std::vector<std::shared_ptr<NodeGLTF>>& nodes,
                       std::vector<std::shared_ptr<AnimationGLTF>>& animations);
  void _loadSkins(const tinygltf::Model& modelInternal,
                  const std::vector<std::shared_ptr<NodeGLTF>>& nodes,
                  std::vector<std::shared_ptr<SkinGLTF>>& skins);
  void _generateTangent(std::vector<uint32_t>& indexes, std::vector<Vertex3D>& vertices);

  std::shared_ptr<NodeGLTF> _findNode(std::shared_ptr<NodeGLTF> parent, uint32_t index);
  std::shared_ptr<NodeGLTF> _nodeFromIndex(uint32_t index, const std::vector<std::shared_ptr<NodeGLTF>>& nodes);
  void _loadNode(const tinygltf::Model& modelInternal,
                 const tinygltf::Node& input,
                 std::shared_ptr<NodeGLTF> parent,
                 uint32_t nodeIndex,
                 const std::vector<std::shared_ptr<MaterialGLTF>>& materials,
                 const std::vector<std::shared_ptr<MeshStatic3D>>& meshes,
                 std::vector<std::shared_ptr<NodeGLTF>>& nodes);

 public:
  LoaderGLTF(std::shared_ptr<ImageManager> imageManager, std::shared_ptr<EngineState> engineState);
#ifdef __ANDROID__
  void setAssetManager(AAssetManager* assetManager);
#endif
  std::shared_ptr<ModelGLTF> load(std::string path);
};
