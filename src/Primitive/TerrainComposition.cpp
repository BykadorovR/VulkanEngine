#include "Primitive/TerrainComposition.h"
#undef far
#undef near
#include "stb_image_write.h"
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <nlohmann/json.hpp>
#include <fstream>

struct FragmentPointLightPushDepth {
  alignas(16) glm::vec3 lightPosition;
  alignas(16) int far;
};

struct TesselationControlPush {
  int patchDimX;
  int patchDimY;
  int minTessellationLevel;
  int maxTessellationLevel;
  float minTesselationDistance;
  float maxTesselationDistance;
};

struct TesselationControlPushDepth {
  int minTessellationLevel;
  int maxTessellationLevel;
  float minTesselationDistance;
  float maxTesselationDistance;
};

struct TesselationEvaluationPush {
  int patchDimX;
  int patchDimY;
  float heightScale;
  float heightShift;
};

struct TesselationEvaluationPushDepth {
  float heightScale;
  float heightShift;
};

struct FragmentPush {
  int enableShadow;
  int enableLighting;
  glm::vec3 cameraPosition;
};

struct FragmentPushDebug {
  alignas(16) int patchEdge;
  int showLOD;
  int enableShadow;
  int enableLighting;
  int tile;
};

TerrainCompositionDebug::TerrainCompositionDebug(std::shared_ptr<ImageCPU<uint8_t>> heightMapCPU,
                                                 std::pair<int, int> patchNumber,
                                                 std::shared_ptr<CommandBuffer> commandBufferTransfer,
                                                 std::shared_ptr<GUI> gui,
                                                 std::shared_ptr<GameState> gameState,
                                                 std::shared_ptr<EngineState> engineState) {
  setName("TerrainComposition");
  _engineState = engineState;
  _gameState = gameState;
  _patchNumber = patchNumber;
  _gui = gui;
  _changedMaterial.resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  // needed for layout
  _defaultMaterialColor = std::make_shared<MaterialColor>(MaterialTarget::TERRAIN, commandBufferTransfer, engineState);
  _defaultMaterialColor->setBaseColor(std::vector{4, _gameState->getResourceManager()->getTextureOne()});
  _material = _defaultMaterialColor;
  _heightMapCPU = heightMapCPU;
  _heightMapGPU = _gameState->getResourceManager()->loadImageGPU<uint8_t>({heightMapCPU});
  _changedHeightmap.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _heightMap = std::make_shared<Texture>(_heightMapGPU, _engineState->getSettings()->getLoadTextureAuxilaryFormat(),
                                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, VK_FILTER_LINEAR,
                                         commandBufferTransfer, engineState);

  _changeMesh.resize(engineState->getSettings()->getMaxFramesInFlight());
  _mesh.resize(engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _mesh[i] = std::make_shared<MeshDynamic3D>(engineState);
    _calculateMesh(i);
  }

  _cameraBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _cameraBuffer[i] = std::make_shared<Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, engineState);

  // layout for Normal
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutNormal{{.binding = 0,
                                                            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                            .descriptorCount = 1,
                                                            .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                            .pImmutableSamplers = nullptr},
                                                           {.binding = 1,
                                                            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                            .descriptorCount = 1,
                                                            .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                            .pImmutableSamplers = nullptr},
                                                           {.binding = 2,
                                                            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                            .descriptorCount = 1,
                                                            .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                            .pImmutableSamplers = nullptr},
                                                           {.binding = 3,
                                                            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                            .descriptorCount = 1,
                                                            .stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT,
                                                            .pImmutableSamplers = nullptr}};
    descriptorSetLayout->createCustom(layoutNormal);
    _descriptorSetLayoutNormalsMesh.push_back({"normal", descriptorSetLayout});

    _descriptorSetNormal.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetNormal[i] = std::make_shared<DescriptorSet>(descriptorSetLayout, engineState);
      std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoNormalsMesh{
          {0, {{.buffer = _cameraBuffer[i]->getData(), .offset = 0, .range = _cameraBuffer[i]->getSize()}}},
          {1, {{.buffer = _cameraBuffer[i]->getData(), .offset = 0, .range = _cameraBuffer[i]->getSize()}}},
          {3, {{.buffer = _cameraBuffer[i]->getData(), .offset = 0, .range = _cameraBuffer[i]->getSize()}}}};
      std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor{
          {2,
           {{.sampler = _heightMap->getSampler()->getSampler(),
             .imageView = _heightMap->getImageView()->getImageView(),
             .imageLayout = _heightMap->getImageView()->getImage()->getImageLayout()}}}};
      _descriptorSetNormal[i]->createCustom(bufferInfoNormalsMesh, textureInfoColor);
    }

    // initialize Normal (per vertex)
    {
      auto shaderNormal = std::make_shared<Shader>(engineState);
      shaderNormal->add("shaders/terrain/terrainDepth_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shaderNormal->add("shaders/terrain/terrainNormal_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      shaderNormal->add("shaders/terrain/terrainDepth_control.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
      shaderNormal->add("shaders/terrain/terrainNormal_evaluation.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
      shaderNormal->add("shaders/terrain/terrainNormal_geometry.spv", VK_SHADER_STAGE_GEOMETRY_BIT);

      _pipelineNormalMesh = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                              _engineState->getDevice());

      std::map<std::string, VkPushConstantRange> pushConstants;
      pushConstants["controlDepth"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
          .offset = 0,
          .size = sizeof(TesselationControlPushDepth),
      };
      pushConstants["evaluate"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
          .offset = sizeof(TesselationControlPushDepth),
          .size = sizeof(TesselationEvaluationPush),
      };
      _pipelineNormalMesh->setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipelineNormalMesh->setDepthTest(true);
      _pipelineNormalMesh->setDepthWrite(true);
      _pipelineNormalMesh->setTesselation(4);
      _pipelineNormalMesh->setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipelineNormalMesh->createCustom(
          shaderNormal, _descriptorSetLayoutNormalsMesh, pushConstants, _mesh[0]->getBindingDescription(),
          _mesh[0]->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                      {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}}),
          RenderPassScenario::GRAPHIC);
    }

    // initialize Tangent (per vertex)
    {
      auto shaderNormal = std::make_shared<Shader>(engineState);
      shaderNormal->add("shaders/terrain/terrainDepth_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shaderNormal->add("shaders/terrain/terrainNormal_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      shaderNormal->add("shaders/terrain/terrainDepth_control.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
      shaderNormal->add("shaders/terrain/terrainTangent_evaluation.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
      shaderNormal->add("shaders/terrain/terrainNormal_geometry.spv", VK_SHADER_STAGE_GEOMETRY_BIT);

      _pipelineTangentMesh = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                               _engineState->getDevice());

      std::map<std::string, VkPushConstantRange> pushConstants;
      pushConstants["controlDepth"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
          .offset = 0,
          .size = sizeof(TesselationControlPushDepth),
      };
      pushConstants["evaluate"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
          .offset = sizeof(TesselationControlPushDepth),
          .size = sizeof(TesselationEvaluationPush),
      };

      _pipelineTangentMesh->setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipelineTangentMesh->setDepthTest(true);
      _pipelineTangentMesh->setDepthWrite(true);
      _pipelineTangentMesh->setTesselation(4);
      _pipelineTangentMesh->setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipelineTangentMesh->createCustom(
          shaderNormal, _descriptorSetLayoutNormalsMesh, pushConstants, _mesh[0]->getBindingDescription(),
          _mesh[0]->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                      {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}}),
          RenderPassScenario::GRAPHIC);
    }
  }

  _reallocatePatch.resize(engineState->getSettings()->getMaxFramesInFlight());
  _patchDescriptionSSBO.resize(engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _reallocatePatchDescription(i);
  }
  // layout for Color
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutColor{{.binding = 0,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 2,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 3,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 4,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 4,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr}};

    descriptorSetLayout->createCustom(layoutColor);
    _descriptorSetLayout.push_back({"color", descriptorSetLayout});

    _descriptorSetColor.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetColor[i] = std::make_shared<DescriptorSet>(descriptorSetLayout, engineState);
    }
    setMaterial(_defaultMaterialColor);

    // initialize Color
    {
      auto shader = std::make_shared<Shader>(engineState);
      shader->add("shaders/terrain/composition/terrainDebug_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/terrain/composition/terrainDebug_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      shader->add("shaders/terrain/composition/terrainDebug_control.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
      shader->add("shaders/terrain/composition/terrainDebug_evaluation.spv",
                  VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
      _pipeline = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(), _engineState->getDevice());

      std::map<std::string, VkPushConstantRange> pushConstants;
      pushConstants["control"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
          .offset = 0,
          .size = sizeof(TesselationControlPush),
      };
      pushConstants["evaluateDepth"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
          .offset = sizeof(TesselationControlPush),
          .size = sizeof(TesselationEvaluationPushDepth),
      };
      pushConstants["fragment"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .offset = sizeof(TesselationControlPush) + sizeof(TesselationEvaluationPushDepth),
          .size = sizeof(FragmentPushDebug),
      };

      _pipeline->setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipeline->setDepthTest(true);
      _pipeline->setDepthWrite(true);
      _pipeline->setTesselation(4);
      _pipeline->setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipeline->createCustom(
          shader, _descriptorSetLayout, pushConstants, _mesh[0]->getBindingDescription(),
          _mesh[0]->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                      {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}}),
          RenderPassScenario::GRAPHIC);

      _pipelineWireframe = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                             _engineState->getDevice());
      _pipelineWireframe->setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipelineWireframe->setPolygonMode(VK_POLYGON_MODE_LINE);
      _pipelineWireframe->setDepthTest(true);
      _pipelineWireframe->setDepthWrite(true);
      _pipelineWireframe->setTesselation(4);
      _pipelineWireframe->setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipelineWireframe->createCustom(
          shader, _descriptorSetLayout, pushConstants, _mesh[0]->getBindingDescription(),
          _mesh[0]->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                      {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}}),
          RenderPassScenario::GRAPHIC);
    }
  }

  _changePatch.resize(engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _updatePatchDescription(i);
  }
}

