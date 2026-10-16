#include <cstring>
#include <future>
#include <type_traits>
#include "stb_image_write.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include "Primitive/TerrainComposition.h"

// Keep standard-library declarations outside the imported module with MSVC.
import Shader;
#undef far
#undef near

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
                                                 const RenderGraph::CommandBuffer* commandBufferTransfer,
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
  _defaultMaterialColor = std::make_shared<MaterialColor>(MaterialTarget::TERRAIN, engineState);
  _defaultMaterialColor->setBaseColor(std::vector{4, _gameState->getResourceManager()->getTextureOne()});
  _material = _defaultMaterialColor;
  _heightMapCPU = heightMapCPU;
  auto& imageManager = _gameState->getResourceManager()->getImageManager();
  _heightMapGPU = imageManager.loadGPU<uint8_t>({heightMapCPU});
  _changedHeightmap.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _heightMap = imageManager.createTexture(_heightMapGPU, _engineState->getSettings()->getLoadTextureAuxilaryFormat(),
                                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, VK_FILTER_LINEAR);

  _changeMesh.resize(engineState->getSettings()->getMaxFramesInFlight());
  _mesh.resize(engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _mesh[i] = std::make_shared<MeshDynamic3D>(engineState);
    _calculateMesh(i);
  }
  const auto vertexBinding = _mesh.front()->getBindingDescription();
  const auto vertexAttributes = _mesh.front()->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}, {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}});
  const VkPipelineVertexInputStateCreateInfo vertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
      .pVertexAttributeDescriptions = vertexAttributes.data(),
  };

  _cameraBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _cameraBuffer[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *engineState->getMemoryAllocator());

  // layout for Normal
  {
    auto shaderNormal = std::make_shared<RenderGraph::Shader>(*engineState->getDevice());
    shaderNormal->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                    "shaders/terrain/terrainDepth_vertex.spv"));
    shaderNormal->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                    "shaders/terrain/terrainNormal_fragment.spv"));
    shaderNormal->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                    "shaders/terrain/terrainDepth_control.spv"));
    shaderNormal->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                    "shaders/terrain/terrainNormal_evaluation.spv"));
    shaderNormal->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                    "shaders/terrain/terrainNormal_geometry.spv"));
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(shaderNormal->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetLayoutNormalsMesh.push_back(descriptorSetLayout);

    _descriptorSetNormal.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetNormal[i] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(descriptorSetLayout).get()},
          *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
      _descriptorSetNormal[i]->add({_cameraBuffer[i].get()});
      _descriptorSetNormal[i]->add({_cameraBuffer[i].get()});
      _descriptorSetNormal[i]->add({_heightMap.get()});
      _descriptorSetNormal[i]->add({_cameraBuffer[i].get()});
      _descriptorSetNormal[i]->initialize(*commandBufferTransfer);
    }

    // initialize Normal (per vertex)
    {
      std::unordered_map<std::string, VkPushConstantRange> pushConstants;
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
      std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts;
      for (const auto& layout : _descriptorSetLayoutNormalsMesh) descriptorSetLayouts.push_back(layout.get());
      RenderGraph::PipelineGraphic pipelineGraphic;
      pipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      pipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      pipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
      pipelineGraphic.setDepthTest(true);
      pipelineGraphic.setDepthWrite(true);
      pipelineGraphic.setTesselation(4);
      pipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipelineNormalMesh = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipelineNormalMesh->createGraphic(pipelineGraphic, shaderNormal->getShaderStageInfo(), descriptorSetLayouts,
                                         pushConstants, vertexInput);
    }

    // initialize Tangent (per vertex)
    {
      auto tangentShader = std::make_shared<RenderGraph::Shader>(*engineState->getDevice());
      tangentShader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                       "shaders/terrain/terrainDepth_vertex.spv"));
      tangentShader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                       "shaders/terrain/terrainNormal_fragment.spv"));
      tangentShader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                       "shaders/terrain/terrainDepth_control.spv"));
      tangentShader->add(_engineState->getFilesystem()->readFile<char>(
          _engineState->getSettings()->getShadersPath() + "shaders/terrain/terrainTangent_evaluation.spv"));
      tangentShader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                       "shaders/terrain/terrainNormal_geometry.spv"));

      std::unordered_map<std::string, VkPushConstantRange> pushConstants;
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

      std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts;
      for (const auto& layout : _descriptorSetLayoutNormalsMesh) descriptorSetLayouts.push_back(layout.get());
      RenderGraph::PipelineGraphic pipelineGraphic;
      pipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      pipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      pipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
      pipelineGraphic.setDepthTest(true);
      pipelineGraphic.setDepthWrite(true);
      pipelineGraphic.setTesselation(4);
      pipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipelineTangentMesh = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipelineTangentMesh->createGraphic(pipelineGraphic, tangentShader->getShaderStageInfo(), descriptorSetLayouts,
                                          pushConstants, vertexInput);
    }
  }

  _reallocatePatch.resize(engineState->getSettings()->getMaxFramesInFlight());
  _patchDescriptionSSBO.resize(engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _reallocatePatchDescription(i);
  }
  // layout for Color
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/composition/terrainDebug_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/composition/terrainDebug_fragment.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/composition/terrainDebug_control.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(
        _engineState->getSettings()->getShadersPath() + "shaders/terrain/composition/terrainDebug_evaluation.spv"));
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetLayout.push_back(descriptorSetLayout);

    _descriptorSetColor.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetColor[i] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(descriptorSetLayout).get()},
          *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
    }
    setMaterial(_defaultMaterialColor);

    // initialize Color
    {
      std::unordered_map<std::string, VkPushConstantRange> pushConstants;
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

      std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts;
      for (const auto& layout : _descriptorSetLayout) descriptorSetLayouts.push_back(layout.get());
      RenderGraph::PipelineGraphic pipelineGraphic;
      pipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      pipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      pipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
      pipelineGraphic.setDepthTest(true);
      pipelineGraphic.setDepthWrite(true);
      pipelineGraphic.setTesselation(4);
      pipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipeline = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipeline->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts, pushConstants,
                               vertexInput);

      RenderGraph::PipelineGraphic wireframePipelineGraphic;
      wireframePipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      wireframePipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      wireframePipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
