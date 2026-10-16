#include <cstring>
#include <iostream>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
#include "glm/gtc/type_ptr.hpp"
#include <filesystem>
#include "mikktspace.h"
#include "Utility/Loader.h"

std::shared_ptr<RenderGraph::Texture> ImageManager::createTexture(std::shared_ptr<ImageDataBuffer> data,
                                                                  VkFormat format,
                                                                  VkSamplerAddressMode mode,
                                                                  int mipMapLevels,
                                                                  VkFilter filter) {
  const auto [width, height] = data->getResolution();
  auto image = std::make_unique<RenderGraph::Image>(*_engineState->getMemoryAllocator());
  image->createImage(format, {width, height}, mipMapLevels, 1, VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

  auto imageView = std::make_shared<RenderGraph::ImageView>(std::move(image), *_engineState->getDevice());
  imageView->createImageView(VK_IMAGE_VIEW_TYPE_2D, 0, 0);
  auto sampler = std::make_shared<RenderGraph::Sampler>(*_engineState->getDevice());
  sampler->createSampler(mode, mipMapLevels, _engineState->getSettings()->getAnisotropicSamples(), filter);
  auto texture = std::make_shared<RenderGraph::Texture>(std::move(imageView), std::move(sampler));
  updateTexture(*texture, data);
  return texture;
}

void ImageManager::updateTexture(const RenderGraph::Texture& texture, const std::shared_ptr<ImageDataBuffer>& data) {
  const auto* imageBytes = static_cast<const std::byte*>(data->buffer->getAllocationInfo().pMappedData);
  const VkDeviceSize imageOffset = 0;
  _engineState->getResourceUploader()->upload(texture.getImageView().getImage(),
                                              std::span(imageBytes, static_cast<std::size_t>(data->buffer->getSize())),
                                              std::span(&imageOffset, 1));
  _engineState->getResourceUploader()->submit();
}

ImageManager::ImageManager(std::shared_ptr<EngineState> engineState) { _engineState = engineState; }

template <>
std::shared_ptr<ImageCPU<uint8_t>> ImageManager::loadCPU<uint8_t>(std::string path) {
  path = _engineState->getSettings()->getAssetsPath() + path;
  int texWidth, texHeight, texChannels;
#ifdef __ANDROID__
  std::vector<stbi_uc> fileContent = _engineState->getFilesystem()->readFile<stbi_uc>(path);
  std::shared_ptr<uint8_t[]> pixels(stbi_load_from_memory(fileContent.data(), fileContent.size(), &texWidth, &texHeight,
                                                          &texChannels, STBI_rgb_alpha));
#else
  // load texture
  std::shared_ptr<uint8_t[]> pixels(stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha),
                                    stbi_image_free);
#endif
  if (!pixels) {
    throw std::runtime_error("failed to load texture image " + path);
  }
  std::shared_ptr<ImageCPU<uint8_t>> imageCPU = std::make_shared<ImageCPU<uint8_t>>();
  imageCPU->setData(pixels);
  imageCPU->setResolution({texWidth, texHeight});
  imageCPU->setChannels(STBI_rgb_alpha);
  return imageCPU;
}

template <>
std::shared_ptr<ImageCPU<float>> ImageManager::loadCPU<float>(std::string path) {
  path = _engineState->getSettings()->getAssetsPath() + path;
  int texWidth, texHeight, texChannels;
#ifdef __ANDROID__
  std::vector<stbi_uc> fileContent = _engineState->getFilesystem()->readFile<stbi_uc>(path);
  std::shared_ptr<float[]> pixels(stbi_loadf_from_memory(fileContent.data(), fileContent.size(), &texWidth, &texHeight,
                                                         &texChannels, STBI_rgb_alpha));
#else
  // load texture
  std::shared_ptr<float[]> pixels(stbi_loadf(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha),
                                  stbi_image_free);
#endif
  if (!pixels) {
    throw std::runtime_error("failed to load texture image " + path);
  }

  std::shared_ptr<ImageCPU<float>> imageCPU = std::make_shared<ImageCPU<float>>();
  imageCPU->setData(pixels);
  imageCPU->setResolution({texWidth, texHeight});
  imageCPU->setChannels(STBI_rgb_alpha);
  return imageCPU;
}

void ModelGLTF::setMaterialsColor(std::vector<std::shared_ptr<MaterialColor>>& materialsColor) {
  _materialsColor = materialsColor;
}

void ModelGLTF::setMaterialsPhong(std::vector<std::shared_ptr<MaterialPhong>>& materialsPhong) {
  _materialsPhong = materialsPhong;
}

void ModelGLTF::setMaterialsPBR(std::vector<std::shared_ptr<MaterialPBR>>& materialsPBR) {
  _materialsPBR = materialsPBR;
}

void ModelGLTF::setSkins(std::vector<std::shared_ptr<SkinGLTF>>& skins) { _skins = skins; }

void ModelGLTF::setAnimations(std::vector<std::shared_ptr<AnimationGLTF>>& animations) { _animations = animations; }

void ModelGLTF::setNodes(std::vector<std::shared_ptr<NodeGLTF>>& nodes) { _nodes = nodes; }