int TerrainCompositionDebug::_saveAuxilary(std::string path) {
  nlohmann::json outputJSON;
  outputJSON["rotation"] = _patchRotationsIndex;
  outputJSON["textures"] = _patchTextures;
  outputJSON["patches"] = _patchNumber;
  std::ofstream file(path + ".json");
  file << outputJSON;
  return true;
}

void TerrainCompositionDebug::_loadAuxilary(std::string path) {
  std::ifstream file(path + ".json");
  nlohmann::json inputJSON;
  file >> inputJSON;
  bool changePatches = false;
  if (inputJSON["rotation"].size() != _patchRotationsIndex.size()) {
    changePatches = true;
    _patchRotationsIndex.resize(inputJSON["rotation"].size());
  }
  for (int i = 0; i < inputJSON["rotation"].size(); i++) {
    _patchRotationsIndex[i] = inputJSON["rotation"][i];
  }
  if (inputJSON["textures"].size() != _patchTextures.size()) {
    changePatches = true;
    _patchTextures.resize(inputJSON["textures"].size());
  }
  for (int i = 0; i < inputJSON["textures"].size(); i++) {
    _patchTextures[i] = inputJSON["textures"][i];
  }
  std::pair<int, int> newPatches = inputJSON["patches"];
  if (newPatches.first != _patchNumber.first || newPatches.second != _patchNumber.second) {
    changePatches = true;
    _patchNumber = newPatches;
  }

  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _changePatch[i] = true;
  }

  if (changePatches) {
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _changeMesh[i] = true;
      // needed for rotation. Number of patches has been changed, need to reallocate and set.
      _reallocatePatch[i] = true;
      _changedMaterial[i] = true;
    }
  }
}

void TerrainCompositionDebug::_updateColorDescriptor() {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialColor>(_material);
  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor{
      {0,
       {{.buffer = _cameraBuffer[currentFrame]->getData(),
         .offset = 0,
         .range = _cameraBuffer[currentFrame]->getSize()}}},
      {1,
       {{.buffer = _patchDescriptionSSBO[currentFrame]->getData(),
         .offset = 0,
         .range = _patchDescriptionSSBO[currentFrame]->getSize()}}},
      {2,
       {{.buffer = _cameraBuffer[currentFrame]->getData(),
         .offset = 0,
         .range = _cameraBuffer[currentFrame]->getSize()}}}};
  std::vector<VkDescriptorImageInfo> textureBaseColor(material->getBaseColor().size());
  for (int j = 0; j < material->getBaseColor().size(); j++) {
    textureBaseColor[j] = {.sampler = material->getBaseColor()[j]->getSampler()->getSampler(),
                           .imageView = material->getBaseColor()[j]->getImageView()->getImageView(),
                           .imageLayout = material->getBaseColor()[j]->getImageView()->getImage()->getImageLayout()};
  }
  std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor{
      {3,
       {{.sampler = _heightMap->getSampler()->getSampler(),
         .imageView = _heightMap->getImageView()->getImageView(),
         .imageLayout = _heightMap->getImageView()->getImage()->getImageLayout()}}},
      {4, {textureBaseColor}}};

  _descriptorSetColor[currentFrame]->createCustom(bufferInfoColor, textureInfoColor);
}

void TerrainCompositionDebug::_reallocatePatchDescription(int currentFrame) {
  _patchTextures = std::vector<int>(_patchNumber.first * _patchNumber.second, 0);
  auto [width, height] = _heightMap->getImageView()->getImage()->getResolution();
  //
  for (int y = 0; y < _patchNumber.second; y++)
    for (int x = 0; x < _patchNumber.first; x++) {
      int patchX0 = (float)x / _patchNumber.first * width;
      int patchY0 = (float)y / _patchNumber.second * height;
      int patchX1 = (float)(x + 1) / _patchNumber.first * width;
      int patchY1 = (float)(y + 1) / _patchNumber.second * height;

      int heightLevel = 0;
      for (int i = patchY0; i < patchY1; i++) {
        for (int j = patchX0; j < patchX1; j++) {
          int textureCoord = (j + i * width) * _heightMapCPU->getChannels();
          heightLevel = std::max(heightLevel, (int)_heightMapCPU->getData()[textureCoord]);
        }
      }

      int index = x + y * _patchNumber.first;
      if (heightLevel < _heightLevels[0]) {
        _patchTextures[index] = 0;
      } else if (heightLevel < _heightLevels[1]) {
        _patchTextures[index] = 1;
      } else if (heightLevel < _heightLevels[2]) {
        _patchTextures[index] = 2;
      } else {
        _patchTextures[index] = 3;
      }
    }

  _patchRotationsIndex = std::vector<int>(_patchNumber.first * _patchNumber.second, 0);
  _patchDescriptionSSBO[currentFrame] = std::make_shared<Buffer>(
      _patchNumber.first * _patchNumber.second * sizeof(PatchDescription), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);
}