#ifndef __ANDROID__
      wireframePipelineGraphic.setPolygonMode(VK_POLYGON_MODE_LINE);
#endif
      wireframePipelineGraphic.setDepthTest(true);
      wireframePipelineGraphic.setDepthWrite(true);
      wireframePipelineGraphic.setTesselation(4);
      wireframePipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipelineWireframe = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipelineWireframe->createGraphic(wireframePipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts,
                                        pushConstants, vertexInput);
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

void TerrainCompositionDebug::_updateColorDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialColor>(_material);
  material->unregisterUpdate(_descriptorSetColor[currentFrame]);
  std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts;
  for (const auto& layout : _descriptorSetLayout) descriptorSetLayouts.push_back(layout.get());
  _descriptorSetColor[currentFrame] = std::make_shared<RenderGraph::DescriptorSet>(
      descriptorSetLayouts, *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
  _descriptorSetColor[currentFrame]->add({_cameraBuffer[currentFrame].get()});
  _descriptorSetColor[currentFrame]->add({_patchDescriptionSSBO[currentFrame].get()});
  _descriptorSetColor[currentFrame]->add({_cameraBuffer[currentFrame].get()});
  _descriptorSetColor[currentFrame]->add({_heightMap.get()});
  std::vector<RenderGraph::Texture*> baseColors;
  baseColors.reserve(material->getBaseColor().size());
  for (const auto& texture : material->getBaseColor()) baseColors.push_back(texture.get());
  _descriptorSetColor[currentFrame]->add(std::move(baseColors));
  _descriptorSetColor[currentFrame]->initialize(commandBuffer);
  material->registerUpdate(_descriptorSetColor[currentFrame],
                           {.frameInFlight = currentFrame, .materialInfo = {{MaterialTexture::COLOR, 4}}});
}

