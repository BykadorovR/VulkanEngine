#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include "Utility/EngineState.h"
#include "Utility/GameState.h"
#include "Graphic/Camera.h"
#include "Graphic/Material.h"
#include "Graphic/LightManager.h"
#include "Primitive/Drawable.h"
#include "Primitive/Line.h"
#include "Primitive/Mesh.h"
#include "Utility/PhysicsManager.h"
#include "Utility/GUI.h"
#include "Primitive/Shape3D.h"
#include "Primitive/Terrain.h"

import Buffer;
import Command;
import DescriptorBuffer;
import Pipeline;

class TerrainCompositionDebug : public TerrainDebug {
 private:
  std::shared_ptr<MaterialColor> _defaultMaterialColor;

  std::shared_ptr<GUI> _gui;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _cameraBuffer;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSetLayout>> _descriptorSetLayout;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSetLayout>> _descriptorSetLayoutNormalsMesh;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSet>> _descriptorSetNormal;
  std::shared_ptr<RenderGraph::Pipeline> _pipeline, _pipelineWireframe;
  std::shared_ptr<RenderGraph::Pipeline> _pipelineNormalMesh, _pipelineTangentMesh;
  int _pickedTile = -1;
  glm::ivec2 _pickedPixel = glm::ivec2(-1, -1);
  bool _showWireframe = false, _showNormals = false, _showPatches = false;
  glm::vec2 _cursorPosition = glm::vec2(-1.f);
  glm::vec2 _clickPosition = glm::vec2(-1.f);
  int _angleIndex = -1, _textureIndex = -1;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _patchDescriptionSSBO;
  char _terrainPath[256] = "";
  void _updateColorDescriptor(const RenderGraph::CommandBuffer& commandBuffer);
  void _reallocatePatchDescription(int currentFrame);
  void _updatePatchDescription(int currentFrame);
  int _saveAuxilary(std::string path);
  void _loadAuxilary(std::string path);

 public:
  TerrainCompositionDebug(std::shared_ptr<ImageCPU<uint8_t>> heightMapCPU,
                          std::pair<int, int> patchNumber,
                          const RenderGraph::CommandBuffer* commandBufferTransfer,
                          std::shared_ptr<GUI> gui,
                          std::shared_ptr<GameState> gameState,
                          std::shared_ptr<EngineState> engineState);

  void draw(const RenderGraph::CommandBuffer* commandBuffer) override;
  void drawDebug(const RenderGraph::CommandBuffer* commandBuffer) override;

  void cursorNotify(float xPos, float yPos) override;
  void mouseNotify(int button, int action, int mods) override;
  void keyNotify(int key, int scancode, int action, int mods) override;
  void charNotify(unsigned int code) override;
  void scrollNotify(double xOffset, double yOffset) override;
  virtual ~TerrainCompositionDebug() = default;
};

class TerrainComposition : public TerrainGPU {
 private:
  std::shared_ptr<GameState> _gameState;
  std::shared_ptr<MaterialColor> _defaultMaterialColor;

  std::vector<std::shared_ptr<RenderGraph::Buffer>> _cameraBuffer;
  std::vector<std::vector<std::vector<std::shared_ptr<RenderGraph::Buffer>>>> _cameraBufferDepth;
  std::map<MaterialType, std::vector<std::shared_ptr<RenderGraph::DescriptorSetLayout>>> _descriptorSetLayout;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSetLayout>> _descriptorSetLayoutShadows;
  std::vector<std::vector<std::vector<std::shared_ptr<RenderGraph::DescriptorSet>>>> _descriptorSetCameraDepth;
  std::map<MaterialType, std::shared_ptr<RenderGraph::Pipeline>> _pipeline;
  std::shared_ptr<RenderGraph::Pipeline> _pipelineDirectional, _pipelinePoint;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _patchDescriptionSSBO;

  void _fillPatchTexturesDescription();
  void _fillPatchRotationsDescription();
  void _allocatePatchDescription(int currentFrame);
  void _updatePatchDescription(int currentFrame);
  void _updateColorDescriptor(const RenderGraph::CommandBuffer& commandBuffer);
  void _updatePhongDescriptor(const RenderGraph::CommandBuffer& commandBuffer);
  void _updatePBRDescriptor(const RenderGraph::CommandBuffer& commandBuffer);

 public:
  TerrainComposition(std::shared_ptr<ImageCPU<uint8_t>> heightMapCPU,
                     std::shared_ptr<GameState> gameState,
                     std::shared_ptr<EngineState> engineState);
  void initialize(const RenderGraph::CommandBuffer* commandBuffer) override;
  void draw(const RenderGraph::CommandBuffer* commandBuffer) override;
  void drawShadow(LightType lightType,
                  int lightIndex,
                  int face,
                  const RenderGraph::CommandBuffer* commandBuffer) override;
  virtual ~TerrainComposition() = default;
};