void TerrainCompositionDebug::_updatePatchDescription(int currentFrame) {
  std::vector<PatchDescription> patchData(_patchNumber.first * _patchNumber.second);
  for (int i = 0; i < patchData.size(); i++) {
    patchData[i] = PatchDescription({.rotation = 90 * _patchRotationsIndex[i], .textureID = _patchTextures[i]});
  }
  // fill only number of lights because it's stub
  _patchDescriptionSSBO[currentFrame]->setData(patchData.data(), patchData.size() * sizeof(PatchDescription));
}

void TerrainCompositionDebug::draw(std::shared_ptr<CommandBuffer> commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  auto drawTerrain = [&](std::shared_ptr<Pipeline> pipeline) {
    vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());

    auto resolution = _engineState->getSettings()->getResolution();
    VkViewport viewport{.x = 0.0f,
                        .y = static_cast<float>(std::get<1>(resolution)),
                        .width = static_cast<float>(std::get<0>(resolution)),
                        .height = static_cast<float>(-std::get<1>(resolution)),
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f};

    vkCmdSetViewport(commandBuffer->getCommandBuffer(), 0, 1, &viewport);

    VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(std::get<0>(resolution), std::get<1>(resolution))};

    vkCmdSetScissor(commandBuffer->getCommandBuffer(), 0, 1, &scissor);
    if (pipeline->getPushConstants().find("control") != pipeline->getPushConstants().end()) {
      TesselationControlPush pushConstants{
          .patchDimX = _patchNumber.first,
          .patchDimY = _patchNumber.second,
          .minTessellationLevel = _minTessellationLevel,
          .maxTessellationLevel = _maxTessellationLevel,
          .minTesselationDistance = _minTesselationDistance,
          .maxTesselationDistance = _maxTesselationDistance,
      };
      auto info = pipeline->getPushConstants()["control"];
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    if (pipeline->getPushConstants().find("controlDepth") != pipeline->getPushConstants().end()) {
      TesselationControlPushDepth pushConstants{.minTessellationLevel = _minTessellationLevel,
                                                .maxTessellationLevel = _maxTessellationLevel,
                                                .minTesselationDistance = _minTesselationDistance,
                                                .maxTesselationDistance = _maxTesselationDistance};
      auto info = pipeline->getPushConstants()["controlDepth"];
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    if (pipeline->getPushConstants().find("evaluate") != pipeline->getPushConstants().end()) {
      TesselationEvaluationPush pushConstants{.patchDimX = _patchNumber.first,
                                              .patchDimY = _patchNumber.second,
                                              .heightScale = _heightScale,
                                              .heightShift = _heightShift};

      auto info = pipeline->getPushConstants()["evaluate"];
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    if (pipeline->getPushConstants().find("evaluateDepth") != pipeline->getPushConstants().end()) {
      TesselationEvaluationPushDepth pushConstants{.heightScale = _heightScale, .heightShift = _heightShift};
      auto info = pipeline->getPushConstants()["evaluateDepth"];
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    if (pipeline->getPushConstants().find("fragment") != pipeline->getPushConstants().end()) {
      FragmentPushDebug pushConstants{.patchEdge = _enableEdge,
                                      .showLOD = _showLoD,
                                      .enableShadow = _enableShadow,
                                      .enableLighting = _enableLighting,
                                      .tile = _pickedTile};

      auto info = pipeline->getPushConstants()["fragment"];
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    // same buffer to both tessellation shaders because we're not going to change camera between these 2 stages
    BufferMVP cameraUBO{.model = getModel(),
                        .view = _gameState->getCameraManager()->getCurrentCamera()->getView(),
                        .projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection()};

    _cameraBuffer[currentFrame]->setData(&cameraUBO);

    VkBuffer vertexBuffers[] = {_mesh[currentFrame]->getVertexBuffer()->getBuffer()->getData()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

    // color
    auto pipelineLayout = pipeline->getDescriptorSetLayout();
    auto colorLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                    [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                      return info.first == std::string("color");
                                    });
    if (colorLayout != pipelineLayout.end()) {
      vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->getPipelineLayout(), 0, 1,
                              &_descriptorSetColor[currentFrame]->getDescriptorSets(), 0, nullptr);
    }

    // normals and tangents
    auto normalTangentLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                            [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                              return info.first == std::string("normal");
                                            });
    if (normalTangentLayout != pipelineLayout.end()) {
      vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->getPipelineLayout(), 0, 1,
                              &_descriptorSetNormal[currentFrame]->getDescriptorSets(), 0, nullptr);
    }

    vkCmdDraw(commandBuffer->getCommandBuffer(), _mesh[currentFrame]->getVertexData().size(), 1, 0, 0);
  };

  if (_changeMesh[_engineState->getFrameInFlight()]) {
    _calculateMesh(_engineState->getFrameInFlight());
    _changeMesh[_engineState->getFrameInFlight()] = false;
  }

  if (_reallocatePatch[_engineState->getFrameInFlight()]) {
    _reallocatePatchDescription(_engineState->getFrameInFlight());
    _reallocatePatch[_engineState->getFrameInFlight()] = false;
  }

  if (_changePatch[_engineState->getFrameInFlight()]) {
    _updatePatchDescription(_engineState->getFrameInFlight());
    _changePatch[_engineState->getFrameInFlight()] = false;
  }

  if (_changedMaterial[currentFrame]) {
    _updateColorDescriptor();
    _changedMaterial[currentFrame] = false;
  }

  if (_changedHeightmap[currentFrame]) {
    std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor{
        {3,
         {{.sampler = _heightMap->getSampler()->getSampler(),
           .imageView = _heightMap->getImageView()->getImageView(),
           .imageLayout = _heightMap->getImageView()->getImage()->getImageLayout()}}}};
    _descriptorSetColor[currentFrame]->createCustom({}, textureInfoColor);

    _terrainCPU->setHeightmap(_terrainPhysics->getHeights());

    _changedHeightmap[currentFrame] = false;
  }

  auto pipeline = _pipeline;
  if (_drawType == DrawType::WIREFRAME) pipeline = _pipelineWireframe;
  if (_drawType == DrawType::NORMAL) pipeline = _pipelineNormalMesh;
  if (_drawType == DrawType::TANGENT) pipeline = _pipelineTangentMesh;
  drawTerrain(pipeline);
}