void TerrainCompositionDebug::_reallocatePatchDescription(int currentFrame) {
  _patchTextures = std::vector<int>(_patchNumber.first * _patchNumber.second, 0);
  auto [width, height] = std::tuple{(_heightMap->getImageView().getImage()).getResolution().x,
                                    (_heightMap->getImageView().getImage()).getResolution().y};
  //
  for (int y = 0; y < _patchNumber.second; y++)
    for (int x = 0; x < _patchNumber.first; x++) {
      const int patchX0 = x * width / _patchNumber.first;
      const int patchY0 = y * height / _patchNumber.second;
      const int patchX1 = (x + 1) * width / _patchNumber.first;
      const int patchY1 = (y + 1) * height / _patchNumber.second;

      int heightLevel = 0;
      for (int i = patchY0; i < patchY1; i++) {
        for (int j = patchX0; j < patchX1; j++) {
          int textureCoord = (j + i * width) * _heightMapCPU->getChannels();
          heightLevel = std::max(heightLevel, static_cast<int>(_heightMapCPU->getData()[textureCoord]));
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
  _patchDescriptionSSBO[currentFrame] = std::make_shared<RenderGraph::Buffer>(
      _patchNumber.first * _patchNumber.second * sizeof(PatchDescription), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      *_engineState->getMemoryAllocator());
}

void TerrainCompositionDebug::_updatePatchDescription(int currentFrame) {
  std::vector<PatchDescription> patchData(_patchNumber.first * _patchNumber.second);
  for (int i = 0; i < patchData.size(); i++) {
    patchData[i] = PatchDescription({.rotation = 90 * _patchRotationsIndex[i], .textureID = _patchTextures[i]});
  }
  // fill only number of lights because it's stub
  std::memcpy(static_cast<std::byte*>(_patchDescriptionSSBO[currentFrame]->getAllocationInfo().pMappedData),
              patchData.data(), static_cast<std::size_t>(patchData.size() * sizeof(PatchDescription)));
}

void TerrainCompositionDebug::draw(const RenderGraph::CommandBuffer* commandBuffer) {
  const int currentFrame = _engineState->getFrameInFlight();
  auto drawTerrain = [&](std::shared_ptr<RenderGraph::Pipeline> pipeline) {
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
      auto info = pipeline->getPushConstants().at("control");
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    if (pipeline->getPushConstants().find("controlDepth") != pipeline->getPushConstants().end()) {
      TesselationControlPushDepth pushConstants{.minTessellationLevel = _minTessellationLevel,
                                                .maxTessellationLevel = _maxTessellationLevel,
                                                .minTesselationDistance = _minTesselationDistance,
                                                .maxTesselationDistance = _maxTesselationDistance};
      auto info = pipeline->getPushConstants().at("controlDepth");
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    if (pipeline->getPushConstants().find("evaluate") != pipeline->getPushConstants().end()) {
      TesselationEvaluationPush pushConstants{.patchDimX = _patchNumber.first,
                                              .patchDimY = _patchNumber.second,
                                              .heightScale = _heightScale,
                                              .heightShift = _heightShift};

      auto info = pipeline->getPushConstants().at("evaluate");
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    if (pipeline->getPushConstants().find("evaluateDepth") != pipeline->getPushConstants().end()) {
      TesselationEvaluationPushDepth pushConstants{.heightScale = _heightScale, .heightShift = _heightShift};
      auto info = pipeline->getPushConstants().at("evaluateDepth");
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    if (pipeline->getPushConstants().find("fragment") != pipeline->getPushConstants().end()) {
      FragmentPushDebug pushConstants{.patchEdge = _enableEdge,
                                      .showLOD = _showLoD,
                                      .enableShadow = _enableShadow,
                                      .enableLighting = _enableLighting,
                                      .tile = _pickedTile};

      auto info = pipeline->getPushConstants().at("fragment");
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    // same buffer to both tessellation shaders because we're not going to change camera between these 2 stages
    BufferMVP cameraUBO{.model = getModel(),
                        .view = _gameState->getCameraManager()->getCurrentCamera()->getView(),
                        .projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection()};

    std::memcpy(static_cast<std::byte*>(_cameraBuffer[currentFrame]->getAllocationInfo().pMappedData), &cameraUBO,
                static_cast<std::size_t>(_cameraBuffer[currentFrame]->getSize()));

    VkBuffer vertexBuffers[] = {_mesh[currentFrame]->getVertexBuffer()->getBuffer()->getBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

    auto descriptor = _descriptorSetColor[currentFrame];
    if (_drawType == DrawType::NORMAL || _drawType == DrawType::TANGENT)
      descriptor = _descriptorSetNormal[currentFrame];
    descriptor->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(), *commandBuffer);

    vkCmdDraw(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh[currentFrame]->getVertexData().size()), 1,
              0, 0);
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
    _updateColorDescriptor(*commandBuffer);
    _changedMaterial[currentFrame] = false;
  }

  if (_changedHeightmap[currentFrame]) {
    _updateColorDescriptor(*commandBuffer);

    _terrainCPU->setHeightmap(_terrainPhysics->getHeights());

    _changedHeightmap[currentFrame] = false;
  }

  auto pipeline = _pipeline;
  if (_drawType == DrawType::WIREFRAME) pipeline = _pipelineWireframe;
  if (_drawType == DrawType::NORMAL) pipeline = _pipelineNormalMesh;
  if (_drawType == DrawType::TANGENT) pipeline = _pipelineTangentMesh;
  drawTerrain(pipeline);
}

void TerrainCompositionDebug::drawDebug(const RenderGraph::CommandBuffer*) {
  int currentFrame = _engineState->getFrameInFlight();

  if (_changedHeightmap[currentFrame]) {
    _gameState->getResourceManager()->getImageManager().updateTexture(*_heightMap, _heightMapGPU);
  }
  if (_gui->startTree("Terrain")) {
    _gui->drawText({"Tile: " + std::to_string(_pickedTile)});
    _gui->drawText({"RenderGraph::Texture: " + std::to_string(_pickedPixel.x) + "x" + std::to_string(_pickedPixel.y)});
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
    textureList["##RenderGraph::Texture"] = &_textureIndex;
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

void TerrainCompositionDebug::mouseNotify(int button, int action, int) {
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

void TerrainCompositionDebug::keyNotify(int, int, int, int) {}

void TerrainCompositionDebug::charNotify(unsigned int) {}

void TerrainCompositionDebug::scrollNotify(double, double yOffset) {
  _changeHeightmap(_pickedPixel, static_cast<int>(yOffset));
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

void TerrainComposition::initialize(const RenderGraph::CommandBuffer* commandBuffer) {
  // needed for layout
  _defaultMaterialColor = std::make_shared<MaterialColor>(MaterialTarget::TERRAIN, _engineState);
  _defaultMaterialColor->setBaseColor(std::vector{4, _gameState->getResourceManager()->getTextureOne()});
  _material = _defaultMaterialColor;
  _changedMaterial.resize(_engineState->getSettings()->getMaxFramesInFlight(), false);

  auto& imageManager = _gameState->getResourceManager()->getImageManager();
  _heightMapGPU = imageManager.loadGPU<uint8_t>({_heightMapCPU});
  _heightMap = imageManager.createTexture(_heightMapGPU, _engineState->getSettings()->getLoadTextureAuxilaryFormat(),
                                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, VK_FILTER_LINEAR);
  auto [width, height] = std::tuple{(_heightMap->getImageView().getImage()).getResolution().x,
                                    (_heightMap->getImageView().getImage()).getResolution().y};
  _mesh = std::make_shared<MeshStatic3D>(_engineState);
  _calculateMesh();
  const auto vertexBinding = _mesh->getBindingDescription();
  const auto vertexAttributes = _mesh->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}, {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}});
  const VkPipelineVertexInputStateCreateInfo vertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
      .pVertexAttributeDescriptions = vertexAttributes.data(),
  };

  _cameraBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _cameraBuffer[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());

  // layout for Shadows
  {
    auto shaderDirectional = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shaderDirectional->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                         "shaders/terrain/terrainDepth_vertex.spv"));
    shaderDirectional->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                         "shaders/terrain/terrainDepth_control.spv"));
    shaderDirectional->add(_engineState->getFilesystem()->readFile<char>(
        _engineState->getSettings()->getShadersPath() + "shaders/terrain/terrainDepth_evaluation.spv"));
    shaderDirectional->add(_engineState->getFilesystem()->readFile<char>(
        _engineState->getSettings()->getShadersPath() + "shaders/terrain/terrainDepthDirectional_fragment.spv"));
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(shaderDirectional->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetLayoutShadows.push_back(descriptorSetLayout);

    _cameraBufferDepth.resize(_engineState->getSettings()->getMaxFramesInFlight());
    _descriptorSetCameraDepth.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      for (int l = 0; l < _engineState->getSettings()->getMaxDirectionalLights(); l++) {
        auto buffer = std::make_shared<RenderGraph::Buffer>(
            sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            *_engineState->getMemoryAllocator());
        _cameraBufferDepth[i].push_back({buffer});

        auto cameraSet = std::make_shared<RenderGraph::DescriptorSet>(
            std::vector<RenderGraph::DescriptorSetLayout*>{(descriptorSetLayout).get()},
            *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
        cameraSet->add({buffer.get()});
        cameraSet->add({buffer.get()});
        cameraSet->add({_heightMap.get()});
        cameraSet->initialize(*commandBuffer);
        _descriptorSetCameraDepth[i].push_back({cameraSet});
      }
      for (int l = 0; l < _engineState->getSettings()->getMaxPointLights(); l++) {
        std::vector<std::shared_ptr<RenderGraph::Buffer>> facesBuffer(6);
        std::vector<std::shared_ptr<RenderGraph::DescriptorSet>> facesSet(6);
        for (int f = 0; f < 6; f++) {
          facesBuffer[f] = std::make_shared<RenderGraph::Buffer>(
              sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
              VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
              *_engineState->getMemoryAllocator());
          facesSet[f] = std::make_shared<RenderGraph::DescriptorSet>(
              std::vector<RenderGraph::DescriptorSetLayout*>{(descriptorSetLayout).get()},
              *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
          facesSet[f]->add({facesBuffer[f].get()});
          facesSet[f]->add({facesBuffer[f].get()});
          facesSet[f]->add({_heightMap.get()});
          facesSet[f]->initialize(*commandBuffer);
        }
        _cameraBufferDepth[i].push_back(facesBuffer);
        _descriptorSetCameraDepth[i].push_back(facesSet);
      }
    }

    // initialize Shadows (Directional)
    {
      std::unordered_map<std::string, VkPushConstantRange> pushConstants;
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

      std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts;
      for (const auto& layout : _descriptorSetLayoutShadows) descriptorSetLayouts.push_back(layout.get());
      RenderGraph::PipelineGraphic pipelineGraphic;
      pipelineGraphic.setColorAttachments({_engineState->getSettings()->getShadowMapFormat()});
      pipelineGraphic.setDepthAttachment(std::nullopt);
      pipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
      pipelineGraphic.setDepthBias(true);
      pipelineGraphic.setColorBlendOp(VK_BLEND_OP_MIN);
      pipelineGraphic.setTesselation(4);
      pipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipelineDirectional = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipelineDirectional->createGraphic(pipelineGraphic, shaderDirectional->getShaderStageInfo(),
                                          descriptorSetLayouts, pushConstants, vertexInput);
    }

    // initialize Shadows (Point)
    {
      auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
      shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                "shaders/terrain/terrainDepth_vertex.spv"));
      shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                "shaders/terrain/terrainDepth_control.spv"));
      shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                "shaders/terrain/terrainDepth_evaluation.spv"));
      shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                "shaders/terrain/terrainDepthPoint_fragment.spv"));

      std::unordered_map<std::string, VkPushConstantRange> pushConstants;
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
          .offset = static_cast<uint32_t>(
              sizeof(TesselationControlPushDepth) +
              std::max(sizeof(TesselationEvaluationPushDepth), std::alignment_of<FragmentPointLightPushDepth>::value)),
          .size = sizeof(FragmentPointLightPushDepth),
      };

      std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts;
      for (const auto& layout : _descriptorSetLayoutShadows) descriptorSetLayouts.push_back(layout.get());
      RenderGraph::PipelineGraphic pipelineGraphic;
      pipelineGraphic.setColorAttachments({_engineState->getSettings()->getShadowMapFormat()});
      pipelineGraphic.setDepthAttachment(std::nullopt);
      // Point-light cubemap cameras require the opposite winding.
      pipelineGraphic.setCullMode(VK_CULL_MODE_FRONT_BIT);
      pipelineGraphic.setDepthBias(true);
      pipelineGraphic.setColorBlendOp(VK_BLEND_OP_MIN);
      pipelineGraphic.setTesselation(4);
      pipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipelinePoint = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipelinePoint->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts, pushConstants,
                                    vertexInput);
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
    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/composition/terrainColor_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/composition/terrainColor_fragment.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/composition/terrainColor_control.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(
        _engineState->getSettings()->getShadersPath() + "shaders/terrain/composition/terrainColor_evaluation.spv"));
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetLayout[MaterialType::COLOR].push_back(descriptorSetLayout);
    _descriptorSetLayout[MaterialType::COLOR].push_back(_gameState->getLightManager()->getDSLGlobalTerrainPhong());

    _descriptorSetColor.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetColor[i] = nullptr;
    }
    setMaterial(_defaultMaterialColor);

    // initialize Color
    {
      std::unordered_map<std::string, VkPushConstantRange> pushConstants;
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

      std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts;
      for (const auto& layout : _descriptorSetLayout[MaterialType::COLOR]) descriptorSetLayouts.push_back(layout.get());
      RenderGraph::PipelineGraphic pipelineGraphic;
      pipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      pipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      pipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
      pipelineGraphic.setDepthTest(true);
      pipelineGraphic.setDepthWrite(true);
      pipelineGraphic.setTesselation(4);
      pipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipeline[MaterialType::COLOR] = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipeline[MaterialType::COLOR]->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts,
                                                    pushConstants, vertexInput);
    }
  }
  // layout for Phong
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
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));

    _descriptorSetLayout[MaterialType::PHONG].push_back(descriptorSetLayout);
    _descriptorSetLayout[MaterialType::PHONG].push_back(_gameState->getLightManager()->getDSLGlobalTerrainPhong());

    _descriptorSetPhong.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetPhong[i] = nullptr;
    }
    // update descriptor set in setMaterial

    // initialize Phong
    {
      std::unordered_map<std::string, VkPushConstantRange> pushConstants;
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

      std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts;
      for (const auto& layout : _descriptorSetLayout[MaterialType::PHONG]) descriptorSetLayouts.push_back(layout.get());
      RenderGraph::PipelineGraphic pipelineGraphic;
      pipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      pipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      pipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
      pipelineGraphic.setDepthTest(true);
      pipelineGraphic.setDepthWrite(true);
      pipelineGraphic.setTesselation(4);
      pipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipeline[MaterialType::PHONG] = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipeline[MaterialType::PHONG]->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts,
                                                    pushConstants, vertexInput);
    }
  }
  // layout for PBR
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
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetLayout[MaterialType::PBR].push_back(descriptorSetLayout);
    _descriptorSetLayout[MaterialType::PBR].push_back(_gameState->getLightManager()->getDSLGlobalTerrainPBR());

    _descriptorSetPBR.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetPBR[i] = nullptr;
    }
    // update descriptor set in setMaterial

    // initialize PBR
    {
      std::unordered_map<std::string, VkPushConstantRange> pushConstants;
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

      std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts;
      for (const auto& layout : _descriptorSetLayout[MaterialType::PBR]) descriptorSetLayouts.push_back(layout.get());
      RenderGraph::PipelineGraphic pipelineGraphic;
      pipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      pipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      pipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
      pipelineGraphic.setDepthTest(true);
      pipelineGraphic.setDepthWrite(true);
      pipelineGraphic.setTesselation(4);
      pipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
      _pipeline[MaterialType::PBR] = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipeline[MaterialType::PBR]->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts,
                                                  pushConstants, vertexInput);
    }
  }
}

