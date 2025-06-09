#pragma once
#include "Utility/EngineState.h"
#include "Utility/GameState.h"
#include "Utility/ResourceManager.h"
#include "Vulkan/Descriptor.h"
#include "Vulkan/Pipeline.h"
#include "Primitive/Drawable.h"
#include "Primitive/Mesh.h"
#include "Graphic/Camera.h"
#include "Graphic/Material.h"
#include "Utility/PhysicsManager.h"
#include <Jolt/Physics/Body/BodyCreationSettings.h>

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
  std::map<MaterialType, std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>>>
      _descriptorSetLayout;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayoutNormalsMesh;
  std::vector<std::vector<std::vector<std::shared_ptr<DescriptorSet>>>> _descriptorSetCameraDepth;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSetNormalsMesh, _descriptorSetColor, _descriptorSetPhong,
      _descriptorSetPBR;
  std::vector<std::shared_ptr<Buffer>> _uniformBufferCamera;

  std::vector<std::vector<std::vector<std::shared_ptr<Buffer>>>> _cameraUBODepth;
  std::map<ShapeType, std::map<MaterialType, std::shared_ptr<PipelineGraphic>>> _pipeline, _pipelineWireframe;
  std::shared_ptr<PipelineGraphic> _pipelineDirectional, _pipelinePoint, _pipelineNormalMesh, _pipelineTangentMesh;
  std::shared_ptr<Material> _material;
  std::shared_ptr<MaterialColor> _defaultMaterialColor;
  std::shared_ptr<MaterialPhong> _defaultMaterialPhong;
  std::shared_ptr<MaterialPBR> _defaultMaterialPBR;
  MaterialType _materialType = MaterialType::COLOR;
  DrawType _drawType = DrawType::FILL;
  VkCullModeFlags _cullMode;
  bool _enableShadow = true;
  bool _enableLighting = true;

  void _updateColorDescriptor();
  void _updatePhongDescriptor();
  void _updatePBRDescriptor();

 public:
  Shape3D(ShapeType shapeType,
          std::shared_ptr<Mesh3D> mesh,
          VkCullModeFlags cullMode,
          std::shared_ptr<CommandBuffer> commandBufferTransfer,
          std::shared_ptr<GameState> gameState,
          std::shared_ptr<EngineState> engineState);

  void enableShadow(bool enable);
  void enableLighting(bool enable);
  void setMaterial(std::shared_ptr<MaterialColor> material);
  void setMaterial(std::shared_ptr<MaterialPhong> material);
  void setMaterial(std::shared_ptr<MaterialPBR> material);
  void setDrawType(DrawType drawType);

  std::shared_ptr<Mesh3D> getMesh();

  void draw(std::shared_ptr<CommandBuffer> commandBuffer) override;
  void drawShadow(LightType lightType, int lightIndex, int face, std::shared_ptr<CommandBuffer> commandBuffer) override;
};