void TerrainCompositionDebug::drawDebug(std::shared_ptr<CommandBuffer> commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();

  if (_changedHeightmap[currentFrame]) {
    _heightMap->copyFrom(_heightMapGPU, commandBuffer);
  }
  if (_gui->startTree("Terrain")) {
    _gui->drawText({"Tile: " + std::to_string(_pickedTile)});
    _gui->drawText({"Texture: " + std::to_string(_pickedPixel.x) + "x" + std::to_string(_pickedPixel.y)});
    _gui->drawText({"Click: " + std::to_string(_clickPosition.x) + "x" + std::to_string(_clickPosition.y)});
    glm::vec3 physicsHit = glm::vec3(-1.f);
    if (_hitCoords) physicsHit = _hitCoords.value();
    _gui->drawText({"Hit: " + std::to_string(physicsHit.x) + "x" + std::to_string(physicsHit.y) + "x" +
                    std::to_string(physicsHit.z)});
    std::map<std::string, int*> patchesNumber{{"Patch x", &_patchNumber.first}, {"Patch y", &_patchNumber.second}};
    if (_gui->drawInputInt(patchesNumber)) {
      // we can't change mesh here because we have to change it for all frames in flight eventually
      for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
        _changeMesh[i] = true;
        // needed for rotation. Number of patches has been changed, need to reallocate and set.
        _reallocatePatch[i] = true;
        _changePatch[i] = true;
        _changedMaterial[i] = true;
      }
    }
    std::map<std::string, int*> tesselationLevels{{"Tesselation min", &_minTessellationLevel},
                                                  {"Tesselation max", &_maxTessellationLevel}};
    if (_gui->drawInputInt(tesselationLevels)) {
      setTessellationLevel(_minTessellationLevel, _maxTessellationLevel);
    }

    std::map<std::string, int*> angleList;
    angleList["##Angle"] = &_angleIndex;
    if (_gui->drawListBox({"0", "90", "180", "270"}, angleList, 4)) {
      if (_pickedTile > 0 && _pickedTile < _patchRotationsIndex.size()) {
        _patchRotationsIndex[_pickedTile] = _angleIndex;
        for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
          _changePatch[i] = true;
        }
      }
    }

    std::map<std::string, int*> textureList;
    textureList["##Texture"] = &_textureIndex;
    if (_gui->drawListBox({"0", "1", "2", "3"}, textureList, 4)) {
      if (_pickedTile > 0 && _pickedTile < _patchTextures.size()) {
        _patchTextures[_pickedTile] = _textureIndex;
        for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
          _changePatch[i] = true;
        }
      }
    }

    _gui->drawInputText("##Path", _terrainPath, sizeof(_terrainPath));

    if (_gui->drawButton("Save terrain")) {
      _saveHeightmap(std::string(_terrainPath));
      _saveAuxilary(std::string(_terrainPath));
    }

    if (_gui->drawButton("Load terrain")) {
      _loadHeightmap(std::string(_terrainPath));
      _loadAuxilary(std::string(_terrainPath));
    }

    if (_gui->drawCheckbox({{"Patches", &_showPatches}})) {
      patchEdge(_showPatches);
    }
    if (_gui->drawCheckbox({{"LoD", &_showLoD}})) {
      showLoD(_showLoD);
    }
    if (_gui->drawCheckbox({{"Wireframe", &_showWireframe}})) {
      setDrawType(DrawType::WIREFRAME);
      _showNormals = false;
    }
    if (_gui->drawCheckbox({{"Normal", &_showNormals}})) {
      setDrawType(DrawType::NORMAL);
      _showWireframe = false;
    }
    if (_showWireframe == false && _showNormals == false) {
      setDrawType(DrawType::FILL);
    }
    _gui->endTree();
  }
}

void TerrainCompositionDebug::cursorNotify(float xPos, float yPos) { _cursorPosition = glm::vec2{xPos, yPos}; }

void TerrainCompositionDebug::mouseNotify(int button, int action, int mods) {
#ifdef __ANDROID__
  if (button == 0 && action == 1) {
#else
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
#endif
    _clickPosition = _cursorPosition;
    _hitCoords = _terrainPhysics->getHit(_cursorPosition);
    if (_hitCoords) {
      // find the corresponding patch number
      _pickedTile = _calculateTileByPosition(_hitCoords.value());
      _pickedPixel = _calculatePixelByPosition(_hitCoords.value());
      // reset selected item in angles list
      _angleIndex = _patchRotationsIndex[_pickedTile];
      _textureIndex = _patchTextures[_pickedTile];
    }
  }
}

void TerrainCompositionDebug::keyNotify(int key, int scancode, int action, int mods) {}

void TerrainCompositionDebug::charNotify(unsigned int code) {}

void TerrainCompositionDebug::scrollNotify(double xOffset, double yOffset) {
  _changeHeightmap(_pickedPixel, yOffset);
  // need to recreate TerrainPhysics because heightmapCPU was updated
  _terrainPhysics->reset(_heightMapCPU);
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) _changedHeightmap[i] = true;
}

TerrainComposition::TerrainComposition(std::shared_ptr<ImageCPU<uint8_t>> heightMapCPU,
                                       std::shared_ptr<GameState> gameState,
                                       std::shared_ptr<EngineState> engineState) {
  setName("Terrain");
  _engineState = engineState;
  _gameState = gameState;
  _heightMapCPU = heightMapCPU;
}