void TerrainComposition::_fillPatchTexturesDescription() {
  _patchTextures = std::vector<int>(_patchNumber.first * _patchNumber.second, 0);
  auto [width, height] = std::tuple{(_heightMap->getImageView().getImage()).getResolution().x,
                                    (_heightMap->getImageView().getImage()).getResolution().y};
  //
  for (int y = 0; y < _patchNumber.second; y++)
    for (int x = 0; x < _patchNumber.first; x++) {
      const int patchX0 = x * width / _patchNumber.first;
      const int patchY0 = y * height / _patchNumber.second;
      const int patchX1 = (x + 1) * width / _patchNumber.first;
      const int patchY1 = (y + 1) * height / _patchNumber.second;

      int heightLevel = 0;
      for (int i = patchY0; i < patchY1; i++) {
        for (int j = patchX0; j < patchX1; j++) {
          int textureCoord = (j + i * width) * _heightMapCPU->getChannels();
          heightLevel = std::max(heightLevel, static_cast<int>(_heightMapCPU->getData()[textureCoord]));
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
  _patchDescriptionSSBO[currentFrame] = std::make_shared<RenderGraph::Buffer>(
      _patchNumber.first * _patchNumber.second * sizeof(PatchDescription), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      *_engineState->getMemoryAllocator());
}

void TerrainComposition::_updatePatchDescription(int currentFrame) {
  std::vector<PatchDescription> patchData(_patchNumber.first * _patchNumber.second);
  for (int i = 0; i < patchData.size(); i++) {
    patchData[i] = PatchDescription({.rotation = 90 * _patchRotationsIndex[i], .textureID = _patchTextures[i]});
  }
  // fill only number of lights because it's stub
  std::memcpy(static_cast<std::byte*>(_patchDescriptionSSBO[currentFrame]->getAllocationInfo().pMappedData),
              patchData.data(), static_cast<std::size_t>(patchData.size() * sizeof(PatchDescription)));
}

void TerrainComposition::_updateColorDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  const int currentFrame = _engineState->getFrameInFlight();
  const auto material = std::dynamic_pointer_cast<MaterialColor>(_material);
  material->unregisterUpdate(_descriptorSetColor[currentFrame]);
  auto descriptor = std::make_shared<RenderGraph::DescriptorSet>(
      std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout[MaterialType::COLOR][0]).get()},
      *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
  descriptor->add({_cameraBuffer[currentFrame].get()});
  descriptor->add({_patchDescriptionSSBO[currentFrame].get()});
  descriptor->add({_cameraBuffer[currentFrame].get()});
  descriptor->add({_heightMap.get()});
  std::vector<RenderGraph::Texture*> baseColors;
  for (const auto& texture : material->getBaseColor()) baseColors.push_back(texture.get());
  descriptor->add(std::move(baseColors));
  descriptor->initialize(commandBuffer);
  material->registerUpdate(descriptor, {.frameInFlight = currentFrame, .materialInfo = {{MaterialTexture::COLOR, 4}}});
  _descriptorSetColor[currentFrame] = std::move(descriptor);
}

void TerrainComposition::_updatePhongDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  const int currentFrame = _engineState->getFrameInFlight();
  const auto material = std::dynamic_pointer_cast<MaterialPhong>(_material);
  material->unregisterUpdate(_descriptorSetPhong[currentFrame]);
  auto descriptor = std::make_shared<RenderGraph::DescriptorSet>(
      std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout[MaterialType::PHONG][0]).get()},
      *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
  descriptor->add({_cameraBuffer[currentFrame].get()});
  descriptor->add({_patchDescriptionSSBO[currentFrame].get()});
  descriptor->add({_cameraBuffer[currentFrame].get()});
  descriptor->add({_heightMap.get()});
  std::vector<RenderGraph::Texture*> baseColors;
  for (const auto& texture : material->getBaseColor()) baseColors.push_back(texture.get());
  descriptor->add(std::move(baseColors));
  std::vector<RenderGraph::Texture*> normals;
  for (const auto& texture : material->getNormal()) normals.push_back(texture.get());
  descriptor->add(std::move(normals));
  std::vector<RenderGraph::Texture*> speculars;
  for (const auto& texture : material->getSpecular()) speculars.push_back(texture.get());
  descriptor->add(std::move(speculars));
  descriptor->add({material->getBufferCoefficients()[currentFrame].get()});
  descriptor->initialize(commandBuffer);
  material->registerUpdate(
      descriptor,
      {.frameInFlight = currentFrame,
       .materialInfo = {{MaterialTexture::COLOR, 4}, {MaterialTexture::NORMAL, 5}, {MaterialTexture::SPECULAR, 6}}});
  _descriptorSetPhong[currentFrame] = std::move(descriptor);
}