void ModelGLTF::setMeshes(std::vector<std::shared_ptr<MeshStatic3D>>& meshes) { _meshes = meshes; }

const std::vector<std::shared_ptr<MaterialColor>>& ModelGLTF::getMaterialsColor() { return _materialsColor; }

const std::vector<std::shared_ptr<MaterialPhong>>& ModelGLTF::getMaterialsPhong() { return _materialsPhong; }

const std::vector<std::shared_ptr<MaterialPBR>>& ModelGLTF::getMaterialsPBR() { return _materialsPBR; }

const std::vector<std::shared_ptr<SkinGLTF>>& ModelGLTF::getSkins() { return _skins; }

const std::vector<std::shared_ptr<AnimationGLTF>>& ModelGLTF::getAnimations() { return _animations; }
// one mesh - one node
const std::vector<std::shared_ptr<NodeGLTF>>& ModelGLTF::getNodes() { return _nodes; }

const std::vector<std::shared_ptr<MeshStatic3D>>& ModelGLTF::getMeshes() { return _meshes; }

LoaderGLTF::LoaderGLTF(std::shared_ptr<ImageManager> imageManager, std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  _imageManager = imageManager;
}

#ifdef __ANDROID__
void LoaderGLTF::setAssetManager(AAssetManager* assetManager) { tinygltf::asset_manager = assetManager; }
#endif

std::shared_ptr<ModelGLTF> LoaderGLTF::load(std::string path) {
  path = _engineState->getSettings()->getAssetsPath() + path;

  if (_models.contains(path) == false) {
    std::shared_ptr<ModelGLTF> modelExternal = std::make_shared<ModelGLTF>();
    std::vector<std::shared_ptr<NodeGLTF>> nodes;
    std::vector<std::shared_ptr<MaterialGLTF>> materials;
    std::vector<std::shared_ptr<SkinGLTF>> skins;
    std::vector<std::shared_ptr<AnimationGLTF>> animations;
    std::vector<std::shared_ptr<MeshStatic3D>> meshes;
    tinygltf::Model modelInternal;
    std::string err, warn;
    bool loaded = false;
    std::string extension = path.substr(path.find_last_of(".") + 1);
    if (extension == "gltf")
      loaded = _loader.LoadASCIIFromFile(&modelInternal, &err, &warn, path);
    else if (extension == "glb")
      loaded = _loader.LoadBinaryFromFile(&modelInternal, &err, &warn, path);
    if (loaded == false) throw std::runtime_error("Can't load model: " + path);

    // allocate meshes
    for (int i = 0; i < modelInternal.meshes.size(); i++) {
      auto mesh = std::make_shared<MeshStatic3D>(_engineState);
      meshes.push_back(mesh);
    }
    // load material
    _loadMaterials(modelInternal, materials, modelExternal);
    // load nodes
    const tinygltf::Scene& scene = modelInternal.scenes[0];
    for (size_t i = 0; i < scene.nodes.size(); i++) {
      tinygltf::Node& node = modelInternal.nodes[scene.nodes[i]];
      _loadNode(modelInternal, node, nullptr, scene.nodes[i], materials, meshes, nodes);
    }
    // load bones/joints
    _loadSkins(modelInternal, nodes, skins);
    // load animations
    _loadAnimations(modelInternal, nodes, animations);

    modelExternal->setAnimations(animations);
    modelExternal->setMeshes(meshes);
    modelExternal->setNodes(nodes);
    modelExternal->setSkins(skins);
    _models[path] = modelExternal;
  }

  return _models[path];
}