void TerrainComposition::initialize(std::shared_ptr<CommandBuffer> commandBuffer) {
  // needed for layout
  _defaultMaterialColor = std::make_shared<MaterialColor>(MaterialTarget::TERRAIN, commandBuffer, _engineState);
  _defaultMaterialColor->setBaseColor(std::vector{4, _gameState->getResourceManager()->getTextureOne()});
  _material = _defaultMaterialColor;
  _changedMaterial.resize(_engineState->getSettings()->getMaxFramesInFlight(), false);

  _heightMapGPU = _gameState->getResourceManager()->loadImageGPU<uint8_t>({_heightMapCPU});

  _heightMap = std::make_shared<Texture>(_heightMapGPU, _engineState->getSettings()->getLoadTextureAuxilaryFormat(),
                                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, VK_FILTER_LINEAR, commandBuffer,
                                         _engineState);
  auto [width, height] = _heightMap->getImageView()->getImage()->getResolution();
  _mesh = std::make_shared<MeshStatic3D>(_engineState);
  _calculateMesh(commandBuffer);

  _cameraBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _cameraBuffer[i] = std::make_shared<Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);

  // layout for Shadows
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutShadows{
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
         .pImmutableSamplers = nullptr},
        {.binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
         .pImmutableSamplers = nullptr},
        {.binding = 2,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
         .pImmutableSamplers = nullptr}};
    descriptorSetLayout->createCustom(layoutShadows);
    _descriptorSetLayoutShadows.push_back({"shadows", descriptorSetLayout});

    _cameraBufferDepth.resize(_engineState->getSettings()->getMaxFramesInFlight());
    _descriptorSetCameraDepth.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      for (int l = 0; l < _engineState->getSettings()->getMaxDirectionalLights(); l++) {
        auto buffer = std::make_shared<Buffer>(
            sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);
        _cameraBufferDepth[i].push_back({buffer});

        auto cameraSet = std::make_shared<DescriptorSet>(descriptorSetLayout, _engineState);
        std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoNormalsMesh{
            {0, {{.buffer = buffer->getData(), .offset = 0, .range = buffer->getSize()}}},
            {1, {{.buffer = buffer->getData(), .offset = 0, .range = buffer->getSize()}}}};
        std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor{
            {2,
             {{.sampler = _heightMap->getSampler()->getSampler(),
               .imageView = _heightMap->getImageView()->getImageView(),
               .imageLayout = _heightMap->getImageView()->getImage()->getImageLayout()}}}};
        cameraSet->createCustom(bufferInfoNormalsMesh, textureInfoColor);
        _descriptorSetCameraDepth[i].push_back({cameraSet});
      }
      for (int l = 0; l < _engineState->getSettings()->getMaxPointLights(); l++) {
        std::vector<std::shared_ptr<Buffer>> facesBuffer(6);
        std::vector<std::shared_ptr<DescriptorSet>> facesSet(6);
        for (int f = 0; f < 6; f++) {
          facesBuffer[f] = std::make_shared<Buffer>(
              sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);
          facesSet[f] = std::make_shared<DescriptorSet>(descriptorSetLayout, _engineState);
          std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoNormalsMesh{
              {0, {{.buffer = facesBuffer[f]->getData(), .offset = 0, .range = facesBuffer[f]->getSize()}}},
              {1, {{.buffer = facesBuffer[f]->getData(), .offset = 0, .range = facesBuffer[f]->getSize()}}}};
          std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor{
              {2,
               {{.sampler = _heightMap->getSampler()->getSampler(),
                 .imageView = _heightMap->getImageView()->getImageView(),
                 .imageLayout = _heightMap->getImageView()->getImage()->getImageLayout()}}}};

          facesSet[f]->createCustom(bufferInfoNormalsMesh, textureInfoColor);
        }
        _cameraBufferDepth[i].push_back(facesBuffer);
        _descriptorSetCameraDepth[i].push_back(facesSet);
      }
    }

    // initialize Shadows (Directional)
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/terrain/terrainDepth_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/terrain/terrainDepth_control.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
      shader->add("shaders/terrain/terrainDepth_evaluation.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
      shader->add("shaders/terrain/terrainDepthDirectional_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

      std::map<std::string, VkPushConstantRange> pushConstants;
      pushConstants["controlDepth"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
          .offset = 0,
          .size = sizeof(TesselationControlPushDepth),
      };
      pushConstants["evaluateDepth"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
          .offset = sizeof(TesselationControlPushDepth),
          .size = sizeof(TesselationEvaluationPushDepth),
      };

      _pipelineDirectional = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                               _engineState->getDevice());
      _pipelineDirectional->setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipelineDirectional->setDepthBias(true);
      _pipelineDirectional->setColorBlendOp(VK_BLEND_OP_MIN);
      _pipelineDirectional->setTesselation(4);
      _pipelineDirectional->setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipelineDirectional->createCustom(
          shader, _descriptorSetLayoutShadows, pushConstants, _mesh->getBindingDescription(),
          _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                   {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}}),
          RenderPassScenario::SHADOW);
    }

    // initialize Shadows (Point)
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/terrain/terrainDepth_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/terrain/terrainDepth_control.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
      shader->add("shaders/terrain/terrainDepth_evaluation.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
      shader->add("shaders/terrain/terrainDepthPoint_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

      std::map<std::string, VkPushConstantRange> pushConstants;
      pushConstants["controlDepth"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
          .offset = 0,
          .size = sizeof(TesselationControlPushDepth),
      };
      pushConstants["evaluateDepth"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
          .offset = sizeof(TesselationControlPushDepth),
          .size = sizeof(TesselationEvaluationPushDepth),
      };
      pushConstants["fragmentDepth"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .offset = sizeof(TesselationControlPushDepth) +
                    std::max(sizeof(TesselationEvaluationPushDepth),
                             std::alignment_of<FragmentPointLightPushDepth>::value),
          .size = sizeof(FragmentPointLightPushDepth),
      };

      _pipelinePoint = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                         _engineState->getDevice());
      // we use different culling here because camera looks upside down for lighting because of some specific cubemap Y
      // coordinate stuff
      _pipelinePoint->setCullMode(VK_CULL_MODE_FRONT_BIT);
      _pipelinePoint->setDepthBias(true);
      _pipelinePoint->setColorBlendOp(VK_BLEND_OP_MIN);
      _pipelinePoint->setTesselation(4);
      _pipelinePoint->setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipelinePoint->createCustom(
          shader, _descriptorSetLayoutShadows, pushConstants, _mesh->getBindingDescription(),
          _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                   {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}}),
          RenderPassScenario::SHADOW);
    }
  }

  if (_patchRotationsIndex.size() == 0) _fillPatchRotationsDescription();
  if (_patchTextures.size() == 0) _fillPatchTexturesDescription();

  _patchDescriptionSSBO.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _allocatePatchDescription(i);
    _updatePatchDescription(i);
  }
  // layout for Color
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutColor{{.binding = 0,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 2,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 3,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 4,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 4,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr}};
    descriptorSetLayout->createCustom(layoutColor);
    _descriptorSetLayout[MaterialType::COLOR].push_back({"color", descriptorSetLayout});
    _descriptorSetLayout[MaterialType::COLOR].push_back(
        {"globalPhong", _gameState->getLightManager()->getDSLGlobalTerrainPhong()});

    _descriptorSetColor.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetColor[i] = std::make_shared<DescriptorSet>(descriptorSetLayout, _engineState);
    }
    setMaterial(_defaultMaterialColor);

    // initialize Color
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/terrain/composition/terrainColor_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/terrain/composition/terrainColor_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      shader->add("shaders/terrain/composition/terrainColor_control.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
      shader->add("shaders/terrain/composition/terrainColor_evaluation.spv",
                  VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
      _pipeline[MaterialType::COLOR] = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                                         _engineState->getDevice());

      std::map<std::string, VkPushConstantRange> pushConstants;
      pushConstants["control"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
          .offset = 0,
          .size = sizeof(TesselationControlPush),
      };
      pushConstants["evaluate"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
          .offset = sizeof(TesselationControlPush),
          .size = sizeof(TesselationEvaluationPush),
      };
      pushConstants["fragment"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .offset = sizeof(TesselationControlPush) + sizeof(TesselationEvaluationPush),
          .size = sizeof(FragmentPush),
      };

      _pipeline[MaterialType::COLOR]->setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipeline[MaterialType::COLOR]->setDepthTest(true);
      _pipeline[MaterialType::COLOR]->setDepthWrite(true);
      _pipeline[MaterialType::COLOR]->setTesselation(4);
      _pipeline[MaterialType::COLOR]->setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipeline[MaterialType::COLOR]->createCustom(
          shader, _descriptorSetLayout[MaterialType::COLOR], pushConstants, _mesh->getBindingDescription(),
          _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                   {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}}),
          RenderPassScenario::GRAPHIC);
    }
  }
  // layout for Phong
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutPhong{{.binding = 0,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 2,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 3,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 4,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 4,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 5,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 4,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 6,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 4,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 7,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr}};
    descriptorSetLayout->createCustom(layoutPhong);

    _descriptorSetLayout[MaterialType::PHONG].push_back({"phong", descriptorSetLayout});
    _descriptorSetLayout[MaterialType::PHONG].push_back(
        {"globalPhong", _gameState->getLightManager()->getDSLGlobalTerrainPhong()});

    _descriptorSetPhong.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetPhong[i] = std::make_shared<DescriptorSet>(descriptorSetLayout, _engineState);
    }
    // update descriptor set in setMaterial

    // initialize Phong
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/terrain/composition/terrainColor_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/terrain/composition/terrainPhong_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      shader->add("shaders/terrain/composition/terrainColor_control.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
      shader->add("shaders/terrain/composition/terrainColor_evaluation.spv",
                  VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
      _pipeline[MaterialType::PHONG] = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                                         _engineState->getDevice());

      std::map<std::string, VkPushConstantRange> pushConstants;
      pushConstants["control"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
          .offset = 0,
          .size = sizeof(TesselationControlPush),
      };
      pushConstants["evaluate"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
          .offset = sizeof(TesselationControlPush),
          .size = sizeof(TesselationEvaluationPush),
      };
      pushConstants["fragment"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .offset = sizeof(TesselationControlPush) + sizeof(TesselationEvaluationPush),
          .size = sizeof(FragmentPush),
      };

      _pipeline[MaterialType::PHONG]->setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipeline[MaterialType::PHONG]->setDepthTest(true);
      _pipeline[MaterialType::PHONG]->setDepthWrite(true);
      _pipeline[MaterialType::PHONG]->setTesselation(4);
      _pipeline[MaterialType::PHONG]->setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipeline[MaterialType::PHONG]->createCustom(
          shader, _descriptorSetLayout[MaterialType::PHONG], pushConstants, _mesh->getBindingDescription(),
          _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                   {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}}),
          RenderPassScenario::GRAPHIC);
    }
  }
  // layout for PBR
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutPBR{{.binding = 0,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 1,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 2,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 3,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 4,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 4,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 5,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 4,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 6,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 4,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 7,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 4,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 8,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 4,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 9,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 4,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 10,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 11,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 12,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 13,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 14,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr}};
    descriptorSetLayout->createCustom(layoutPBR);
    _descriptorSetLayout[MaterialType::PBR].push_back({"pbr", descriptorSetLayout});
    _descriptorSetLayout[MaterialType::PBR].push_back(
        {"globalPBR", _gameState->getLightManager()->getDSLGlobalTerrainPBR()});

    _descriptorSetPBR.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetPBR[i] = std::make_shared<DescriptorSet>(descriptorSetLayout, _engineState);
    }
    // update descriptor set in setMaterial

    // initialize PBR
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/terrain/composition/terrainColor_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/terrain/composition/terrainPBR_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      shader->add("shaders/terrain/composition/terrainColor_control.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
      shader->add("shaders/terrain/composition/terrainColor_evaluation.spv",
                  VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
      _pipeline[MaterialType::PBR] = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                                       _engineState->getDevice());

      std::map<std::string, VkPushConstantRange> pushConstants;
      pushConstants["control"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
          .offset = 0,
          .size = sizeof(TesselationControlPush),
      };
      pushConstants["evaluate"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
          .offset = sizeof(TesselationControlPush),
          .size = sizeof(TesselationEvaluationPush),
      };
      pushConstants["fragment"] = VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .offset = sizeof(TesselationControlPush) + sizeof(TesselationEvaluationPush),
          .size = sizeof(FragmentPush),
      };

      _pipeline[MaterialType::PBR]->setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipeline[MaterialType::PBR]->setDepthTest(true);
      _pipeline[MaterialType::PBR]->setDepthWrite(true);
      _pipeline[MaterialType::PBR]->setTesselation(4);
      _pipeline[MaterialType::PBR]->setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipeline[MaterialType::PBR]->createCustom(
          shader, _descriptorSetLayout[MaterialType::PBR], pushConstants, _mesh->getBindingDescription(),
          _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                   {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}}),
          RenderPassScenario::GRAPHIC);
    }
  }
}