void TerrainComposition::_updatePBRDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  const int currentFrame = _engineState->getFrameInFlight();
  const auto material = std::dynamic_pointer_cast<MaterialPBR>(_material);
  material->unregisterUpdate(_descriptorSetPBR[currentFrame]);
  auto descriptor = std::make_shared<RenderGraph::DescriptorSet>(
      std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout[MaterialType::PBR][0]).get()},
      *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
  descriptor->add({_cameraBuffer[currentFrame].get()});
  descriptor->add({_patchDescriptionSSBO[currentFrame].get()});
  descriptor->add({_cameraBuffer[currentFrame].get()});
  descriptor->add({_heightMap.get()});
  const auto addTextures = [&](const auto& textures) {
    std::vector<RenderGraph::Texture*> rawTextures;
    for (const auto& texture : textures) rawTextures.push_back(texture.get());
    descriptor->add(std::move(rawTextures));
  };
  addTextures(material->getBaseColor());
  addTextures(material->getNormal());
  addTextures(material->getMetallic());
  addTextures(material->getRoughness());
  addTextures(material->getOccluded());
  addTextures(material->getEmissive());
  descriptor->add({material->getDiffuseIBL().get()});
  descriptor->add({material->getSpecularIBL().get()});
  descriptor->add({material->getSpecularBRDF().get()});
  descriptor->add({material->getBufferCoefficients()[currentFrame].get()});
  descriptor->add({material->getBufferAlphaCutoff()[currentFrame].get()});
  descriptor->initialize(commandBuffer);
  material->registerUpdate(descriptor, {.frameInFlight = currentFrame,
                                        .materialInfo = {{MaterialTexture::COLOR, 4},
                                                         {MaterialTexture::NORMAL, 5},
                                                         {MaterialTexture::METALLIC, 6},
                                                         {MaterialTexture::ROUGHNESS, 7},
                                                         {MaterialTexture::OCCLUSION, 8},
                                                         {MaterialTexture::EMISSIVE, 9},
                                                         {MaterialTexture::IBL_DIFFUSE, 10},
                                                         {MaterialTexture::IBL_SPECULAR, 11},
                                                         {MaterialTexture::BRDF_SPECULAR, 12}}});
  _descriptorSetPBR[currentFrame] = std::move(descriptor);
}

