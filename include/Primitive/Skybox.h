#pragma once
#include "Utility/EngineState.h"
#include "Utility/GameState.h"
#include "Utility/ResourceManager.h"
#include "Vulkan/Pipeline.h"
#include "Vulkan/Render.h"
#include "Graphic/Camera.h"
#include "Graphic/Material.h"
#include "Primitive/Cubemap.h"
#include "Primitive/Mesh.h"
#include "Primitive/Drawable.h"

class Skybox {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<GameState> _gameState;

  std::shared_ptr<MeshStatic3D> _mesh;
  std::vector<std::shared_ptr<Buffer>> _uniformBuffer;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayout;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSet;
  std::shared_ptr<PipelineGraphic> _pipeline;
  std::shared_ptr<Material> _material;
  std::shared_ptr<MaterialColor> _defaultMaterialColor;
  MaterialType _materialType = MaterialType::COLOR;
  glm::mat4 _model = glm::mat4(1.f);
  std::vector<bool> _changedMaterial;

  void _updateColorDescriptor();

 public:
  Skybox(std::shared_ptr<CommandBuffer> commandBufferTransfer,
         std::shared_ptr<GameState> gameState,
         std::shared_ptr<EngineState> engineState);
  void setMaterial(std::shared_ptr<MaterialColor> material);
  void setModel(glm::mat4 model);
  std::shared_ptr<MeshStatic3D> getMesh();

  void draw(std::shared_ptr<CommandBuffer> commandBuffer);
};