void TerrainComposition::_fillPatchTexturesDescription() {
  _patchTextures = std::vector<int>(_patchNumber.first * _patchNumber.second, 0);
  auto [width, height] = _heightMap->getImageView()->getImage()->getResolution();
  //
  for (int y = 0; y < _patchNumber.second; y++)
    for (int x = 0; x < _patchNumber.first; x++) {
      int patchX0 = (float)x / _patchNumber.first * width;
      int patchY0 = (float)y / _patchNumber.second * height;
      int patchX1 = (float)(x + 1) / _patchNumber.first * width;
      int patchY1 = (float)(y + 1) / _patchNumber.second * height;

      int heightLevel = 0;
      for (int i = patchY0; i < patchY1; i++) {
        for (int j = patchX0; j < patchX1; j++) {
          int textureCoord = (j + i * width) * _heightMapCPU->getChannels();
          heightLevel = std::max(heightLevel, (int)_heightMapCPU->getData()[textureCoord]);
        }
      }

      int index = x + y * _patchNumber.first;
      if (heightLevel < _heightLevels[0]) {
        _patchTextures[index] = 0;
      } else if (heightLevel < _heightLevels[1]) {
        _patchTextures[index] = 1;
      } else if (heightLevel < _heightLevels[2]) {
        _patchTextures[index] = 2;
      } else {
        _patchTextures[index] = 3;
      }
    }
}

void TerrainComposition::_fillPatchRotationsDescription() {
  _patchRotationsIndex = std::vector<int>(_patchNumber.first * _patchNumber.second, 0);
}

void TerrainComposition::_allocatePatchDescription(int currentFrame) {
  _patchDescriptionSSBO[currentFrame] = std::make_shared<Buffer>(
      _patchNumber.first * _patchNumber.second * sizeof(PatchDescription), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);
}

void TerrainComposition::_updatePatchDescription(int currentFrame) {
  std::vector<PatchDescription> patchData(_patchNumber.first * _patchNumber.second);
  for (int i = 0; i < patchData.size(); i++) {
    patchData[i] = PatchDescription({.rotation = 90 * _patchRotationsIndex[i], .textureID = _patchTextures[i]});
  }
  // fill only number of lights because it's stub
  _patchDescriptionSSBO[currentFrame]->setData(patchData.data(), patchData.size() * sizeof(PatchDescription));
}

void TerrainComposition::_updateColorDescriptor() {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialColor>(_material);
  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor{
      {0,
       {{.buffer = _cameraBuffer[currentFrame]->getData(),
         .offset = 0,
         .range = _cameraBuffer[currentFrame]->getSize()}}},
      {1,
       {{.buffer = _patchDescriptionSSBO[currentFrame]->getData(),
         .offset = 0,
         .range = _patchDescriptionSSBO[currentFrame]->getSize()}}},
      {2,
       {{.buffer = _cameraBuffer[currentFrame]->getData(),
         .offset = 0,
         .range = _cameraBuffer[currentFrame]->getSize()}}}};

  std::vector<VkDescriptorImageInfo> textureBaseColor(material->getBaseColor().size());
  for (int j = 0; j < material->getBaseColor().size(); j++) {
    textureBaseColor[j] = {.sampler = material->getBaseColor()[j]->getSampler()->getSampler(),
                           .imageView = material->getBaseColor()[j]->getImageView()->getImageView(),
                           .imageLayout = material->getBaseColor()[j]->getImageView()->getImage()->getImageLayout()};
  }
  std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor{
      {3,
       {{.sampler = _heightMap->getSampler()->getSampler(),
         .imageView = _heightMap->getImageView()->getImageView(),
         .imageLayout = _heightMap->getImageView()->getImage()->getImageLayout()}}},
      {4, {textureBaseColor}}};

  _descriptorSetColor[currentFrame]->createCustom(bufferInfoColor, textureInfoColor);
}

void TerrainComposition::_updatePhongDescriptor() {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialPhong>(_material);
  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor{
      {0,
       {{.buffer = _cameraBuffer[currentFrame]->getData(),
         .offset = 0,
         .range = _cameraBuffer[currentFrame]->getSize()}}},
      {1,
       {{.buffer = _patchDescriptionSSBO[currentFrame]->getData(),
         .offset = 0,
         .range = _patchDescriptionSSBO[currentFrame]->getSize()}}},
      {2,
       {{.buffer = _cameraBuffer[currentFrame]->getData(),
         .offset = 0,
         .range = _cameraBuffer[currentFrame]->getSize()}}},
      {7,
       {{.buffer = material->getBufferCoefficients()[currentFrame]->getData(),
         .offset = 0,
         .range = material->getBufferCoefficients()[currentFrame]->getSize()}}}};
  std::vector<VkDescriptorImageInfo> textureBaseColor(material->getBaseColor().size());
  for (int j = 0; j < material->getBaseColor().size(); j++) {
    textureBaseColor[j] = {.sampler = material->getBaseColor()[j]->getSampler()->getSampler(),
                           .imageView = material->getBaseColor()[j]->getImageView()->getImageView(),
                           .imageLayout = material->getBaseColor()[j]->getImageView()->getImage()->getImageLayout()};
  }
  std::vector<VkDescriptorImageInfo> textureBaseNormal(material->getNormal().size());
  for (int j = 0; j < material->getNormal().size(); j++) {
    textureBaseNormal[j] = {.sampler = material->getNormal()[j]->getSampler()->getSampler(),
                            .imageView = material->getNormal()[j]->getImageView()->getImageView(),
                            .imageLayout = material->getNormal()[j]->getImageView()->getImage()->getImageLayout()};
  }
  std::vector<VkDescriptorImageInfo> textureBaseSpecular(material->getSpecular().size());
  for (int j = 0; j < material->getSpecular().size(); j++) {
    textureBaseSpecular[j] = {.sampler = material->getSpecular()[j]->getSampler()->getSampler(),
                              .imageView = material->getSpecular()[j]->getImageView()->getImageView(),
                              .imageLayout = material->getSpecular()[j]->getImageView()->getImage()->getImageLayout()};
  }
  std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor{
      {3,
       {{.sampler = _heightMap->getSampler()->getSampler(),
         .imageView = _heightMap->getImageView()->getImageView(),
         .imageLayout = _heightMap->getImageView()->getImage()->getImageLayout()}}},
      {4, {textureBaseColor}},
      {5, {textureBaseNormal}},
      {6, {textureBaseSpecular}}};
  _descriptorSetPhong[currentFrame]->createCustom(bufferInfoColor, textureInfoColor);
}

