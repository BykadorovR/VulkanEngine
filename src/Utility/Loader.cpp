#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
#include "Utility/Loader.h"
#include "glm/gtc/type_ptr.hpp"
#include <filesystem>
#include "mikktspace.h"

LoaderImage::LoaderImage(std::shared_ptr<EngineState> engineState) { _engineState = engineState; }

template <>
std::shared_ptr<ImageCPU<uint8_t>> LoaderImage::loadCPU<uint8_t>(std::string path) {
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
std::shared_ptr<ImageCPU<float>> LoaderImage::loadCPU<float>(std::string path) {
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

LoaderGLTF::LoaderGLTF(std::shared_ptr<LoaderImage> loaderImage, std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  _loaderImage = loaderImage;
}

#ifdef __ANDROID__
void LoaderGLTF::setAssetManager(AAssetManager* assetManager) { tinygltf::asset_manager = assetManager; }
#endif

std::shared_ptr<ModelGLTF> LoaderGLTF::load(std::string path, std::shared_ptr<CommandBuffer> commandBufferTransfer) {
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
    _loadMaterials(modelInternal, materials, modelExternal, commandBufferTransfer);
    // load nodes
    const tinygltf::Scene& scene = modelInternal.scenes[0];
    for (size_t i = 0; i < scene.nodes.size(); i++) {
      tinygltf::Node& node = modelInternal.nodes[scene.nodes[i]];
      _loadNode(modelInternal, node, nullptr, scene.nodes[i], materials, meshes, nodes, commandBufferTransfer);
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

  SMikkTSpaceInterface mkif;
  mkif.m_getNormal = [](SMikkTSpaceContext const* context, float normOut[], const int faceNum, const int vertNum) {
    VertexIndex& vertexIndex = *reinterpret_cast<VertexIndex*>(context->m_pUserData);
    glm::vec3 v;
    if (vertexIndex.indexes.size() > 0) {
      uint32_t index = vertexIndex.indexes[faceNum * 3 + vertNum];
      if (index < vertexIndex.vertices.size()) {
        v = vertexIndex.vertices[index].normal;
      }
    } else {
      v = vertexIndex.vertices[faceNum * 3 + vertNum].normal;
    }

    normOut[0] = v.x;
    normOut[1] = v.y;
    normOut[2] = v.z;
  };

  mkif.m_getNumFaces = [](SMikkTSpaceContext const* context) -> int {
    VertexIndex& vertexIndex = *reinterpret_cast<VertexIndex*>(context->m_pUserData);
    if (vertexIndex.indexes.size() > 0) return vertexIndex.indexes.size() / 3;
    return vertexIndex.vertices.size() / 3;
  };

  mkif.m_getNumVerticesOfFace = [](SMikkTSpaceContext const* context, const int faceNum) -> int { return 3; };
  mkif.m_getPosition = [](SMikkTSpaceContext const* context, float posOut[], const int faceNum, const int vertNum) {
    VertexIndex& vertexIndex = *reinterpret_cast<VertexIndex*>(context->m_pUserData);
    glm::vec3 v;
    if (vertexIndex.indexes.size() > 0) {
      uint32_t index = vertexIndex.indexes[faceNum * 3 + vertNum];
      if (index < vertexIndex.vertices.size()) {
        v = vertexIndex.vertices[index].pos;
      }
    } else {
      v = vertexIndex.vertices[faceNum * 3 + vertNum].pos;
    }

    posOut[0] = v.x;
    posOut[1] = v.y;
    posOut[2] = v.z;
  };
  mkif.m_getTexCoord = [](SMikkTSpaceContext const* context, float texCoordOut[], const int faceNum,
                          const int vertNum) {
    VertexIndex& vertexIndex = *reinterpret_cast<VertexIndex*>(context->m_pUserData);
    glm::vec2 v;
    if (vertexIndex.indexes.size() > 0) {
      uint32_t index = vertexIndex.indexes[faceNum * 3 + vertNum];
      if (index < vertexIndex.vertices.size()) {
        v = vertexIndex.vertices[index].texCoord;
      }
    } else {
      v = vertexIndex.vertices[faceNum * 3 + vertNum].texCoord;
    }

    texCoordOut[0] = v.x;
    texCoordOut[1] = v.y;
  };
  mkif.m_setTSpace = nullptr;
  mkif.m_setTSpaceBasic = [](SMikkTSpaceContext const* context, const float tangent[], const float fSign,
                             const int faceNum, const int vertNum) {
    VertexIndex& vertexIndex = *reinterpret_cast<VertexIndex*>(context->m_pUserData);
    Vertex3D* v;
    if (vertexIndex.indexes.size() > 0) {
      uint32_t index = vertexIndex.indexes[faceNum * 3 + vertNum];
      if (index < vertexIndex.vertices.size()) {
        v = &vertexIndex.vertices[index];
      }
    } else {
      v = &vertexIndex.vertices[faceNum * 3 + vertNum];
    }

    glm::vec3 temp = glm::make_vec3(tangent);
    v->tangent = glm::vec4(temp, fSign);
  };

  SMikkTSpaceContext msc;
  msc.m_pInterface = &mkif;
  std::shared_ptr<VertexIndex> vertexIndexData = std::make_shared<VertexIndex>(indexes, vertices);
  msc.m_pUserData = vertexIndexData.get();

  bool res = genTangSpaceDefault(&msc);
  if (res == false) std::cout << "Can't generate tangent vectors";
}

// load all textures here
std::shared_ptr<Texture> LoaderGLTF::_loadTexture(int imageIndex,
                                                  VkFormat format,
                                                  const tinygltf::Model& modelInternal,
                                                  std::vector<std::shared_ptr<Texture>>& textures,
                                                  std::shared_ptr<CommandBuffer> commandBufferTransfer) {
  std::shared_ptr<Texture> texture = textures[imageIndex];
  if (texture == nullptr) {
    const tinygltf::Image glTFImage = modelInternal.images[imageIndex];
    auto filePath = glTFImage.uri;
    if (std::filesystem::exists(filePath)) {
      auto imageCPU = _loaderImage->loadCPU<uint8_t>(filePath);
      texture = std::make_shared<Texture>(_loaderImage->loadGPU<uint8_t>({imageCPU}), format,
                                          VK_SAMPLER_ADDRESS_MODE_REPEAT, 1, VK_FILTER_LINEAR, commandBufferTransfer,
                                          _engineState);
    } else {
      // Get the image data from the glTF loader
      unsigned char* buffer = nullptr;
      int bufferSize = 0;
      bool deleteBuffer = false;
      // We convert RGB-only images to RGBA, as most devices don't support RGB-formats in Vul
      // kan
      if (glTFImage.component == 3) {
        bufferSize = glTFImage.width * glTFImage.height * 4;
        buffer = new unsigned char[bufferSize];
        unsigned char* rgba = buffer;
        const unsigned char* rgb = glTFImage.image.data();
        for (size_t i = 0; i < glTFImage.width * glTFImage.height; ++i) {
          memcpy(rgba, rgb, sizeof(unsigned char) * 3);
          rgba += 4;
          rgb += 3;
        }
        deleteBuffer = true;
      } else {
        buffer = (unsigned char*)glTFImage.image.data();
        bufferSize = glTFImage.image.size();
      }

      // copy buffer to Texture
      auto stagingBuffer = std::make_shared<Buffer>(
          bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);

      stagingBuffer->setData(buffer);

      // for some textures SRGB is used but for others linear format
      auto image = std::make_shared<Image>(std::tuple{glTFImage.width, glTFImage.height}, 1, 1, format,
                                           VK_IMAGE_TILING_OPTIMAL,
                                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, _engineState);

      image->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1,
                          1, commandBufferTransfer);
      image->copyFrom(stagingBuffer, {0}, commandBufferTransfer);
      image->changeLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1,
                          1, commandBufferTransfer);

      auto imageView = std::make_shared<ImageView>(image, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1, VK_IMAGE_ASPECT_COLOR_BIT,
                                                   _engineState);
      texture = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_REPEAT, 1, VK_FILTER_LINEAR, imageView, _engineState);
      if (deleteBuffer) {
        delete[] buffer;
      }
    }
    textures[imageIndex] = texture;
  }
  return texture;
}