void LoaderGLTF::_generateTangent(std::vector<uint32_t>& indexes, std::vector<Vertex3D>& vertices) {
  struct VertexIndex {
    std::vector<uint32_t>& indexes;
    std::vector<Vertex3D>& vertices;
  };

  SMikkTSpaceInterface interface{};
  interface.m_getNormal = [](SMikkTSpaceContext const* context, float normalOutput[], const int faceNum,
                             const int vertNum) {
    VertexIndex& vertexIndex = *reinterpret_cast<VertexIndex*>(context->m_pUserData);
    glm::vec3 normal{0.0f};
    if (!vertexIndex.indexes.empty()) {
      const uint32_t index = vertexIndex.indexes[faceNum * 3 + vertNum];
      if (index < vertexIndex.vertices.size()) {
        normal = vertexIndex.vertices[index].normal;
      }
    } else {
      normal = vertexIndex.vertices[faceNum * 3 + vertNum].normal;
    }

    normalOutput[0] = normal.x;
    normalOutput[1] = normal.y;
    normalOutput[2] = normal.z;
  };

  interface.m_getNumFaces = [](SMikkTSpaceContext const* context) -> int {
    VertexIndex& vertexIndex = *reinterpret_cast<VertexIndex*>(context->m_pUserData);
    if (!vertexIndex.indexes.empty()) return static_cast<int>(vertexIndex.indexes.size() / 3);
    return static_cast<int>(vertexIndex.vertices.size() / 3);
  };

  interface.m_getNumVerticesOfFace = [](SMikkTSpaceContext const*, int) -> int { return 3; };
  interface.m_getPosition = [](SMikkTSpaceContext const* context, float positionOutput[], const int faceNum,
                               const int vertNum) {
    VertexIndex& vertexIndex = *reinterpret_cast<VertexIndex*>(context->m_pUserData);
    glm::vec3 position{0.0f};
    if (!vertexIndex.indexes.empty()) {
      const uint32_t index = vertexIndex.indexes[faceNum * 3 + vertNum];
      if (index < vertexIndex.vertices.size()) {
        position = vertexIndex.vertices[index].pos;
      }
    } else {
      position = vertexIndex.vertices[faceNum * 3 + vertNum].pos;
    }

    positionOutput[0] = position.x;
    positionOutput[1] = position.y;
    positionOutput[2] = position.z;
  };
  interface.m_getTexCoord = [](SMikkTSpaceContext const* context, float textureCoordinateOutput[], const int faceNum,
                               const int vertNum) {
    VertexIndex& vertexIndex = *reinterpret_cast<VertexIndex*>(context->m_pUserData);
    glm::vec2 textureCoordinate{0.0f};
    if (!vertexIndex.indexes.empty()) {
      const uint32_t index = vertexIndex.indexes[faceNum * 3 + vertNum];
      if (index < vertexIndex.vertices.size()) {
        textureCoordinate = vertexIndex.vertices[index].texCoord;
      }
    } else {
      textureCoordinate = vertexIndex.vertices[faceNum * 3 + vertNum].texCoord;
    }

    textureCoordinateOutput[0] = textureCoordinate.x;
    textureCoordinateOutput[1] = textureCoordinate.y;
  };
  interface.m_setTSpaceBasic = [](SMikkTSpaceContext const* context, const float tangent[], const float sign,
                                  const int faceNum, const int vertNum) {
    VertexIndex& vertexIndex = *reinterpret_cast<VertexIndex*>(context->m_pUserData);
    Vertex3D* vertex = nullptr;
    if (!vertexIndex.indexes.empty()) {
      const uint32_t index = vertexIndex.indexes[faceNum * 3 + vertNum];
      if (index < vertexIndex.vertices.size()) {
        vertex = &vertexIndex.vertices[index];
      }
    } else {
      vertex = &vertexIndex.vertices[faceNum * 3 + vertNum];
    }
    if (vertex == nullptr) return;

    vertex->tangent = glm::vec4(glm::make_vec3(tangent), sign);
  };

  VertexIndex vertexIndexData{indexes, vertices};
  SMikkTSpaceContext context{.m_pInterface = &interface, .m_pUserData = &vertexIndexData};

  if (!genTangSpaceDefault(&context)) std::cout << "Can't generate tangent vectors";
}