void TerrainComposition::_updatePBRDescriptor() {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialPBR>(_material);
  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor{
      {0,
       {{.buffer = _cameraBuffer[currentFrame]->getData(),
         .offset = 0,
         .range = _cameraBuffer[currentFrame]->getSize()}}},
      {1,
       {{.buffer = _patchDescriptionSSBO[currentFrame]->getData(),
         .offset = 0,
         .range = _patchDescriptionSSBO[currentFrame]->getSize()}}},
      {2,
       {{.buffer = _cameraBuffer[currentFrame]->getData(),
         .offset = 0,
         .range = _cameraBuffer[currentFrame]->getSize()}}},
      {13,
       {{.buffer = material->getBufferCoefficients()[currentFrame]->getData(),
         .offset = 0,
         .range = material->getBufferCoefficients()[currentFrame]->getSize()}}},
      {14,
       {{.buffer = material->getBufferAlphaCutoff()[currentFrame]->getData(),
         .offset = 0,
         .range = material->getBufferAlphaCutoff()[currentFrame]->getSize()}}}};
  std::vector<VkDescriptorImageInfo> textureBaseColor(material->getBaseColor().size());
  for (int j = 0; j < material->getBaseColor().size(); j++) {
    textureBaseColor[j] = {.sampler = material->getBaseColor()[j]->getSampler()->getSampler(),
                           .imageView = material->getBaseColor()[j]->getImageView()->getImageView(),
                           .imageLayout = material->getBaseColor()[j]->getImageView()->getImage()->getImageLayout()};
  }
  std::vector<VkDescriptorImageInfo> textureBaseNormal(material->getNormal().size());
  for (int j = 0; j < material->getNormal().size(); j++) {
    textureBaseNormal[j] = {.sampler = material->getNormal()[j]->getSampler()->getSampler(),
                            .imageView = material->getNormal()[j]->getImageView()->getImageView(),
                            .imageLayout = material->getNormal()[j]->getImageView()->getImage()->getImageLayout()};
  }
  std::vector<VkDescriptorImageInfo> textureBaseMetallic(material->getMetallic().size());
  for (int j = 0; j < material->getMetallic().size(); j++) {
    textureBaseMetallic[j] = {.sampler = material->getMetallic()[j]->getSampler()->getSampler(),
                              .imageView = material->getMetallic()[j]->getImageView()->getImageView(),
                              .imageLayout = material->getMetallic()[j]->getImageView()->getImage()->getImageLayout()};
  }
  std::vector<VkDescriptorImageInfo> textureBaseRoughness(material->getRoughness().size());
  for (int j = 0; j < material->getRoughness().size(); j++) {
    textureBaseRoughness[j] = {
        .sampler = material->getRoughness()[j]->getSampler()->getSampler(),
        .imageView = material->getRoughness()[j]->getImageView()->getImageView(),
        .imageLayout = material->getRoughness()[j]->getImageView()->getImage()->getImageLayout()};
  }
  std::vector<VkDescriptorImageInfo> textureBaseOcclusion(material->getOccluded().size());
  for (int j = 0; j < material->getOccluded().size(); j++) {
    textureBaseOcclusion[j] = {.sampler = material->getOccluded()[j]->getSampler()->getSampler(),
                               .imageView = material->getOccluded()[j]->getImageView()->getImageView(),
                               .imageLayout = material->getOccluded()[j]->getImageView()->getImage()->getImageLayout()};
  }
  std::vector<VkDescriptorImageInfo> textureBaseEmissive(material->getEmissive().size());
  for (int j = 0; j < material->getEmissive().size(); j++) {
    textureBaseEmissive[j] = {.sampler = material->getEmissive()[j]->getSampler()->getSampler(),
                              .imageView = material->getEmissive()[j]->getImageView()->getImageView(),
                              .imageLayout = material->getEmissive()[j]->getImageView()->getImage()->getImageLayout()};
  }
  std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor{
      {3,
       {{.sampler = _heightMap->getSampler()->getSampler(),
         .imageView = _heightMap->getImageView()->getImageView(),
         .imageLayout = _heightMap->getImageView()->getImage()->getImageLayout()}}},
      {4, textureBaseColor},
      {5, textureBaseNormal},
      {6, textureBaseMetallic},
      {7, {textureBaseRoughness}},
      {8, {textureBaseOcclusion}},
      {9, {textureBaseEmissive}},
      {10,
       {{.sampler = material->getDiffuseIBL()->getSampler()->getSampler(),
         .imageView = material->getDiffuseIBL()->getImageView()->getImageView(),
         .imageLayout = material->getDiffuseIBL()->getImageView()->getImage()->getImageLayout()}}},
      {11,
       {{.sampler = material->getSpecularIBL()->getSampler()->getSampler(),
         .imageView = material->getSpecularIBL()->getImageView()->getImageView(),
         .imageLayout = material->getSpecularIBL()->getImageView()->getImage()->getImageLayout()}}},
      {12,
       {{.sampler = material->getSpecularBRDF()->getSampler()->getSampler(),
         .imageView = material->getSpecularBRDF()->getImageView()->getImageView(),
         .imageLayout = material->getSpecularBRDF()->getImageView()->getImage()->getImageLayout()}}}};
  _descriptorSetPBR[currentFrame]->createCustom(bufferInfoColor, textureInfoColor);
}

