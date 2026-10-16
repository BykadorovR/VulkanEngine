#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include "Utility/EngineState.h"
#include "Utility/GameState.h"
#include "Utility/ResourceManager.h"
#include "Primitive/Drawable.h"
#include "Primitive/Mesh.h"
#include "Graphic/Camera.h"
#include "Graphic/Material.h"
#include "Utility/PhysicsManager.h"

import Buffer;
import Command;
import DescriptorBuffer;
import Pipeline;

enum class ShapeType { CUBE = 0, SPHERE = 1, CAPSULE = 2 };

class Shape3DPhysics {
 private:
  std::shared_ptr<PhysicsManager> _physicsManager;
  // destructor is private, can't use smart pointer
  JPH::Body* _shapeBody;

 public:
  Shape3DPhysics(glm::vec3 translate, glm::vec3 size, std::shared_ptr<PhysicsManager> physicsManager);
  void setTranslate(glm::vec3 translate);
  glm::vec3 getTranslate();
  glm::quat getRotate();
  void setLinearVelocity(glm::vec3 velocity);
  ~Shape3DPhysics();
};

class Shape3D : public Drawable, public Shadowable {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<GameState> _gameState;
  std::vector<bool> _changedMaterial;
  std::map<ShapeType, std::map<MaterialType, std::vector<std::string>>> _shadersColor;
  std::map<ShapeType, std::vector<std::string>> _shadersLightDirectional, _shadersLightPoint, _shadersNormalsMesh,
      _shadersTangentMesh;
  ShapeType _shapeType;
  std::shared_ptr<Mesh3D> _mesh;
  std::map<MaterialType, std::vector<std::shared_ptr<RenderGraph::DescriptorSetLayout>>> _descriptorSetLayout;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _descriptorSetLayoutNormalsMesh;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _descriptorSetLayoutDepth;
  std::vector<std::vector<std::vector<std::shared_ptr<RenderGraph::DescriptorSet>>>> _descriptorSetCameraDepth;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSet>> _descriptorSetNormalsMesh, _descriptorSetColor,
      _descriptorSetPhong, _descriptorSetPBR;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _uniformBufferCamera;

  std::vector<std::vector<std::vector<std::shared_ptr<RenderGraph::Buffer>>>> _cameraUBODepth;
  std::map<ShapeType, std::map<MaterialType, std::shared_ptr<RenderGraph::Pipeline>>> _pipeline, _pipelineWireframe;
  std::shared_ptr<RenderGraph::Pipeline> _pipelineDirectional, _pipelinePoint, _pipelineNormalMesh,
      _pipelineTangentMesh;
  std::shared_ptr<Material> _material;
  std::shared_ptr<MaterialColor> _defaultMaterialColor;
  MaterialType _materialType = MaterialType::COLOR;
  DrawType _drawType = DrawType::FILL;
  bool _enableShadow = true;
  bool _enableLighting = true;

  void _updateColorDescriptor(const RenderGraph::CommandBuffer& commandBuffer);
  void _updatePhongDescriptor(const RenderGraph::CommandBuffer& commandBuffer);
  void _updatePBRDescriptor(const RenderGraph::CommandBuffer& commandBuffer);
  void _unregisterMaterialUpdates();

 public:
  Shape3D(ShapeType shapeType,
          std::shared_ptr<Mesh3D> mesh,
          VkCullModeFlags cullMode,
          const RenderGraph::CommandBuffer* commandBufferTransfer,
          std::shared_ptr<GameState> gameState,
          std::shared_ptr<EngineState> engineState);

  void enableShadow(bool enable);
  void enableLighting(bool enable);
  void setMaterial(std::shared_ptr<MaterialColor> material);
  void setMaterial(std::shared_ptr<MaterialPhong> material);
  void setMaterial(std::shared_ptr<MaterialPBR> material);
  void setDrawType(DrawType drawType);

  std::shared_ptr<Mesh3D> getMesh();

  void draw(const RenderGraph::CommandBuffer* commandBuffer) override;
  void drawShadow(LightType lightType,
                  int lightIndex,
                  int face,
                  const RenderGraph::CommandBuffer* commandBuffer) override;
};