// load all textures here
std::shared_ptr<RenderGraph::Texture> LoaderGLTF::_loadTexture(
    int imageIndex,
    VkFormat format,
    const tinygltf::Model& modelInternal,
    std::vector<std::shared_ptr<RenderGraph::Texture>>& textures) {
  std::shared_ptr<RenderGraph::Texture> texture = textures[imageIndex];
  if (texture == nullptr) {
    const tinygltf::Image glTFImage = modelInternal.images[imageIndex];
    auto filePath = glTFImage.uri;
    if (std::filesystem::exists(filePath)) {
      auto imageCPU = _imageManager->loadCPU<uint8_t>(filePath);
      auto imageData = _imageManager->loadGPU<uint8_t>({imageCPU});
      texture = _imageManager->createTexture(imageData, format, VK_SAMPLER_ADDRESS_MODE_REPEAT, 1, VK_FILTER_LINEAR);
    } else {
      // Get the image data from the glTF loader
      const unsigned char* buffer = nullptr;
      std::size_t bufferSize = 0;
      std::vector<unsigned char> convertedBuffer;
      // Most devices do not support RGB-only formats, so expand them to RGBA.
      if (glTFImage.component == 3) {
        const auto pixelCount = static_cast<std::size_t>(glTFImage.width) * static_cast<std::size_t>(glTFImage.height);
        convertedBuffer.resize(pixelCount * 4);
        buffer = convertedBuffer.data();
        unsigned char* rgba = convertedBuffer.data();
        const unsigned char* rgb = glTFImage.image.data();
        for (std::size_t i = 0; i < pixelCount; ++i) {
          std::memcpy(rgba, rgb, sizeof(unsigned char) * 3);
          rgba += 4;
          rgb += 3;
        }
      } else {
        buffer = glTFImage.image.data();
        bufferSize = glTFImage.image.size();
      }

      // copy buffer to RenderGraph::Texture
      auto stagingBuffer = std::make_shared<RenderGraph::Buffer>(
          bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
          *_engineState->getMemoryAllocator());

      std::memcpy(static_cast<std::byte*>(stagingBuffer->getAllocationInfo().pMappedData), buffer,
                  static_cast<std::size_t>(stagingBuffer->getSize()));

      // for some textures SRGB is used but for others linear format
      auto image = std::make_unique<RenderGraph::Image>(*_engineState->getMemoryAllocator());
      image->createImage(format, {glTFImage.width, glTFImage.height}, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

      const VkDeviceSize imageOffset = 0;
      const auto* imageBytes = static_cast<const std::byte*>(stagingBuffer->getAllocationInfo().pMappedData);
      _engineState->getResourceUploader()->upload(
          *image, std::span(imageBytes, static_cast<std::size_t>(stagingBuffer->getSize())),
          std::span(&imageOffset, 1));
      _engineState->getResourceUploader()->submit();

      auto imageView = std::make_shared<RenderGraph::ImageView>(std::move(image), *_engineState->getDevice());
      imageView->createImageView(VK_IMAGE_VIEW_TYPE_2D, 0, 0);
      auto sampler = std::make_shared<RenderGraph::Sampler>(*_engineState->getDevice());
      sampler->createSampler(VK_SAMPLER_ADDRESS_MODE_REPEAT, 1, _engineState->getSettings()->getAnisotropicSamples(),
                             VK_FILTER_LINEAR);
      texture = std::make_shared<RenderGraph::Texture>(std::move(imageView), std::move(sampler));
    }
    textures[imageIndex] = texture;
  }
  return texture;
}

// TODO: we can store baseColorFactor only in GLTF material, rest will go to PBR/Phong
void LoaderGLTF::_loadMaterials(const tinygltf::Model& modelInternal,
                                std::vector<std::shared_ptr<MaterialGLTF>>& materialGLTF,
                                std::shared_ptr<ModelGLTF> modelExternal) {
  std::vector<std::shared_ptr<MaterialPBR>> materialsPBR;
  std::vector<std::shared_ptr<MaterialPhong>> materialsPhong;
  std::vector<std::shared_ptr<MaterialColor>> materialsColor;
  std::vector<std::shared_ptr<RenderGraph::Texture>> textures(modelInternal.images.size(), nullptr);
  for (size_t i = 0; i < modelInternal.materials.size(); i++) {
    const tinygltf::Material& glTFMaterial = modelInternal.materials[i];
    auto materialPhong = std::make_shared<MaterialPhong>(MaterialTarget::SIMPLE, _engineState);
    auto materialPBR = std::make_shared<MaterialPBR>(MaterialTarget::SIMPLE, _engineState);
    auto materialColor = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, _engineState);
    auto material = std::make_shared<MaterialGLTF>();
    // Get the base color factor
    const auto& baseColorFactor = glTFMaterial.pbrMetallicRoughness.baseColorFactor;
    material->baseColorFactor = {static_cast<float>(baseColorFactor[0]), static_cast<float>(baseColorFactor[1]),
                                 static_cast<float>(baseColorFactor[2]), static_cast<float>(baseColorFactor[3])};
    const float metallicFactor = static_cast<float>(glTFMaterial.pbrMetallicRoughness.metallicFactor);
    const float roughnessFactor = static_cast<float>(glTFMaterial.pbrMetallicRoughness.roughnessFactor);
    const float occlusionStrength = static_cast<float>(glTFMaterial.occlusionTexture.strength);
    const auto& glTFEmissiveFactor = glTFMaterial.emissiveFactor;
    const glm::vec3 emissiveFactor{static_cast<float>(glTFEmissiveFactor[0]), static_cast<float>(glTFEmissiveFactor[1]),
                                   static_cast<float>(glTFEmissiveFactor[2])};
    materialPBR->setCoefficients(metallicFactor, roughnessFactor, occlusionStrength, emissiveFactor);

    // Get double side propery
    materialPBR->setDoubleSided(glTFMaterial.doubleSided);
    materialPhong->setDoubleSided(glTFMaterial.doubleSided);
    // Get alpha cut off information
    const float alphaCutoff = static_cast<float>(glTFMaterial.alphaCutoff);
    materialPBR->setAlphaCutoff(glTFMaterial.alphaMode == "MASK", alphaCutoff);
    materialPhong->setAlphaCutoff(glTFMaterial.alphaMode == "MASK", alphaCutoff);
    // Get base color texture
    {
      // glTF texture index
      auto baseColorTextureIndex = glTFMaterial.pbrMetallicRoughness.baseColorTexture.index;
      if (baseColorTextureIndex >= 0) {
        // glTF image index
        auto baseColorImageIndex = modelInternal.textures[baseColorTextureIndex].source;
        // set texture to phong material
        materialPhong->setBaseColor({_loadTexture(
            baseColorImageIndex, _engineState->getSettings()->getLoadTextureColorFormat(), modelInternal, textures)});
        // set texture to PBR material
        materialPBR->setBaseColor({_loadTexture(
            baseColorImageIndex, _engineState->getSettings()->getLoadTextureColorFormat(), modelInternal, textures)});
        materialColor->setBaseColor({_loadTexture(
            baseColorImageIndex, _engineState->getSettings()->getLoadTextureColorFormat(), modelInternal, textures)});
      }
    }
    // Get normal texture
    {
      auto normalTextureIndex = glTFMaterial.normalTexture.index;
      if (normalTextureIndex >= 0) {
        // glTF image index
        auto normalImageIndex = modelInternal.textures[normalTextureIndex].source;
        // set normal texture to phong material
        materialPhong->setNormal({_loadTexture(
            normalImageIndex, _engineState->getSettings()->getLoadTextureAuxilaryFormat(), modelInternal, textures)});
        // set normal texture to PBR material
        materialPBR->setNormal({_loadTexture(
            normalImageIndex, _engineState->getSettings()->getLoadTextureAuxilaryFormat(), modelInternal, textures)});
      }
    }
    // Get metallic-roughness texture
    {
      auto metallicRoughnessTextureIndex = glTFMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index;
      if (metallicRoughnessTextureIndex >= 0) {
        // glTF image index
        auto metallicRoughnessImageIndex = modelInternal.textures[metallicRoughnessTextureIndex].source;
        // set specular texture to Phong material
        materialPhong->setSpecular(
            {_loadTexture(metallicRoughnessImageIndex, _engineState->getSettings()->getLoadTextureAuxilaryFormat(),
                          modelInternal, textures)});
        // set metallic texture to PBR material
        materialPBR->setMetallic(
            {_loadTexture(metallicRoughnessImageIndex, _engineState->getSettings()->getLoadTextureAuxilaryFormat(),
                          modelInternal, textures)});
        // set roughness texture to PBR material
        materialPBR->setRoughness(
            {_loadTexture(metallicRoughnessImageIndex, _engineState->getSettings()->getLoadTextureAuxilaryFormat(),
                          modelInternal, textures)});
      }
    }
    // Get occlusion texture
    {
      auto occlusionTextureIndex = glTFMaterial.occlusionTexture.index;
      if (occlusionTextureIndex >= 0) {
        auto occlusionImageIndex = modelInternal.textures[occlusionTextureIndex].source;
        materialPBR->setOccluded(
            {_loadTexture(occlusionImageIndex, _engineState->getSettings()->getLoadTextureAuxilaryFormat(),
                          modelInternal, textures)});
      }
    }
    // Get emissive texture
    {
      auto emissiveTextureIndex = glTFMaterial.emissiveTexture.index;
      if (emissiveTextureIndex >= 0) {
        auto emissiveImageIndex = modelInternal.textures[emissiveTextureIndex].source;
        materialPBR->setEmissive({_loadTexture(
            emissiveImageIndex, _engineState->getSettings()->getLoadTextureColorFormat(), modelInternal, textures)});
      }
    }

    materialGLTF.push_back(material);
    materialsPhong.push_back(materialPhong);
    materialsPBR.push_back(materialPBR);
    materialsColor.push_back(materialColor);
  }
  if (materialsPBR.size() > 0) modelExternal->setMaterialsPBR(materialsPBR);
  if (materialsPhong.size() > 0) modelExternal->setMaterialsPhong(materialsPhong);
  if (materialsColor.size() > 0) modelExternal->setMaterialsColor(materialsColor);
}