void TerrainComposition::draw(const RenderGraph::CommandBuffer* commandBuffer) {
  const int currentFrame = _engineState->getFrameInFlight();
  auto drawTerrain = [&](std::shared_ptr<RenderGraph::Pipeline> pipeline) {
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

      auto info = pipeline->getPushConstants().at("control");
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    if (pipeline->getPushConstants().find("evaluate") != pipeline->getPushConstants().end()) {
      TesselationEvaluationPush pushConstants{.patchDimX = _patchNumber.first,
                                              .patchDimY = _patchNumber.second,
                                              .heightScale = _heightScale,
                                              .heightShift = _heightShift};

      auto info = pipeline->getPushConstants().at("evaluate");
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    if (pipeline->getPushConstants().find("fragment") != pipeline->getPushConstants().end()) {
      FragmentPush pushConstants{.enableShadow = _enableShadow,
                                 .enableLighting = _enableLighting,
                                 .cameraPosition = _gameState->getCameraManager()->getCurrentCamera()->getEye()};

      auto info = pipeline->getPushConstants().at("fragment");
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    // same buffer to both tessellation shaders because we're not going to change camera between these 2 stages
    BufferMVP cameraUBO{.model = getModel(),
                        .view = _gameState->getCameraManager()->getCurrentCamera()->getView(),
                        .projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection()};

    std::memcpy(static_cast<std::byte*>(_cameraBuffer[currentFrame]->getAllocationInfo().pMappedData), &cameraUBO,
                static_cast<std::size_t>(_cameraBuffer[currentFrame]->getSize()));

    VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

    auto descriptor = _descriptorSetColor[currentFrame];
    if (_materialType == MaterialType::PHONG) descriptor = _descriptorSetPhong[currentFrame];
    if (_materialType == MaterialType::PBR) descriptor = _descriptorSetPBR[currentFrame];
    descriptor->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(), *commandBuffer);
    if (_materialType == MaterialType::PBR) {
      _gameState->getLightManager()->getDSGlobalTerrainPBR()->bind(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                   pipeline->getPipelineLayout(), *commandBuffer);
    } else {
      _gameState->getLightManager()->getDSGlobalTerrainPhong()->bind(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                     pipeline->getPipelineLayout(), *commandBuffer);
    }

    vkCmdDraw(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getVertexData().size()), 1, 0, 0);
  };

  if (_changedMaterial[currentFrame]) {
    switch (_materialType) {
      case MaterialType::COLOR:
        _updateColorDescriptor(*commandBuffer);
        break;
      case MaterialType::PHONG:
        _updatePhongDescriptor(*commandBuffer);
        break;
      case MaterialType::PBR:
        _updatePBRDescriptor(*commandBuffer);
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
                                    const RenderGraph::CommandBuffer* commandBuffer) {
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
  float far = 0.0f;
  int lightIndexTotal = lightIndex;
  if (lightType == LightType::DIRECTIONAL) {
    view = _gameState->getLightManager()->getDirectionalLights()[lightIndex]->getCamera()->getView();
    projection = _gameState->getLightManager()->getDirectionalLights()[lightIndex]->getCamera()->getProjection();
    far = _gameState->getLightManager()->getDirectionalLights()[lightIndex]->getCamera()->getFar();
  } else if (lightType == LightType::POINT) {
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

    auto info = pipeline->getPushConstants().at("controlDepth");
    vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                       info.size, &pushConstants);
  }

  if (pipeline->getPushConstants().find("evaluateDepth") != pipeline->getPushConstants().end()) {
    TesselationEvaluationPushDepth pushConstants{.heightScale = _heightScale, .heightShift = _heightShift};

    auto info = pipeline->getPushConstants().at("evaluateDepth");
    vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                       info.size, &pushConstants);
  }

  if (pipeline->getPushConstants().find("fragmentDepth") != pipeline->getPushConstants().end()) {
    FragmentPointLightPushDepth pushConstants{
        .lightPosition = _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getPosition(),
        .far = static_cast<int>(far)};

    auto info = pipeline->getPushConstants().at("fragmentDepth");
    vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                       info.size, &pushConstants);
  }

  // same buffer to both tessellation shaders because we're not going to change camera between these 2 stages
  BufferMVP cameraUBO{.model = getModel(), .view = view, .projection = projection};

  std::memcpy(
      static_cast<std::byte*>(_cameraBufferDepth[currentFrame][lightIndexTotal][face]->getAllocationInfo().pMappedData),
      &cameraUBO, static_cast<std::size_t>(_cameraBufferDepth[currentFrame][lightIndexTotal][face]->getSize()));

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  _descriptorSetCameraDepth[currentFrame][lightIndexTotal][face]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                       pipeline->getPipelineLayout(), *commandBuffer);

  vkCmdDraw(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getVertexData().size()), 1, 0, 0);
}
