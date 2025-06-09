#pragma once
#include "Vulkan/Device.h"
#include "Vulkan/Buffer.h"
#include "Vulkan/Shader.h"
#include "Vulkan/Pipeline.h"
#include "Vulkan/Command.h"
#include "Utility/Settings.h"
#include "Utility/GameState.h"
#include "Utility/ResourceManager.h"
#include "Utility/Logger.h"
#include "Utility/Loader.h"
#include "Utility/Animation.h"
#include "Graphic/Camera.h"
#include "Graphic/LightManager.h"
#include "Graphic/Material.h"
#include "Primitive/Drawable.h"
#include "Utility/PhysicsManager.h"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/Character.h>

enum class GroundState {
  OnGround,       ///< Character is on the ground and can move freely.
  OnSteepGround,  ///< Character is on a slope that is too steep and can't climb up any further. The caller should start
                  ///< applying downward velocity if sliding from the slope is desired.
  NotSupported,   ///< Character is touching an object, but is not supported by it and should fall. The GetGroundXXX
                  ///< functions will return information about the touched object.
  InAir,          ///< Character is in the air and is not touching anything
};

class Model3DPhysics {
 private:
  std::shared_ptr<PhysicsManager> _physicsManager;
  // destructor is private, can't use smart pointer
  JPH::Ref<JPH::Character> _character;
  float _collisionTolerance = 0.2f;
  float _rotationSpeed = 3.f;
  float _movementSpeed;

 public:
  Model3DPhysics(glm::vec3 translate, glm::vec3 size, std::shared_ptr<PhysicsManager> physicsManager);
  Model3DPhysics(glm::vec3 translate, float height, float radius, std::shared_ptr<PhysicsManager> physicsManager);
  void postUpdate();
  bool move(glm::vec3 direction);
  bool moveTo(glm::vec3 endPoint, float error);
  void setRotationSpeed(float rotationSpeed);
  void setMovementSpeed(float movementSpeed);
  void setTranslate(glm::vec3 translate);
  void setRotate(glm::quat rotate);
  void setShape(float height, float radius);
  glm::quat getRotate();
  glm::vec3 getTranslate();
  void setLinearVelocity(glm::vec3 velocity);
  glm::vec3 getUp();
  void setUp(glm::vec3 up);
  glm::vec3 getLinearVelocity();
  void setFriction(float friction);
  GroundState getGroundState();
  glm::vec3 getGroundNormal();
  glm::vec3 getGroundVelocity();
  glm::vec3 getSize();
  ~Model3DPhysics();
};

class Model3D : public Drawable, public Shadowable {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<GameState> _gameState;
  std::vector<bool> _changedMaterial;
  std::vector<std::shared_ptr<NodeGLTF>> _nodes;
  std::vector<std::vector<std::vector<std::shared_ptr<Buffer>>>> _cameraUBODepth;
  std::vector<std::shared_ptr<Buffer>> _cameraUBOFull;
  std::vector<std::vector<std::vector<std::shared_ptr<DescriptorSet>>>> _descriptorSetCameraDepth;
  std::vector<std::vector<std::shared_ptr<DescriptorSet>>> _descriptorSetColor, _descriptorSetPhong, _descriptorSetPBR,
      _descriptorSetJoints;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSetNormalsMesh;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayoutJoints;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayoutNormalsMesh;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayoutColor, _descriptorSetLayoutPhong, _descriptorSetLayoutPBR;
  std::map<MaterialType, std::shared_ptr<PipelineGraphic>> _pipeline, _pipelineCullOff, _pipelineWireframe;
  std::shared_ptr<PipelineGraphic> _pipelineNormalMesh, _pipelineNormalMeshCullOff, _pipelineTangentMesh,
      _pipelineTangentMeshCullOff, _pipelineDirectional, _pipelinePoint;

  std::shared_ptr<MaterialPhong> _defaultMaterialPhong;
  std::shared_ptr<MaterialPBR> _defaultMaterialPBR;
  std::shared_ptr<MaterialColor> _defaultMaterialColor;
  std::shared_ptr<MeshStatic3D> _mesh;
  std::shared_ptr<Animation> _defaultAnimation;
  bool _enableDepth = true;
  bool _enableShadow = true;
  bool _enableLighting = true;
  std::shared_ptr<Animation> _animation;
  std::vector<std::shared_ptr<Material>> _materials;
  std::vector<std::shared_ptr<MeshStatic3D>> _meshes;
  MaterialType _materialType = MaterialType::PHONG;
  DrawType _drawType = DrawType::FILL;

  void _updateJointsDescriptor();
  void _updateColorDescriptor();
  void _updatePhongDescriptor();
  void _updatePBRDescriptor();

  void _drawNode(std::shared_ptr<CommandBuffer> commandBuffer,
                 std::shared_ptr<Pipeline> pipeline,
                 std::shared_ptr<Pipeline> pipelineCullOff,
                 std::shared_ptr<DescriptorSet> cameraDS,
                 std::shared_ptr<Buffer> cameraUBO,
                 glm::mat4 view,
                 glm::mat4 projection,
                 std::shared_ptr<NodeGLTF> node);

 public:
  Model3D(const std::vector<std::shared_ptr<NodeGLTF>>& nodes,
          const std::vector<std::shared_ptr<MeshStatic3D>>& meshes,
          std::shared_ptr<CommandBuffer> commandBufferTransfer,
          std::shared_ptr<GameState> gameState,
          std::shared_ptr<EngineState> engineState);
  void enableShadow(bool enable);
  void enableLighting(bool enable);

  void setMaterial(std::vector<std::shared_ptr<MaterialPBR>> materials);
  void setMaterial(std::vector<std::shared_ptr<MaterialPhong>> materials);
  void setMaterial(std::vector<std::shared_ptr<MaterialColor>> materials);
  void setAnimation(std::shared_ptr<Animation> animation);
  void setDrawType(DrawType drawType);

  void enableDepth(bool enable);
  bool isDepthEnabled();

  MaterialType getMaterialType();
  DrawType getDrawType();
  std::shared_ptr<AABB> getAABBPositions();
  std::shared_ptr<AABB> getAABBJoints();

  void draw(std::shared_ptr<CommandBuffer> commandBuffer) override;
  void drawShadow(LightType lightType, int lightIndex, int face, std::shared_ptr<CommandBuffer> commandBuffer) override;
};