void LoaderGLTF::_loadNode(const tinygltf::Model& modelInternal,
                           const tinygltf::Node& input,
                           std::shared_ptr<NodeGLTF> parent,
                           uint32_t nodeIndex,
                           const std::vector<std::shared_ptr<MaterialGLTF>>& materials,
                           const std::vector<std::shared_ptr<MeshStatic3D>>& meshes,
                           std::vector<std::shared_ptr<NodeGLTF>>& nodes) {
  std::shared_ptr<NodeGLTF> node = std::make_shared<NodeGLTF>();
  node->parent = parent;
  node->matrix = glm::mat4(1.f);
  node->index = nodeIndex;
  node->skin = input.skin;
  node->mesh = input.mesh;

  // Get the local node matrix
  // It's either made up from translation, rotation, scale or a 4x4 matrix
  if (input.translation.size() == 3) {
    node->translation = glm::make_vec3(input.translation.data());
  }
  if (input.rotation.size() == 4) {
    glm::quat q = glm::make_quat(input.rotation.data());
    node->rotation = glm::mat4(q);
  }
  if (input.scale.size() == 3) {
    node->scale = glm::make_vec3(input.scale.data());
  }

  if (input.matrix.size() == 16) {
    node->matrix = glm::make_mat4x4(input.matrix.data());
  };

  // Load node's children
  if (input.children.size() > 0) {
    for (size_t i = 0; i < input.children.size(); i++) {
      _loadNode(modelInternal, modelInternal.nodes[input.children[i]], node, input.children[i], materials, meshes,
                nodes);
    }
  }

  // we have to calculate translate-scale-rotate matrices here
  glm::mat4 nodeMatrix = node->getLocalMatrix();
  std::shared_ptr<NodeGLTF> currentParent = node->parent;
  while (currentParent) {
    nodeMatrix = currentParent->getLocalMatrix() * nodeMatrix;
    currentParent = currentParent->parent;
  }

  // If the node contains mesh data, we load vertices and indices from the buffers
  // In glTF this is done via accessors and buffer views
  if (input.mesh > -1) {
    // GLTF mesh
    const tinygltf::Mesh meshGLTF = modelInternal.meshes[input.mesh];
    // internal engine mesh
    auto mesh = meshes[input.mesh];
    std::vector<uint32_t> indexes;
    std::vector<Vertex3D> vertices;
    std::shared_ptr<AABB> aabb = std::make_shared<AABB>();
    bool generateTangent = true;
    // Iterate through all primitives of this node's mesh
    for (size_t i = 0; i < meshGLTF.primitives.size(); i++) {
      const tinygltf::Primitive& glTFPrimitive = meshGLTF.primitives[i];
      const uint32_t firstIndex = static_cast<uint32_t>(indexes.size());
      const uint32_t vertexStart = static_cast<uint32_t>(vertices.size());
      uint32_t indexCount = 0;
      bool hasSkin = false;
      // Vertices
      {
        const float* positionBuffer = nullptr;
        const float* normalsBuffer = nullptr;
        const float* texCoordsBuffer = nullptr;
        const void* jointIndicesBuffer = nullptr;
        const float* jointWeightsBuffer = nullptr;
        const float* tangentsBuffer = nullptr;
        size_t vertexCount = 0;

        int jointByteStride = 0;
        int jointComponentType = 0;
        int positionByteStride = 0;
        int normalByteStride = 0;
        int uv0ByteStride = 0;
        int weightByteStride = 0;
        int tangentByteStride = 0;
        // Get buffer data for vertex positions
        if (glTFPrimitive.attributes.find("POSITION") != glTFPrimitive.attributes.end()) {
          const tinygltf::Accessor& accessor =
              modelInternal.accessors[glTFPrimitive.attributes.find("POSITION")->second];
          glm::vec4 tempMin = {accessor.minValues[0], accessor.minValues[1], accessor.minValues[2], 1.f};
          glm::vec4 tempMax = {accessor.maxValues[0], accessor.maxValues[1], accessor.maxValues[2], 1.f};
          tempMin = nodeMatrix * tempMin;
          tempMax = nodeMatrix * tempMax;
          aabb->extend(glm::vec3(tempMin.x, tempMin.y, tempMin.z));
          aabb->extend(glm::vec3(tempMax.x, tempMax.y, tempMax.z));

          const tinygltf::BufferView& view = modelInternal.bufferViews[accessor.bufferView];
          positionBuffer = reinterpret_cast<const float*>(
              &(modelInternal.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
          vertexCount = accessor.count;
          positionByteStride = accessor.ByteStride(view) ? (accessor.ByteStride(view) / sizeof(float))
                                                         : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
        }
        // Get buffer data for vertex normals
        if (glTFPrimitive.attributes.find("NORMAL") != glTFPrimitive.attributes.end()) {
          const tinygltf::Accessor& accessor = modelInternal.accessors[glTFPrimitive.attributes.find("NORMAL")->second];
          const tinygltf::BufferView& view = modelInternal.bufferViews[accessor.bufferView];
          normalsBuffer = reinterpret_cast<const float*>(
              &(modelInternal.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
          normalByteStride = accessor.ByteStride(view) ? (accessor.ByteStride(view) / sizeof(float))
                                                       : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
        }
        // Get buffer data for vertex texture coordinates
        // glTF supports multiple sets, we only load the first one
        if (glTFPrimitive.attributes.find("TEXCOORD_0") != glTFPrimitive.attributes.end()) {
          const tinygltf::Accessor& accessor =
              modelInternal.accessors[glTFPrimitive.attributes.find("TEXCOORD_0")->second];
          const tinygltf::BufferView& view = modelInternal.bufferViews[accessor.bufferView];
          texCoordsBuffer = reinterpret_cast<const float*>(
              &(modelInternal.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
          uv0ByteStride = accessor.ByteStride(view) ? (accessor.ByteStride(view) / sizeof(float))
                                                    : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
        }

        if (glTFPrimitive.attributes.find("TANGENT") != glTFPrimitive.attributes.end()) {
          const tinygltf::Accessor& accessor =
              modelInternal.accessors[glTFPrimitive.attributes.find("TANGENT")->second];
          const tinygltf::BufferView& view = modelInternal.bufferViews[accessor.bufferView];
          tangentsBuffer = reinterpret_cast<const float*>(
              &(modelInternal.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
          tangentByteStride = accessor.ByteStride(view) ? (accessor.ByteStride(view) / sizeof(float))
                                                        : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
        }

        // Get vertex joint indices
        if (glTFPrimitive.attributes.find("JOINTS_0") != glTFPrimitive.attributes.end()) {
          const tinygltf::Accessor& accessor =
              modelInternal.accessors[glTFPrimitive.attributes.find("JOINTS_0")->second];
          const tinygltf::BufferView& view = modelInternal.bufferViews[accessor.bufferView];
          jointIndicesBuffer = &(modelInternal.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]);
          jointComponentType = accessor.componentType;
          jointByteStride = accessor.ByteStride(view)
                                ? (accessor.ByteStride(view) / tinygltf::GetComponentSizeInBytes(jointComponentType))
                                : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
        }
        // Get vertex joint weights
        if (glTFPrimitive.attributes.find("WEIGHTS_0") != glTFPrimitive.attributes.end()) {
          const tinygltf::Accessor& accessor =
              modelInternal.accessors[glTFPrimitive.attributes.find("WEIGHTS_0")->second];
          const tinygltf::BufferView& view = modelInternal.bufferViews[accessor.bufferView];
          jointWeightsBuffer = reinterpret_cast<const float*>(
              &(modelInternal.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
          weightByteStride = accessor.ByteStride(view) ? (accessor.ByteStride(view) / sizeof(float))
                                                       : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
        }

        hasSkin = (jointIndicesBuffer && jointWeightsBuffer);

        // Append data to model's vertex buffer
        for (size_t v = 0; v < vertexCount; v++) {
          Vertex3D vertex{};
          vertex.pos = glm::vec4(glm::make_vec3(&positionBuffer[v * positionByteStride]), 1.0f);
          // default value
          vertex.normal = glm::vec3(0.f, 0.f, 1.f);
          if (normalsBuffer) {
            vertex.normal = glm::normalize(glm::vec3(glm::make_vec3(&normalsBuffer[v * normalByteStride])));
          }
          vertex.texCoord = texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[v * uv0ByteStride]) : glm::vec3(0.0f);
          vertex.jointIndices = glm::vec4(0.0f);
          vertex.jointWeights = glm::vec4(0.0f);
          if (hasSkin) {
            vertex.jointWeights = glm::make_vec4(&jointWeightsBuffer[v * weightByteStride]);
            switch (jointComponentType) {
              case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                const uint16_t* buf = static_cast<const uint16_t*>(jointIndicesBuffer);
                vertex.jointIndices = glm::vec4(glm::make_vec4(&buf[v * jointByteStride]));
                break;
              }
              case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                const uint8_t* buf = static_cast<const uint8_t*>(jointIndicesBuffer);
                vertex.jointIndices = glm::vec4(glm::make_vec4(&buf[v * jointByteStride]));
                break;
              }
              default:
                // Not supported by spec
                std::cerr << "Joint component type " << jointComponentType << " not supported!" << std::endl;
                break;
            }
            // prepare AABB for every joint
            const auto extendAABBJoints = [&](float weight, float index) {
              if (weight > 0) {
                const int jointIndex = static_cast<int>(index);
                auto aabbJoints = mesh->getAABBJoints();
                if (!aabbJoints.contains(jointIndex)) aabbJoints[jointIndex] = std::make_shared<AABB>();
                aabbJoints[jointIndex]->extend(vertex.pos);
                mesh->setAABBJoints(aabbJoints);
              }
            };

            extendAABBJoints(vertex.jointWeights.x, vertex.jointIndices.x);
            extendAABBJoints(vertex.jointWeights.y, vertex.jointIndices.y);
            extendAABBJoints(vertex.jointWeights.z, vertex.jointIndices.z);
            extendAABBJoints(vertex.jointWeights.w, vertex.jointIndices.w);
          }
          vertex.color = glm::vec3(1.f);
          if (materials.size() > glTFPrimitive.material)
            vertex.color = materials[glTFPrimitive.material]->baseColorFactor;

          vertex.tangent = glm::vec4(0.0f);
          if (tangentsBuffer) {
            vertex.tangent = glm::make_vec4(&tangentsBuffer[v * tangentByteStride]);
            generateTangent = false;
          }

          vertices.push_back(vertex);
        }
      }
      // Indices
      {
        const tinygltf::Accessor& accessor = modelInternal.accessors[glTFPrimitive.indices];
        const tinygltf::BufferView& bufferView = modelInternal.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = modelInternal.buffers[bufferView.buffer];

        indexCount += static_cast<uint32_t>(accessor.count);

        // glTF supports different component types of indices
        // TODO: check + vertexStart, what is it
        switch (accessor.componentType) {
          case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
            const uint32_t* buf = reinterpret_cast<const uint32_t*>(
                &buffer.data[accessor.byteOffset + bufferView.byteOffset]);
            for (size_t index = 0; index < accessor.count; index++) {
              indexes.push_back(buf[index] + vertexStart);
            }
            break;
          }
          case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
            const uint16_t* buf = reinterpret_cast<const uint16_t*>(
                &buffer.data[accessor.byteOffset + bufferView.byteOffset]);
            for (size_t index = 0; index < accessor.count; index++) {
              indexes.push_back(buf[index] + vertexStart);
            }
            break;
          }
          case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(
                &buffer.data[accessor.byteOffset + bufferView.byteOffset]);
            for (size_t index = 0; index < accessor.count; index++) {
              indexes.push_back(buf[index] + vertexStart);
            }
            break;
          }
          default:
            std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
            return;
        }
      }
      MeshPrimitive primitive{.firstIndex = static_cast<int>(firstIndex),
                              .indexCount = static_cast<int>(indexCount),
                              .materialIndex = glTFPrimitive.material};
      mesh->addPrimitive(primitive);
    }

    if (generateTangent) {
      _generateTangent(indexes, vertices);
    }
    mesh->setGlobalMatrix(nodeMatrix);
    mesh->setIndexes(indexes);
    mesh->setVertices(vertices);
    mesh->setAABBPositions(aabb);
  }

  // we store all node's heads in _nodes array
  // if parent exists just attach node to existing node tree
  if (parent) {
    parent->children.push_back(node);
  } else {
    nodes.push_back(node);
  }
}

// Helper functions for locating glTF nodes
std::shared_ptr<NodeGLTF> LoaderGLTF::_findNode(std::shared_ptr<NodeGLTF> parent, uint32_t index) {
  std::shared_ptr<NodeGLTF> nodeFound = nullptr;
  if (parent->index == index) {
    return parent;
  }
  for (auto& child : parent->children) {
    nodeFound = _findNode(child, index);
    if (nodeFound) {
      break;
    }
  }
  return nodeFound;
}

std::shared_ptr<NodeGLTF> LoaderGLTF::_nodeFromIndex(uint32_t index,
                                                     const std::vector<std::shared_ptr<NodeGLTF>>& nodes) {
  std::shared_ptr<NodeGLTF> nodeFound = nullptr;
  for (auto& node : nodes) {
    nodeFound = _findNode(node, index);
    if (nodeFound) {
      break;
    }
  }
  return nodeFound;
}

// TODO: ssbo matrix copy is missing
void LoaderGLTF::_loadSkins(const tinygltf::Model& modelInternal,
                            const std::vector<std::shared_ptr<NodeGLTF>>& nodes,
                            std::vector<std::shared_ptr<SkinGLTF>>& skins) {
  for (size_t i = 0; i < modelInternal.skins.size(); i++) {
    std::shared_ptr<SkinGLTF> skin = std::make_shared<SkinGLTF>();
    tinygltf::Skin glTFSkin = modelInternal.skins[i];
    skin->name = glTFSkin.name;
    // Find joint nodes
    for (int jointIndex : glTFSkin.joints) {
      auto node = _nodeFromIndex(jointIndex, nodes);
      if (node) {
        skin->joints.push_back(node);
      }
    }

    // Get the inverse bind matrices from the buffer associated to this skin
    if (glTFSkin.inverseBindMatrices > -1) {
      const tinygltf::Accessor& accessor = modelInternal.accessors[glTFSkin.inverseBindMatrices];
      const tinygltf::BufferView& bufferView = modelInternal.bufferViews[accessor.bufferView];
      const tinygltf::Buffer& buffer = modelInternal.buffers[bufferView.buffer];
      skin->inverseBindMatrices.resize(accessor.count);
      std::memcpy(skin->inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset],
                  accessor.count * sizeof(glm::mat4));
    }

    skins.push_back(skin);
  }
}

void LoaderGLTF::_loadAnimations(const tinygltf::Model& modelInternal,
                                 const std::vector<std::shared_ptr<NodeGLTF>>& nodes,
                                 std::vector<std::shared_ptr<AnimationGLTF>>& animations) {
  for (size_t i = 0; i < modelInternal.animations.size(); i++) {
    std::shared_ptr<AnimationGLTF> animation = std::make_shared<AnimationGLTF>();
    tinygltf::Animation glTFAnimation = modelInternal.animations[i];
    animation->name = glTFAnimation.name;

    // Samplers
    animation->samplers.resize(glTFAnimation.samplers.size());
    for (size_t j = 0; j < glTFAnimation.samplers.size(); j++) {
      tinygltf::AnimationSampler glTFSampler = glTFAnimation.samplers[j];
      AnimationSamplerGLTF& dstSampler = animation->samplers[j];
      dstSampler.interpolation = glTFSampler.interpolation;

      // Read sampler keyframe input time values
      {
        const tinygltf::Accessor& accessor = modelInternal.accessors[glTFSampler.input];
        const tinygltf::BufferView& bufferView = modelInternal.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = modelInternal.buffers[bufferView.buffer];
        const void* dataPtr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];
        const float* buf = static_cast<const float*>(dataPtr);
        for (size_t index = 0; index < accessor.count; index++) {
          dstSampler.inputs.push_back(buf[index]);
        }
        // Adjust animation's start and end times
        for (auto input : animation->samplers[j].inputs) {
          if (input < animation->start) {
            animation->start = input;
          };
          if (input > animation->end) {
            animation->end = input;
          }
        }
      }

      // Read sampler keyframe output translate/rotate/scale values
      {
        const tinygltf::Accessor& accessor = modelInternal.accessors[glTFSampler.output];
        const tinygltf::BufferView& bufferView = modelInternal.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = modelInternal.buffers[bufferView.buffer];
        const void* dataPtr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];
        switch (accessor.type) {
          case TINYGLTF_TYPE_VEC3: {
            const glm::vec3* buf = static_cast<const glm::vec3*>(dataPtr);
            for (size_t index = 0; index < accessor.count; index++) {
              dstSampler.outputsVec4.push_back(glm::vec4(buf[index], 0.0f));
            }
            break;
          }
          case TINYGLTF_TYPE_VEC4: {
            const glm::vec4* buf = static_cast<const glm::vec4*>(dataPtr);
            for (size_t index = 0; index < accessor.count; index++) {
              dstSampler.outputsVec4.push_back(buf[index]);
            }
            break;
          }
          default: {
            std::cout << "unknown type" << std::endl;
            break;
          }
        }
      }
    }

    // Channels
    animation->channels.resize(glTFAnimation.channels.size());
    for (size_t j = 0; j < glTFAnimation.channels.size(); j++) {
      tinygltf::AnimationChannel glTFChannel = glTFAnimation.channels[j];
      AnimationChannelGLTF& dstChannel = animation->channels[j];
      dstChannel.path = glTFChannel.target_path;
      dstChannel.samplerIndex = glTFChannel.sampler;
      dstChannel.node = _nodeFromIndex(glTFChannel.target_node, nodes);
    }

    animations.push_back(animation);
  }
}