void TerrainComposition::draw(std::shared_ptr<CommandBuffer> commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  auto drawTerrain = [&](std::shared_ptr<Pipeline> pipeline) {
    vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());

    auto resolution = _engineState->getSettings()->getResolution();
    VkViewport viewport{.x = 0.0f,
                        .y = static_cast<float>(std::get<1>(resolution)),
                        .width = static_cast<float>(std::get<0>(resolution)),
                        .height = static_cast<float>(-std::get<1>(resolution)),
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f};
    vkCmdSetViewport(commandBuffer->getCommandBuffer(), 0, 1, &viewport);

    VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(std::get<0>(resolution), std::get<1>(resolution))};

    vkCmdSetScissor(commandBuffer->getCommandBuffer(), 0, 1, &scissor);

    if (pipeline->getPushConstants().find("control") != pipeline->getPushConstants().end()) {
      TesselationControlPush pushConstants{.patchDimX = _patchNumber.first,
                                           .patchDimY = _patchNumber.second,
                                           .minTessellationLevel = _minTessellationLevel,
                                           .maxTessellationLevel = _maxTessellationLevel,
                                           .minTesselationDistance = _minTesselationDistance,
                                           .maxTesselationDistance = _maxTesselationDistance};

      auto info = pipeline->getPushConstants()["control"];
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    if (pipeline->getPushConstants().find("evaluate") != pipeline->getPushConstants().end()) {
      TesselationEvaluationPush pushConstants{.patchDimX = _patchNumber.first,
                                              .patchDimY = _patchNumber.second,
                                              .heightScale = _heightScale,
                                              .heightShift = _heightShift};

      auto info = pipeline->getPushConstants()["evaluate"];
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    if (pipeline->getPushConstants().find("fragment") != pipeline->getPushConstants().end()) {
      FragmentPush pushConstants{.enableShadow = _enableShadow,
                                 .enableLighting = _enableLighting,
                                 .cameraPosition = _gameState->getCameraManager()->getCurrentCamera()->getEye()};

      auto info = pipeline->getPushConstants()["fragment"];
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    // same buffer to both tessellation shaders because we're not going to change camera between these 2 stages
    BufferMVP cameraUBO{.model = getModel(),
                        .view = _gameState->getCameraManager()->getCurrentCamera()->getView(),
                        .projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection()};

    _cameraBuffer[currentFrame]->setData(&cameraUBO);

    VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getData()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

    // color
    auto pipelineLayout = pipeline->getDescriptorSetLayout();
    auto colorLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                    [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                      return info.first == std::string("color");
                                    });
    if (colorLayout != pipelineLayout.end()) {
      vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->getPipelineLayout(), 0, 1,
                              &_descriptorSetColor[currentFrame]->getDescriptorSets(), 0, nullptr);
    }

    // Phong
    auto phongLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                    [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                      return info.first == std::string("phong");
                                    });
    if (phongLayout != pipelineLayout.end()) {
      vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->getPipelineLayout(), 0, 1,
                              &_descriptorSetPhong[currentFrame]->getDescriptorSets(), 0, nullptr);
    }

    // global Phong
    auto globalLayoutPhong = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                          [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                            return info.first == std::string("globalPhong");
                                          });
    if (globalLayoutPhong != pipelineLayout.end()) {
      vkCmdBindDescriptorSets(
          commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(), 1, 1,
          &_gameState->getLightManager()->getDSGlobalTerrainPhong()->getDescriptorSets(), 0, nullptr);
    }

    // PBR
    auto pbrLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                  [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                    return info.first == std::string("pbr");
                                  });
    if (pbrLayout != pipelineLayout.end()) {
      vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->getPipelineLayout(), 0, 1,
                              &_descriptorSetPBR[currentFrame]->getDescriptorSets(), 0, nullptr);
    }

    // global PBR
    auto globalLayoutPBR = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                        [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                          return info.first == std::string("globalPBR");
                                        });
    if (globalLayoutPBR != pipelineLayout.end()) {
      vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->getPipelineLayout(), 1, 1,
                              &_gameState->getLightManager()->getDSGlobalTerrainPBR()->getDescriptorSets(), 0, nullptr);
    }

    vkCmdDraw(commandBuffer->getCommandBuffer(), _mesh->getVertexData().size(), 1, 0, 0);
  };

  if (_changedMaterial[currentFrame]) {
    switch (_materialType) {
      case MaterialType::COLOR:
        _updateColorDescriptor();
        break;
      case MaterialType::PHONG:
        _updatePhongDescriptor();
        break;
      case MaterialType::PBR:
        _updatePBRDescriptor();
        break;
    }
    _changedMaterial[currentFrame] = false;
  }

  auto pipeline = _pipeline[_materialType];
  drawTerrain(pipeline);
}

void TerrainComposition::drawShadow(LightType lightType,
                                    int lightIndex,
                                    int face,
                                    std::shared_ptr<CommandBuffer> commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  auto pipeline = _pipelineDirectional;
  if (lightType == LightType::POINT) pipeline = _pipelinePoint;

  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());

  std::tuple<int, int> resolution = _engineState->getSettings()->getShadowMapResolution();

  // Cube Maps have been specified to follow the RenderMan specification (for whatever reason),
  // and RenderMan assumes the images' origin being in the upper left so we don't need to swap anything
  VkViewport viewport{};
  if (lightType == LightType::DIRECTIONAL) {
    viewport = {.x = 0.0f,
                .y = static_cast<float>(std::get<1>(resolution)),
                .width = static_cast<float>(std::get<0>(resolution)),
                .height = static_cast<float>(-std::get<1>(resolution))};
  } else if (lightType == LightType::POINT) {
    viewport = {.x = 0.0f,
                .y = 0.f,
                .width = static_cast<float>(std::get<0>(resolution)),
                .height = static_cast<float>(std::get<1>(resolution))};
  }
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer->getCommandBuffer(), 0, 1, &viewport);

  VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(std::get<0>(resolution), std::get<1>(resolution))};
  vkCmdSetScissor(commandBuffer->getCommandBuffer(), 0, 1, &scissor);

  glm::mat4 view(1.f);
  glm::mat4 projection(1.f);
  float far;
  int lightIndexTotal = lightIndex;
  if (lightType == LightType::DIRECTIONAL) {
    view = _gameState->getLightManager()->getDirectionalLights()[lightIndex]->getCamera()->getView();
    projection = _gameState->getLightManager()->getDirectionalLights()[lightIndex]->getCamera()->getProjection();
    far = _gameState->getLightManager()->getDirectionalLights()[lightIndex]->getCamera()->getFar();
  }
  if (lightType == LightType::POINT) {
    lightIndexTotal += _engineState->getSettings()->getMaxDirectionalLights();
    view = _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getView(face);
    projection = _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getProjection();
    far = _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getFar();
  }

  if (pipeline->getPushConstants().find("controlDepth") != pipeline->getPushConstants().end()) {
    TesselationControlPushDepth pushConstants{.minTessellationLevel = _minTessellationLevel,
                                              .maxTessellationLevel = _maxTessellationLevel,
                                              .minTesselationDistance = _minTesselationDistance,
                                              .maxTesselationDistance = _maxTesselationDistance};

    auto info = pipeline->getPushConstants()["controlDepth"];
    vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                       info.size, &pushConstants);
  }

  if (pipeline->getPushConstants().find("evaluateDepth") != pipeline->getPushConstants().end()) {
    TesselationEvaluationPushDepth pushConstants{.heightScale = _heightScale, .heightShift = _heightShift};

    auto info = pipeline->getPushConstants()["evaluateDepth"];
    vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                       info.size, &pushConstants);
  }

  if (pipeline->getPushConstants().find("fragmentDepth") != pipeline->getPushConstants().end()) {
    FragmentPointLightPushDepth pushConstants{
        .lightPosition = _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getPosition(),
        .far = static_cast<int>(far)};

    auto info = pipeline->getPushConstants()["fragmentDepth"];
    vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                       info.size, &pushConstants);
  }

  // same buffer to both tessellation shaders because we're not going to change camera between these 2 stages
  BufferMVP cameraUBO{.model = getModel(), .view = view, .projection = projection};

  _cameraBufferDepth[currentFrame][lightIndexTotal][face]->setData(&cameraUBO);

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getData()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  auto pipelineLayout = pipeline->getDescriptorSetLayout();
  auto normalLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                   [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                     return info.first == std::string("shadows");
                                   });
  if (normalLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(
        commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(), 0, 1,
        &_descriptorSetCameraDepth[currentFrame][lightIndexTotal][face]->getDescriptorSets(), 0, nullptr);
  }

  vkCmdDraw(commandBuffer->getCommandBuffer(), _mesh->getVertexData().size(), 1, 0, 0);
}