// TODO: we can store baseColorFactor only in GLTF material, rest will go to PBR/Phong
void LoaderGLTF::_loadMaterials(const tinygltf::Model& modelInternal,
                                std::vector<std::shared_ptr<MaterialGLTF>>& materialGLTF,
                                std::shared_ptr<ModelGLTF> modelExternal,
                                std::shared_ptr<CommandBuffer> commandBufferTransfer) {
  std::vector<std::shared_ptr<MaterialPBR>> materialsPBR;
  std::vector<std::shared_ptr<MaterialPhong>> materialsPhong;
  std::vector<std::shared_ptr<MaterialColor>> materialsColor;
  std::vector<std::shared_ptr<Texture>> textures(modelInternal.images.size(), nullptr);
  for (size_t i = 0; i < modelInternal.materials.size(); i++) {
    tinygltf::Material glTFMaterial = modelInternal.materials[i];
    std::shared_ptr<MaterialPhong> materialPhong = std::make_shared<MaterialPhong>(MaterialTarget::SIMPLE,
                                                                                   commandBufferTransfer, _engineState);
    std::shared_ptr<MaterialPBR> materialPBR = std::make_shared<MaterialPBR>(MaterialTarget::SIMPLE,
                                                                             commandBufferTransfer, _engineState);
    std::shared_ptr<MaterialColor> materialColor = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE,
                                                                                   commandBufferTransfer, _engineState);
    std::shared_ptr<MaterialGLTF> material = std::make_shared<MaterialGLTF>();
    float metallicFactor = 0;
    float roughnessFactor = 0;
    float occlusionStrength = 0;
    glm::vec3 emissiveFactor = glm::vec3(0.f);
    // Get the base color factor
    material->baseColorFactor = glm::make_vec4(glTFMaterial.pbrMetallicRoughness.baseColorFactor.data());
    // Get metallic factor
    metallicFactor = glTFMaterial.pbrMetallicRoughness.metallicFactor;
    // Get roughness factor
    roughnessFactor = glTFMaterial.pbrMetallicRoughness.roughnessFactor;
    // Get occlusion strength
    occlusionStrength = glTFMaterial.occlusionTexture.strength;
    // Get emissive factor
    emissiveFactor = glm::make_vec3(glTFMaterial.emissiveFactor.data());
    materialPBR->setCoefficients(metallicFactor, roughnessFactor, occlusionStrength, emissiveFactor);

    // Get double side propery
    materialPBR->setDoubleSided(glTFMaterial.doubleSided);
    materialPhong->setDoubleSided(glTFMaterial.doubleSided);
    // Get alpha cut off information
    materialPBR->setAlphaCutoff((glTFMaterial.alphaMode == "MASK"), glTFMaterial.alphaCutoff);
    materialPhong->setAlphaCutoff((glTFMaterial.alphaMode == "MASK"), glTFMaterial.alphaCutoff);
    // Get base color texture
    {
      // glTF texture index
      auto baseColorTextureIndex = glTFMaterial.pbrMetallicRoughness.baseColorTexture.index;
      if (baseColorTextureIndex >= 0) {
        // glTF image index
        auto baseColorImageIndex = modelInternal.textures[baseColorTextureIndex].source;
        // set texture to phong material
        materialPhong->setBaseColor(
            {_loadTexture(baseColorImageIndex, _engineState->getSettings()->getLoadTextureColorFormat(), modelInternal,
                          textures, commandBufferTransfer)});
        // set texture to PBR material
        materialPBR->setBaseColor(
            {_loadTexture(baseColorImageIndex, _engineState->getSettings()->getLoadTextureColorFormat(), modelInternal,
                          textures, commandBufferTransfer)});
        materialColor->setBaseColor(
            {_loadTexture(baseColorImageIndex, _engineState->getSettings()->getLoadTextureColorFormat(), modelInternal,
                          textures, commandBufferTransfer)});
      }
    }
    // Get normal texture
    {
      auto normalTextureIndex = glTFMaterial.normalTexture.index;
      if (normalTextureIndex >= 0) {
        // glTF image index
        auto normalImageIndex = modelInternal.textures[normalTextureIndex].source;
        // set normal texture to phong material
        materialPhong->setNormal(
            {_loadTexture(normalImageIndex, _engineState->getSettings()->getLoadTextureAuxilaryFormat(), modelInternal,
                          textures, commandBufferTransfer)});
        // set normal texture to PBR material
        materialPBR->setNormal(
            {_loadTexture(normalImageIndex, _engineState->getSettings()->getLoadTextureAuxilaryFormat(), modelInternal,
                          textures, commandBufferTransfer)});
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
                          modelInternal, textures, commandBufferTransfer)});
        // set metallic texture to PBR material
        materialPBR->setMetallic(
            {_loadTexture(metallicRoughnessImageIndex, _engineState->getSettings()->getLoadTextureAuxilaryFormat(),
                          modelInternal, textures, commandBufferTransfer)});
        // set roughness texture to PBR material
        materialPBR->setRoughness(
            {_loadTexture(metallicRoughnessImageIndex, _engineState->getSettings()->getLoadTextureAuxilaryFormat(),
                          modelInternal, textures, commandBufferTransfer)});
      }
    }
    // Get occlusion texture
    {
      auto occlusionTextureIndex = glTFMaterial.occlusionTexture.index;
      if (occlusionTextureIndex >= 0) {
        auto occlusionImageIndex = modelInternal.textures[occlusionTextureIndex].source;
        materialPBR->setOccluded(
            {_loadTexture(occlusionImageIndex, _engineState->getSettings()->getLoadTextureAuxilaryFormat(),
                          modelInternal, textures, commandBufferTransfer)});
      }
    }
    // Get emissive texture
    {
      auto emissiveTextureIndex = glTFMaterial.emissiveTexture.index;
      if (emissiveTextureIndex >= 0) {
        auto emissiveImageIndex = modelInternal.textures[emissiveTextureIndex].source;
        materialPBR->setEmissive(
            {_loadTexture(emissiveImageIndex, _engineState->getSettings()->getLoadTextureColorFormat(), modelInternal,
                          textures, commandBufferTransfer)});
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
                           std::vector<std::shared_ptr<NodeGLTF>>& nodes,
                           std::shared_ptr<CommandBuffer> commandBufferTransfer) {
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
                nodes, commandBufferTransfer);
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
      uint32_t firstIndex = indexes.size();
      uint32_t vertexStart = vertices.size();
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

        int jointByteStride;
        int jointComponentType;
        int positionByteStride;
        int normalByteStride;
        int uv0ByteStride;
        int weightByteStride;
        int tangentByteStride;
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
            auto extendAABBJoints = [&](float weight, float index) {
              if (weight > 0) {
                auto aabbJoints = mesh->getAABBJoints();
                if (aabbJoints.find(index) == aabbJoints.end()) aabbJoints[index] = std::make_shared<AABB>();
                // auto value = nodeMatrix * glm::vec4(vertex.pos, 1.f);
                aabbJoints[index]->extend(vertex.pos);
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
    mesh->setIndexes(indexes, commandBufferTransfer);
    mesh->setVertices(vertices, commandBufferTransfer);
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