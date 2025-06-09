#include "Primitive/Model.h"
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <unordered_map>
#include <glm/gtx/norm.hpp>

#undef far

struct FragmentPointLightPushDepth {
  alignas(16) glm::vec3 lightPosition;
  alignas(16) int far;
};

struct FragmentPush {
  int enableShadow;
  int enableLighting;
  alignas(16) glm::vec3 cameraPosition;
};

Model3DPhysics::Model3DPhysics(glm::vec3 translate, glm::vec3 size, std::shared_ptr<PhysicsManager> physicsManager) {
  _physicsManager = physicsManager;
  JPH::CharacterSettings settings;
  settings.mShape = new JPH::BoxShape(JPH::Vec3(size.x / 2.f, size.y / 2.f, size.z / 2.f));
  settings.mLayer = Layers::MOVING;
  // settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -size.y / 2.f);
  _character = new JPH::Character(&settings, JPH::Vec3(translate.x, translate.y, translate.z), JPH::Quat::sIdentity(),
                                  0, &_physicsManager->getPhysicsSystem());
  _character->AddToPhysicsSystem(JPH::EActivation::Activate);
}

Model3DPhysics::Model3DPhysics(glm::vec3 translate,
                               float height,
                               float radius,
                               std::shared_ptr<PhysicsManager> physicsManager) {
  _physicsManager = physicsManager;
  JPH::CharacterSettings settings;
  settings.mShape = new JPH::CapsuleShape(height / 2.f, radius);
  settings.mLayer = Layers::MOVING;
  // settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
  _character = new JPH::Character(&settings, JPH::Vec3(translate.x, translate.y, translate.z), JPH::Quat::sIdentity(),
                                  0, &_physicsManager->getPhysicsSystem());
  _character->AddToPhysicsSystem(JPH::EActivation::Activate);
}

void Model3DPhysics::setMovementSpeed(float movementSpeed) { _movementSpeed = movementSpeed; }

bool Model3DPhysics::move(glm::vec3 direction) {
  // Cancel movement in opposite direction of normal when touching something we can't walk up
  if (getGroundState() == GroundState::OnSteepGround || getGroundState() == GroundState::NotSupported) {
    auto normal = getGroundNormal();
    normal.y = 0.0f;
    float dot = glm::dot(normal, direction);
    if (dot < 0.0f) {
      auto change = (dot * normal) / glm::length2(normal);
      direction -= change;
    }
  }
  // Update velocity
  // glm::vec3 currentVelocity = getLinearVelocity();
  glm::vec3 desiredVelocity = _movementSpeed * direction;
  /*if (!(glm::length2(desiredVelocity) <= 1.0e-12f) || currentVelocity.y < 0.0f) desiredVelocity.y = currentVelocity.y;
  glm::vec3 newVelocity = 0.75f * currentVelocity + 0.25f * desiredVelocity;*/
  auto newVelocity = desiredVelocity;
  setLinearVelocity(newVelocity);

  // rotate
  auto flatDirection = glm::vec3(direction.x, 0.f, direction.z);
  if (glm::length(flatDirection) > 0.1f) {
    flatDirection = glm::normalize(flatDirection);
    auto currentRotation = getRotate();
    auto angle = glm::atan(flatDirection.x, flatDirection.z);
    glm::quat rotation = glm::rotate(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::quat smoothedRotation = glm::slerp(currentRotation, rotation,
                                            _physicsManager->getDeltaTime() * _rotationSpeed);
    setRotate(glm::normalize(smoothedRotation));
  }
  return true;
}

bool Model3DPhysics::moveTo(glm::vec3 endPoint, float error) {
  auto position = getTranslate();
  position.y -= getSize().y / 2.f;
  auto distance = glm::distance(endPoint, position);
  if (distance < error) {
    setLinearVelocity({0.f, 0.f, 0.f});
    return false;
  }
  glm::vec3 direction = glm::normalize(endPoint - glm::vec3(position.x, position.y, position.z));
  return move(direction);
}

void Model3DPhysics::setRotationSpeed(float rotationSpeed) { _rotationSpeed = rotationSpeed; }

void Model3DPhysics::setTranslate(glm::vec3 translate) {
  _physicsManager->getBodyInterface().SetPosition(
      _character->GetBodyID(), JPH::RVec3(translate.x, translate.y, translate.z), JPH::EActivation::Activate);
}

void Model3DPhysics::setShape(float height, float radius) {
  // need to recalculate shift by Y axis because of possible shape height change
  auto position = getTranslate();
  auto aabb = getSize();
  // divide by two because aabb is symmetric along the center
  auto heightDiff = ((height + radius * 2.f) - aabb.y) / 2.f;
  setTranslate({position.x, position.y + heightDiff, position.z});
  _physicsManager->getBodyInterface().SetShape(_character->GetBodyID(), new JPH::CapsuleShape(height / 2.f, radius),
                                               false, JPH::EActivation::Activate);
}

void Model3DPhysics::setRotate(glm::quat rotate) {
  _physicsManager->getBodyInterface().SetRotation(
      _character->GetBodyID(), JPH::Quat(rotate.x, rotate.y, rotate.z, rotate.w), JPH::EActivation::Activate);
}

void Model3DPhysics::setLinearVelocity(glm::vec3 velocity) {
  _physicsManager->getBodyInterface().SetLinearVelocity(_character->GetBodyID(), {velocity.x, velocity.y, velocity.z});
}

void Model3DPhysics::setFriction(float friction) {
  _physicsManager->getBodyInterface().SetFriction(_character->GetBodyID(), friction);
}

void Model3DPhysics::setUp(glm::vec3 up) { _character->SetUp({up.x, up.y, up.z}); }

glm::quat Model3DPhysics::getRotate() {
  auto rotation = _physicsManager->getBodyInterface().GetRotation(_character->GetBodyID());
  return glm::quat{rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ()};
}

glm::vec3 Model3DPhysics::getTranslate() {
  auto position = _physicsManager->getBodyInterface().GetPosition(_character->GetBodyID());
  return glm::vec3(position.GetX(), position.GetY(), position.GetZ());
}

glm::vec3 Model3DPhysics::getSize() {
  auto aabb = _character->GetShape()->GetLocalBounds();
  auto size = aabb.mMax - aabb.mMin;
  return {size.GetX(), size.GetY(), size.GetZ()};
}

void Model3DPhysics::postUpdate() { _character->PostSimulation(_collisionTolerance); }

GroundState Model3DPhysics::getGroundState() { return (GroundState)_character->GetGroundState(); }

glm::vec3 Model3DPhysics::getGroundNormal() {
  JPH::Vec3 normal = _character->GetGroundNormal();
  return glm::vec3{normal.GetX(), normal.GetY(), normal.GetZ()};
}

glm::vec3 Model3DPhysics::getGroundVelocity() {
  auto velocity = _character->GetGroundVelocity();
  return {velocity.GetX(), velocity.GetY(), velocity.GetZ()};
}

glm::vec3 Model3DPhysics::getUp() {
  auto up = _character->GetUp();
  return {up.GetX(), up.GetY(), up.GetZ()};
}

glm::vec3 Model3DPhysics::getLinearVelocity() {
  auto velocity = _physicsManager->getBodyInterface().GetLinearVelocity(_character->GetBodyID());
  return {velocity.GetX(), velocity.GetY(), velocity.GetZ()};
}

Model3DPhysics::~Model3DPhysics() {
  _character->RemoveFromPhysicsSystem();
  // destroy body isn't needed because it's called automatically in Character destructor, removeFromPhysicsSystem does
  // removeBody inside
}

void Model3D::enableDepth(bool enable) { _enableDepth = enable; }

bool Model3D::isDepthEnabled() { return _enableDepth; }

Model3D::Model3D(const std::vector<std::shared_ptr<NodeGLTF>>& nodes,
                 const std::vector<std::shared_ptr<MeshStatic3D>>& meshes,
                 std::shared_ptr<CommandBuffer> commandBufferTransfer,
                 std::shared_ptr<GameState> gameState,
                 std::shared_ptr<EngineState> engineState) {
  setName("Model3D");
  _engineState = engineState;
  _nodes = nodes;
  _meshes = meshes;
  _gameState = gameState;
  auto settings = _engineState->getSettings();
  _changedMaterial.resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  // default material if model doesn't have material at all, we still have to send data to shader
  _defaultMaterialPhong = std::make_shared<MaterialPhong>(MaterialTarget::SIMPLE, commandBufferTransfer, engineState);
  _defaultMaterialPhong->setBaseColor({_gameState->getResourceManager()->getTextureOne()});
  _defaultMaterialPhong->setNormal({_gameState->getResourceManager()->getTextureZero()});
  _defaultMaterialPhong->setSpecular({_gameState->getResourceManager()->getTextureZero()});
  _defaultMaterialPBR = std::make_shared<MaterialPBR>(MaterialTarget::SIMPLE, commandBufferTransfer, engineState);
  _defaultMaterialColor = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, commandBufferTransfer, engineState);
  _materials = {_defaultMaterialPhong};

  _mesh = std::make_shared<MeshStatic3D>(engineState);
  _defaultAnimation = std::make_shared<Animation>(std::vector<std::shared_ptr<NodeGLTF>>{},
                                                  std::vector<std::shared_ptr<SkinGLTF>>{},
                                                  std::vector<std::shared_ptr<AnimationGLTF>>{}, engineState);
  _animation = _defaultAnimation;

  // initialize UBO
  _cameraUBOFull.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _cameraUBOFull[i] = std::make_shared<Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);

  // setup joints
  {
    _descriptorSetLayoutJoints = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutJoints{{.binding = 0,
                                                            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                            .descriptorCount = 1,
                                                            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                                            .pImmutableSamplers = nullptr}};
    _descriptorSetLayoutJoints->createCustom(layoutJoints);

    _updateJointsDescriptor();
  }

  // setup Normal
  {
    _descriptorSetLayoutNormalsMesh = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutNormalsMesh{{.binding = 0,
                                                                 .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                                 .descriptorCount = 1,
                                                                 .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                                                 .pImmutableSamplers = nullptr},
                                                                {.binding = 1,
                                                                 .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                                 .descriptorCount = 1,
                                                                 .stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT,
                                                                 .pImmutableSamplers = nullptr}};
    _descriptorSetLayoutNormalsMesh->createCustom(layoutNormalsMesh);

    // TODO: we can just have one buffer and put it twice to descriptor
    _descriptorSetNormalsMesh.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetNormalsMesh[i] = std::make_shared<DescriptorSet>(_descriptorSetLayoutNormalsMesh, engineState);
      std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoNormalsMesh = {
          {0,
           {VkDescriptorBufferInfo{.buffer = _cameraUBOFull[i]->getData(),
                                   .offset = 0,
                                   .range = _cameraUBOFull[i]->getSize()}}},
          {1,
           {VkDescriptorBufferInfo{.buffer = _cameraUBOFull[i]->getData(),
                                   .offset = 0,
                                   .range = _cameraUBOFull[i]->getSize()}}}};
      _descriptorSetNormalsMesh[i]->createCustom(bufferInfoNormalsMesh, {});
    }

    // initialize Normal (per vertex)
    auto shader = std::make_shared<Shader>(_engineState);
    shader->add("shaders/shape/cubeNormal_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
    shader->add("shaders/shape/cubeNormal_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    shader->add("shaders/shape/cubeNormal_geometry.spv", VK_SHADER_STAGE_GEOMETRY_BIT);

    _pipelineNormalMesh = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                            _engineState->getDevice());
    _pipelineNormalMesh->setDepthTest(true);
    _pipelineNormalMesh->setDepthWrite(true);
    _pipelineNormalMesh->setCullMode(VK_CULL_MODE_BACK_BIT);
    _pipelineNormalMesh->createCustom(
        shader, std::vector{std::pair{std::string("normal"), _descriptorSetLayoutNormalsMesh}}, {},
        _mesh->getBindingDescription(),
        _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                                                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)}}),
        RenderPassScenario::GRAPHIC);

    // it's kind of pointless, but if material is set, double sided property can be handled
    _pipelineNormalMeshCullOff = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                                   _engineState->getDevice());
    _pipelineNormalMeshCullOff->setDepthTest(true);
    _pipelineNormalMeshCullOff->setDepthWrite(true);
    _pipelineNormalMeshCullOff->createCustom(
        shader, std::vector{std::pair{std::string("normal"), _descriptorSetLayoutNormalsMesh}}, {},
        _mesh->getBindingDescription(),
        _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                                                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)}}),
        RenderPassScenario::GRAPHIC);

    // initialize Tangent (per vertex)
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/shape/cubeTangent_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/shape/cubeNormal_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      shader->add("shaders/shape/cubeNormal_geometry.spv", VK_SHADER_STAGE_GEOMETRY_BIT);

      _pipelineTangentMesh = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                               _engineState->getDevice());
      _pipelineTangentMesh->setDepthTest(true);
      _pipelineTangentMesh->setDepthWrite(true);
      _pipelineTangentMesh->setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipelineTangentMesh->createCustom(
          shader, std::vector{std::pair{std::string("normal"), _descriptorSetLayoutNormalsMesh}}, {},
          _mesh->getBindingDescription(),
          _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                   {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                                                   {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)}}),
          RenderPassScenario::GRAPHIC);

      // it's kind of pointless, but if material is set, double sided property can be handled
      _pipelineTangentMeshCullOff = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                                      _engineState->getDevice());
      _pipelineTangentMeshCullOff->setDepthTest(true);
      _pipelineTangentMeshCullOff->setDepthWrite(true);
      _pipelineTangentMeshCullOff->createCustom(
          shader, std::vector{std::pair{std::string("normal"), _descriptorSetLayoutNormalsMesh}}, {},
          _mesh->getBindingDescription(),
          _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                   {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                                                   {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)}}),
          RenderPassScenario::GRAPHIC);
    }
  }

  // setup color
  {
    _descriptorSetLayoutColor = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutColor{{.binding = 0,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr}};
    _descriptorSetLayoutColor->createCustom(layoutColor);

    _descriptorSetColor.resize(_engineState->getSettings()->getMaxFramesInFlight());
    // phong is default, will form default descriptor only for it here
    // descriptors are formed in setMaterial

    // initialize Color
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/model/modelColor_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/model/modelColor_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

      _pipeline[MaterialType::COLOR] = std::make_shared<PipelineGraphic>(engineState->getRenderPassManager(),
                                                                         engineState->getDevice());
      std::vector<std::tuple<VkFormat, uint32_t>> attributes = {
          {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
          {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)},
          {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)},
          {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, jointIndices)},
          {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, jointWeights)}};
      _pipeline[MaterialType::COLOR]->setDepthTest(true);
      _pipeline[MaterialType::COLOR]->setDepthWrite(true);
      _pipeline[MaterialType::COLOR]->setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipeline[MaterialType::COLOR]->createCustom(
          shader, {{"color", _descriptorSetLayoutColor}, {"joints", _descriptorSetLayoutJoints}}, {},
          _mesh->getBindingDescription(), _mesh->Mesh3D::getAttributeDescriptions(attributes),
          RenderPassScenario::GRAPHIC);

      _pipelineCullOff[MaterialType::COLOR] = std::make_shared<PipelineGraphic>(engineState->getRenderPassManager(),
                                                                                engineState->getDevice());
      _pipelineCullOff[MaterialType::COLOR]->setDepthTest(true);
      _pipelineCullOff[MaterialType::COLOR]->setDepthWrite(true);
      _pipelineCullOff[MaterialType::COLOR]->createCustom(
          shader, {{"color", _descriptorSetLayoutColor}, {"joints", _descriptorSetLayoutJoints}}, {},
          _mesh->getBindingDescription(), _mesh->Mesh3D::getAttributeDescriptions(attributes),
          RenderPassScenario::GRAPHIC);

      _pipelineWireframe[MaterialType::COLOR] = std::make_shared<PipelineGraphic>(engineState->getRenderPassManager(),
                                                                                  engineState->getDevice());
      _pipelineWireframe[MaterialType::COLOR]->setDepthTest(true);
      _pipelineWireframe[MaterialType::COLOR]->setDepthWrite(true);
      _pipelineWireframe[MaterialType::COLOR]->setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipelineWireframe[MaterialType::COLOR]->setPolygonMode(VK_POLYGON_MODE_LINE);
      _pipelineWireframe[MaterialType::COLOR]->createCustom(
          shader, {{"color", _descriptorSetLayoutColor}, {"joints", _descriptorSetLayoutJoints}}, {},
          _mesh->getBindingDescription(), _mesh->Mesh3D::getAttributeDescriptions(attributes),
          RenderPassScenario::GRAPHIC);
    }
  }

  // setup Phong
  {
    _descriptorSetLayoutPhong = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutPhong{{.binding = 0,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 2,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 3,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 4,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr},
                                                          {.binding = 5,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                           .pImmutableSamplers = nullptr}};
    _descriptorSetLayoutPhong->createCustom(layoutPhong);

    _descriptorSetPhong.resize(_engineState->getSettings()->getMaxFramesInFlight());
    setMaterial({_defaultMaterialPhong});

    auto shader = std::make_shared<Shader>(_engineState);
    shader->add("shaders/model/modelPhong_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
    shader->add("shaders/model/modelPhong_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    _pipeline[MaterialType::PHONG] = std::make_shared<PipelineGraphic>(engineState->getRenderPassManager(),
                                                                       engineState->getDevice());
    std::map<std::string, VkPushConstantRange> defaultPushConstants;
    defaultPushConstants["constants"] =
        VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(FragmentPush)};
    _pipeline[MaterialType::PHONG]->setDepthTest(true);
    _pipeline[MaterialType::PHONG]->setDepthWrite(true);
    _pipeline[MaterialType::PHONG]->setCullMode(VK_CULL_MODE_BACK_BIT);
    _pipeline[MaterialType::PHONG]->createCustom(shader,
                                                 {{"phong", _descriptorSetLayoutPhong},
                                                  {"joints", _descriptorSetLayoutJoints},
                                                  {"globalPhong", _gameState->getLightManager()->getDSLGlobalPhong()}},
                                                 defaultPushConstants, _mesh->getBindingDescription(),
                                                 _mesh->getAttributeDescriptions(), RenderPassScenario::GRAPHIC);

    _pipelineCullOff[MaterialType::PHONG] = std::make_shared<PipelineGraphic>(engineState->getRenderPassManager(),
                                                                              engineState->getDevice());
    _pipelineCullOff[MaterialType::PHONG]->setDepthTest(true);
    _pipelineCullOff[MaterialType::PHONG]->setDepthWrite(true);
    _pipelineCullOff[MaterialType::PHONG]->createCustom(
        shader,
        {{"phong", _descriptorSetLayoutPhong},
         {"joints", _descriptorSetLayoutJoints},
         {"globalPhong", _gameState->getLightManager()->getDSLGlobalPhong()}},
        defaultPushConstants, _mesh->getBindingDescription(), _mesh->getAttributeDescriptions(),
        RenderPassScenario::GRAPHIC);

    _pipelineWireframe[MaterialType::PHONG] = std::make_shared<PipelineGraphic>(engineState->getRenderPassManager(),
                                                                                engineState->getDevice());
    _pipelineWireframe[MaterialType::PHONG]->setDepthTest(true);
    _pipelineWireframe[MaterialType::PHONG]->setDepthWrite(true);
    _pipelineWireframe[MaterialType::PHONG]->setCullMode(VK_CULL_MODE_BACK_BIT);
    _pipelineWireframe[MaterialType::PHONG]->setPolygonMode(VK_POLYGON_MODE_LINE);
    _pipelineWireframe[MaterialType::PHONG]->createCustom(
        shader,
        {{"phong", _descriptorSetLayoutPhong},
         {"joints", _descriptorSetLayoutJoints},
         {"globalPhong", _gameState->getLightManager()->getDSLGlobalPhong()}},
        defaultPushConstants, _mesh->getBindingDescription(), _mesh->getAttributeDescriptions(),
        RenderPassScenario::GRAPHIC);
  }

  // setup PBR
  {
    _descriptorSetLayoutPBR = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutPBR{{.binding = 0,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 1,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 2,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 3,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 4,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 5,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 6,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 7,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 8,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 9,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 10,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr},
                                                        {.binding = 11,
                                                         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                         .descriptorCount = 1,
                                                         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                         .pImmutableSamplers = nullptr}};

    _descriptorSetLayoutPBR->createCustom(layoutPBR);

    _descriptorSetPBR.resize(_engineState->getSettings()->getMaxFramesInFlight());
    // phong is default, will form default descriptor only for it here
    // descriptors are formed in setMaterial

    // initialize PBR
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/model/modelPBR_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/model/modelPBR_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

      _pipeline[MaterialType::PBR] = std::make_shared<PipelineGraphic>(engineState->getRenderPassManager(),
                                                                       engineState->getDevice());
      _pipeline[MaterialType::PBR]->setDepthTest(true);
      _pipeline[MaterialType::PBR]->setDepthWrite(true);
      _pipeline[MaterialType::PBR]->setCullMode(VK_CULL_MODE_BACK_BIT);

      std::map<std::string, VkPushConstantRange> defaultPushConstants;
      defaultPushConstants["constants"] =
          VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(FragmentPush)};

      _pipeline[MaterialType::PBR]->createCustom(shader,
                                                 {{"pbr", _descriptorSetLayoutPBR},
                                                  {"joints", _descriptorSetLayoutJoints},
                                                  {"globalPBR", _gameState->getLightManager()->getDSLGlobalPBR()}},
                                                 defaultPushConstants, _mesh->getBindingDescription(),
                                                 _mesh->getAttributeDescriptions(), RenderPassScenario::GRAPHIC);

      _pipelineCullOff[MaterialType::PBR] = std::make_shared<PipelineGraphic>(engineState->getRenderPassManager(),
                                                                              engineState->getDevice());
      _pipelineCullOff[MaterialType::PBR]->setDepthTest(true);
      _pipelineCullOff[MaterialType::PBR]->setDepthWrite(true);
      _pipelineCullOff[MaterialType::PBR]->createCustom(
          shader,
          {{"pbr", _descriptorSetLayoutPBR},
           {"joints", _descriptorSetLayoutJoints},
           {"globalPBR", _gameState->getLightManager()->getDSLGlobalPBR()}},
          defaultPushConstants, _mesh->getBindingDescription(), _mesh->getAttributeDescriptions(),
          RenderPassScenario::GRAPHIC);

      _pipelineWireframe[MaterialType::PBR] = std::make_shared<PipelineGraphic>(engineState->getRenderPassManager(),
                                                                                engineState->getDevice());
      _pipelineWireframe[MaterialType::PBR]->setDepthTest(true);
      _pipelineWireframe[MaterialType::PBR]->setDepthWrite(true);
      _pipelineWireframe[MaterialType::PBR]->setPolygonMode(VK_POLYGON_MODE_LINE);
      _pipelineWireframe[MaterialType::PBR]->createCustom(
          shader,
          {{"pbr", _descriptorSetLayoutPBR},
           {"joints", _descriptorSetLayoutJoints},
           {"globalPBR", _gameState->getLightManager()->getDSLGlobalPBR()}},
          defaultPushConstants, _mesh->getBindingDescription(), _mesh->getAttributeDescriptions(),
          RenderPassScenario::GRAPHIC);
    }
  }

  auto cameraLayout = std::make_shared<DescriptorSetLayout>(engineState->getDevice());
  VkDescriptorSetLayoutBinding layoutBinding = {.binding = 0,
                                                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                .descriptorCount = 1,
                                                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                                .pImmutableSamplers = nullptr};
  cameraLayout->createCustom({layoutBinding});

  _cameraUBODepth.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _descriptorSetCameraDepth.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    for (int l = 0; l < _engineState->getSettings()->getMaxDirectionalLights(); l++) {
      auto buffer = std::make_shared<Buffer>(sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                             _engineState);
      _cameraUBODepth[i].push_back({buffer});

      auto cameraSet = std::make_shared<DescriptorSet>(cameraLayout, _engineState);
      std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfo = {
          {0, {{.buffer = buffer->getData(), .offset = 0, .range = buffer->getSize()}}}};
      cameraSet->createCustom(bufferInfo, {});
      _descriptorSetCameraDepth[i].push_back({cameraSet});
    }
    for (int l = 0; l < _engineState->getSettings()->getMaxPointLights(); l++) {
      std::vector<std::shared_ptr<Buffer>> facesBuffer(6);
      std::vector<std::shared_ptr<DescriptorSet>> facesSet(6);
      for (int f = 0; f < 6; f++) {
        facesBuffer[f] = std::make_shared<Buffer>(
            sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);
        facesSet[f] = std::make_shared<DescriptorSet>(cameraLayout, _engineState);
        std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfo = {
            {0, {{.buffer = facesBuffer[f]->getData(), .offset = 0, .range = facesBuffer[f]->getSize()}}}};
        facesSet[f]->createCustom(bufferInfo, {});
      }
      _cameraUBODepth[i].push_back(facesBuffer);
      _descriptorSetCameraDepth[i].push_back(facesSet);
    }
  }

  // initialize depth directional
  {
    auto shader = std::make_shared<Shader>(_engineState);
    shader->add("shaders/model/modelDepth_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
    shader->add("shaders/model/modelDepthDirectional_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    _pipelineDirectional = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                             _engineState->getDevice());
    _pipelineDirectional->setDepthBias(true);
    _pipelineDirectional->setColorBlendOp(VK_BLEND_OP_MIN);
    _pipelineDirectional->createCustom(
        shader, {{"depth", cameraLayout}, {"joints", _descriptorSetLayoutJoints}}, {}, _mesh->getBindingDescription(),
        _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                 {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, jointIndices)},
                                                 {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, jointWeights)}}),
        RenderPassScenario::SHADOW);
  }

  // initialize depth point
  {
    auto shader = std::make_shared<Shader>(_engineState);
    shader->add("shaders/model/modelDepth_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
    shader->add("shaders/model/modelDepthPoint_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    _pipelinePoint = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(), _engineState->getDevice());
    std::map<std::string, VkPushConstantRange> defaultPushConstants;
    defaultPushConstants["constants"] = VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(FragmentPointLightPushDepth),
    };

    _pipelinePoint->setDepthBias(true);
    _pipelinePoint->setColorBlendOp(VK_BLEND_OP_MIN);
    _pipelinePoint->createCustom(
        shader, {{"depth", cameraLayout}, {"joints", _descriptorSetLayoutJoints}}, defaultPushConstants,
        _mesh->getBindingDescription(),
        _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                 {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, jointIndices)},
                                                 {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, jointWeights)}}),
        RenderPassScenario::SHADOW);
  }
}

void Model3D::_updateJointsDescriptor() {
  _descriptorSetJoints.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _descriptorSetJoints[i].resize(_animation->getGlobalMatricesBuffer().size());
    for (int skin = 0; skin < _animation->getGlobalMatricesBuffer().size(); skin++) {
      _descriptorSetJoints[i][skin] = std::make_shared<DescriptorSet>(_descriptorSetLayoutJoints, _engineState);

      std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfo = {
          {0,
           {{.buffer = _animation->getGlobalMatricesBuffer()[skin][i]->getData(),
             .offset = 0,
             .range = _animation->getGlobalMatricesBuffer()[skin][i]->getSize()}}}};
      _descriptorSetJoints[i][skin]->createCustom(bufferInfo, {});
    }
  }
}

void Model3D::_updatePBRDescriptor() {
  int currentFrame = _engineState->getFrameInFlight();
  for (int i = 0; i < _materials.size(); i++) {
    std::shared_ptr<MaterialPBR> material = std::dynamic_pointer_cast<MaterialPBR>(_materials[i]);
    std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
        {0,
         {{.buffer = _cameraUBOFull[currentFrame]->getData(),
           .offset = 0,
           .range = _cameraUBOFull[currentFrame]->getSize()}}},
        {10,
         {{.buffer = material->getBufferAlphaCutoff()[currentFrame]->getData(),
           .offset = 0,
           .range = material->getBufferAlphaCutoff()[currentFrame]->getSize()}}},
        {11,
         {{.buffer = material->getBufferCoefficients()[currentFrame]->getData(),
           .offset = 0,
           .range = material->getBufferCoefficients()[currentFrame]->getSize()}}}};

    std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
        {1,
         {{.sampler = material->getBaseColor()[0]->getSampler()->getSampler(),
           .imageView = material->getBaseColor()[0]->getImageView()->getImageView(),
           .imageLayout = material->getBaseColor()[0]->getImageView()->getImage()->getImageLayout()}}},
        {2,
         {{.sampler = material->getNormal()[0]->getSampler()->getSampler(),
           .imageView = material->getNormal()[0]->getImageView()->getImageView(),
           .imageLayout = material->getNormal()[0]->getImageView()->getImage()->getImageLayout()}}},
        {3,
         {{.sampler = material->getMetallic()[0]->getSampler()->getSampler(),
           .imageView = material->getMetallic()[0]->getImageView()->getImageView(),
           .imageLayout = material->getMetallic()[0]->getImageView()->getImage()->getImageLayout()}}},
        {4,
         {{.sampler = material->getRoughness()[0]->getSampler()->getSampler(),
           .imageView = material->getRoughness()[0]->getImageView()->getImageView(),
           .imageLayout = material->getRoughness()[0]->getImageView()->getImage()->getImageLayout()}}},
        {5,
         {{.sampler = material->getOccluded()[0]->getSampler()->getSampler(),
           .imageView = material->getOccluded()[0]->getImageView()->getImageView(),
           .imageLayout = material->getOccluded()[0]->getImageView()->getImage()->getImageLayout()}}},
        {6,
         {{.sampler = material->getEmissive()[0]->getSampler()->getSampler(),
           .imageView = material->getEmissive()[0]->getImageView()->getImageView(),
           .imageLayout = material->getEmissive()[0]->getImageView()->getImage()->getImageLayout()}}},
        {7,
         {{.sampler = material->getDiffuseIBL()->getSampler()->getSampler(),
           .imageView = material->getDiffuseIBL()->getImageView()->getImageView(),
           .imageLayout = material->getDiffuseIBL()->getImageView()->getImage()->getImageLayout()}}},
        {8,
         {{.sampler = material->getSpecularIBL()->getSampler()->getSampler(),
           .imageView = material->getSpecularIBL()->getImageView()->getImageView(),
           .imageLayout = material->getSpecularIBL()->getImageView()->getImage()->getImageLayout()}}},
        {9,
         {{.sampler = material->getSpecularBRDF()->getSampler()->getSampler(),
           .imageView = material->getSpecularBRDF()->getImageView()->getImageView(),
           .imageLayout = material->getSpecularBRDF()->getImageView()->getImage()->getImageLayout()}}}};

    _descriptorSetPBR[currentFrame][i]->createCustom(bufferInfoColor, textureInfoColor);
  }
}

void Model3D::_updatePhongDescriptor() {
  int currentFrame = _engineState->getFrameInFlight();
  for (int i = 0; i < _materials.size(); i++) {
    std::shared_ptr<MaterialPhong> material = std::dynamic_pointer_cast<MaterialPhong>(_materials[i]);
    std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
        {0,
         {{.buffer = _cameraUBOFull[currentFrame]->getData(),
           .offset = 0,
           .range = _cameraUBOFull[currentFrame]->getSize()}}},
        {4,
         {{.buffer = material->getBufferAlphaCutoff()[currentFrame]->getData(),
           .offset = 0,
           .range = material->getBufferAlphaCutoff()[currentFrame]->getSize()}}},
        {5,
         {{.buffer = material->getBufferCoefficients()[currentFrame]->getData(),
           .offset = 0,
           .range = material->getBufferCoefficients()[currentFrame]->getSize()}}}};
    std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
        {1,
         {{.sampler = material->getBaseColor()[0]->getSampler()->getSampler(),
           .imageView = material->getBaseColor()[0]->getImageView()->getImageView(),
           .imageLayout = material->getBaseColor()[0]->getImageView()->getImage()->getImageLayout()}}},
        {2,
         {{.sampler = material->getNormal()[0]->getSampler()->getSampler(),
           .imageView = material->getNormal()[0]->getImageView()->getImageView(),
           .imageLayout = material->getNormal()[0]->getImageView()->getImage()->getImageLayout()}}},
        {3,
         {{.sampler = material->getSpecular()[0]->getSampler()->getSampler(),
           .imageView = material->getSpecular()[0]->getImageView()->getImageView(),
           .imageLayout = material->getSpecular()[0]->getImageView()->getImage()->getImageLayout()}}}};

    _descriptorSetPhong[currentFrame][i]->createCustom(bufferInfoColor, textureInfoColor);
  }
}

void Model3D::_updateColorDescriptor() {
  int currentFrame = _engineState->getFrameInFlight();
  for (int i = 0; i < _materials.size(); i++) {
    std::shared_ptr<MaterialColor> material = std::dynamic_pointer_cast<MaterialColor>(_materials[i]);
    std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
        {0,
         {{.buffer = _cameraUBOFull[currentFrame]->getData(),
           .offset = 0,
           .range = _cameraUBOFull[currentFrame]->getSize()}}}};
    auto texture = _defaultMaterialColor->getBaseColor();
    if (material->getBaseColor().size() > 0) texture = material->getBaseColor();
    std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
        {1,
         {{.sampler = texture[0]->getSampler()->getSampler(),
           .imageView = texture[0]->getImageView()->getImageView(),
           .imageLayout = texture[0]->getImageView()->getImage()->getImageLayout()}}}};
    _descriptorSetColor[currentFrame][i]->createCustom(bufferInfoColor, textureInfoColor);
  }
}

void Model3D::setMaterial(std::vector<std::shared_ptr<MaterialColor>> materials) {
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    for (int material = 0; material < materials.size(); material++) {
      if (material < _materials.size() && material < _descriptorSetColor[i].size())
        _materials[material]->unregisterUpdate(_descriptorSetColor[i][material]);

      if (_descriptorSetColor[i].size() <= material)
        _descriptorSetColor[i].push_back(std::make_shared<DescriptorSet>(

            _descriptorSetLayoutColor, _engineState));
      materials[material]->registerUpdate(_descriptorSetColor[i][material],
                                          {._frameInFlight = i, ._materialInfo = {{MaterialTexture::COLOR, 1}}});
    }
  }
  _materialType = MaterialType::COLOR;

  _materials.clear();
  for (auto& material : materials) {
    _materials.push_back(material);
  }
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void Model3D::setMaterial(std::vector<std::shared_ptr<MaterialPhong>> materials) {
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    for (int material = 0; material < materials.size(); material++) {
      if (material < _materials.size() && material < _descriptorSetPhong[i].size())
        _materials[material]->unregisterUpdate(_descriptorSetPhong[i][material]);

      if (_descriptorSetPhong[i].size() <= material)
        _descriptorSetPhong[i].push_back(std::make_shared<DescriptorSet>(

            _descriptorSetLayoutPhong, _engineState));
      materials[material]->registerUpdate(_descriptorSetPhong[i][material],
                                          {._frameInFlight = i,
                                           ._materialInfo = {{MaterialTexture::COLOR, 1},
                                                             {MaterialTexture::NORMAL, 2},
                                                             {MaterialTexture::SPECULAR, 3}}});
    }
  }
  _materialType = MaterialType::PHONG;

  _materials.clear();
  for (auto& material : materials) {
    _materials.push_back(material);
  }
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void Model3D::setMaterial(std::vector<std::shared_ptr<MaterialPBR>> materials) {
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    for (int material = 0; material < materials.size(); material++) {
      if (material < _materials.size() && material < _descriptorSetPBR[i].size())
        _materials[material]->unregisterUpdate(_descriptorSetPBR[i][material]);

      if (_descriptorSetPBR[i].size() <= material)
        _descriptorSetPBR[i].push_back(std::make_shared<DescriptorSet>(_descriptorSetLayoutPBR, _engineState));
      materials[material]->registerUpdate(_descriptorSetPBR[i][material],
                                          {._frameInFlight = i,
                                           ._materialInfo = {{MaterialTexture::COLOR, 1},
                                                             {MaterialTexture::NORMAL, 2},
                                                             {MaterialTexture::METALLIC, 3},
                                                             {MaterialTexture::ROUGHNESS, 4},
                                                             {MaterialTexture::OCCLUSION, 5},
                                                             {MaterialTexture::EMISSIVE, 6},
                                                             {MaterialTexture::IBL_DIFFUSE, 7},
                                                             {MaterialTexture::IBL_SPECULAR, 8},
                                                             {MaterialTexture::BRDF_SPECULAR, 9}}});
    }
  }
  _materialType = MaterialType::PBR;
  _materials.clear();
  for (auto& material : materials) {
    _materials.push_back(material);
  }
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void Model3D::setDrawType(DrawType drawType) { _drawType = drawType; }

MaterialType Model3D::getMaterialType() { return _materialType; }

DrawType Model3D::getDrawType() { return _drawType; }

std::shared_ptr<AABB> Model3D::getAABBPositions() {
  std::shared_ptr<AABB> aabbTotal = std::make_shared<AABB>();
  for (auto& mesh : _meshes) {
    auto aabb = mesh->getAABBPositions();
    aabbTotal->extend(aabb);
  }
  return aabbTotal;
}

std::shared_ptr<AABB> Model3D::getAABBJoints() {
  auto matrices = _animation->getJointMatrices();
  std::shared_ptr<AABB> aabbTotal = std::make_shared<AABB>();
  for (auto& mesh : _meshes) {
    for (auto [joint, aabb] : mesh->getAABBJoints()) {
      if (matrices.size() > joint) {
        auto jointMatrix = mesh->getGlobalMatrix() * matrices[joint];
        auto newMin = jointMatrix * glm::vec4(aabb->getMin(), 1.f);
        auto newMax = jointMatrix * glm::vec4(aabb->getMax(), 1.f);
        aabbTotal->extend(newMin);
        aabbTotal->extend(newMax);
      }
    }
  }
  return aabbTotal;
}

void Model3D::setAnimation(std::shared_ptr<Animation> animation) {
  _animation = animation;
  _updateJointsDescriptor();
}

void Model3D::_drawNode(std::shared_ptr<CommandBuffer> commandBuffer,
                        std::shared_ptr<Pipeline> pipeline,
                        std::shared_ptr<Pipeline> pipelineCullOff,
                        std::shared_ptr<DescriptorSet> cameraDS,
                        std::shared_ptr<Buffer> cameraUBO,
                        glm::mat4 view,
                        glm::mat4 projection,
                        std::shared_ptr<NodeGLTF> node) {
  int currentFrame = _engineState->getFrameInFlight();
  if (node->mesh >= 0 && _meshes[node->mesh]->getPrimitives().size() > 0) {
    VkBuffer vertexBuffers[] = {_meshes[node->mesh]->getVertexBuffer()->getBuffer()->getData()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(),
                         _meshes[node->mesh]->getIndexBuffer()->getBuffer()->getData(), 0, VK_INDEX_TYPE_UINT32);

    // pass this matrix to uniforms
    BufferMVP cameraMVP{.model = getModel() * _meshes[node->mesh]->getGlobalMatrix(),
                        .view = view,
                        .projection = projection};

    cameraUBO->setData(&cameraMVP);

    auto pipelineLayout = pipeline->getDescriptorSetLayout();

    // normals and tangents
    auto normalTangentLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                            [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                              return info.first == std::string("normal");
                                            });
    if (normalTangentLayout != pipelineLayout.end()) {
      vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->getPipelineLayout(), 0, 1,
                              &_descriptorSetNormalsMesh[currentFrame]->getDescriptorSets(), 0, nullptr);
    }

    // depth
    auto depthLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                    [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                      return info.first == std::string("depth");
                                    });
    if (depthLayout != pipelineLayout.end()) {
      vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->getPipelineLayout(), 0, 1, &cameraDS->getDescriptorSets(), 0, nullptr);
    }

    // joints
    auto jointLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                    [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                      return info.first == std::string("joints");
                                    });

    if (jointLayout != pipelineLayout.end()) {
      // if node->skin == -1 then use 0 index that contains identity matrix because of animation default behavior
      vkCmdBindDescriptorSets(
          commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(), 1, 1,
          &_descriptorSetJoints[currentFrame][std::max(0, node->skin)]->getDescriptorSets(), 0, nullptr);
    }

    for (MeshPrimitive primitive : _meshes[node->mesh]->getPrimitives()) {
      if (primitive.indexCount > 0) {
        std::shared_ptr<Material> material = _defaultMaterialPhong;
        int materialIndex = 0;
        // Get the texture index for this primitive
        if (_materials.size() > 0) {
          material = _materials.front();
          if (primitive.materialIndex >= 0 && primitive.materialIndex < _materials.size()) {
            material = _materials[primitive.materialIndex];
            materialIndex = primitive.materialIndex;
          }
        }

        // color
        auto layoutColor = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                        [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                          return info.first == std::string("color");
                                        });
        if (layoutColor != pipelineLayout.end()) {
          vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline->getPipelineLayout(), 0, 1,
                                  &_descriptorSetColor[currentFrame][materialIndex]->getDescriptorSets(), 0, nullptr);
        }

        // phong
        auto layoutPhong = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                        [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                          return info.first == std::string("phong");
                                        });
        if (layoutPhong != pipelineLayout.end()) {
          vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline->getPipelineLayout(), 0, 1,
                                  &_descriptorSetPhong[currentFrame][materialIndex]->getDescriptorSets(), 0, nullptr);
        }

        // pbr
        auto layoutPBR = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                      [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                        return info.first == std::string("pbr");
                                      });
        if (layoutPBR != pipelineLayout.end()) {
          vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline->getPipelineLayout(), 0, 1,
                                  &_descriptorSetPBR[currentFrame][materialIndex]->getDescriptorSets(), 0, nullptr);
        }

        auto currentPipeline = pipeline;
        // by default double side is false
        if (material->getDoubleSided()) currentPipeline = pipelineCullOff;
        vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                          currentPipeline->getPipeline());
        vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), primitive.indexCount, 1, primitive.firstIndex, 0, 0);
      }
    }
  }

  for (auto& child : node->children) {
    _drawNode(commandBuffer, pipeline, pipelineCullOff, cameraDS, cameraUBO, view, projection, child);
  }
}

void Model3D::enableShadow(bool enable) { _enableShadow = enable; }

void Model3D::enableLighting(bool enable) { _enableLighting = enable; }

void Model3D::draw(std::shared_ptr<CommandBuffer> commandBuffer) {
  auto pipeline = _pipeline[_materialType];
  auto pipelineCullOff = _pipelineCullOff[_materialType];
  if (_drawType == DrawType::WIREFRAME) pipeline = _pipelineWireframe[_materialType];
  if (_drawType == DrawType::NORMAL) {
    pipeline = _pipelineNormalMesh;
    pipelineCullOff = _pipelineNormalMeshCullOff;
  }
  if (_drawType == DrawType::TANGENT) {
    pipeline = _pipelineTangentMesh;
    pipelineCullOff = _pipelineTangentMeshCullOff;
  }

  auto resolution = _engineState->getSettings()->getResolution();
  int currentFrame = _engineState->getFrameInFlight();

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

  VkViewport viewport{.x = 0.0f,
                      .y = static_cast<float>(std::get<1>(resolution)),
                      .width = static_cast<float>(std::get<0>(resolution)),
                      .height = static_cast<float>(-std::get<1>(resolution)),
                      .minDepth = 0.0f,
                      .maxDepth = 1.0f};
  vkCmdSetViewport(commandBuffer->getCommandBuffer(), 0, 1, &viewport);

  VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(std::get<0>(resolution), std::get<1>(resolution))};
  vkCmdSetScissor(commandBuffer->getCommandBuffer(), 0, 1, &scissor);

  if (pipeline->getPushConstants().find("constants") != pipeline->getPushConstants().end()) {
    FragmentPush pushConstants{.enableShadow = _enableShadow,
                               .enableLighting = _enableLighting,
                               .cameraPosition = _gameState->getCameraManager()->getCurrentCamera()->getEye()};
    auto info = pipeline->getPushConstants()["constants"];
    vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                       info.size, &pushConstants);
  }

  auto pipelineLayout = pipeline->getDescriptorSetLayout();

  // global Phong
  auto globalLayoutPhong = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                        [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                          return info.first == std::string("globalPhong");
                                        });
  if (globalLayoutPhong != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->getPipelineLayout(), 2, 1,
                            &_gameState->getLightManager()->getDSGlobalPhong()->getDescriptorSets(), 0, nullptr);
  }

  // global PBR
  auto globalLayoutPBR = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                      [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                        return info.first == std::string("globalPBR");
                                      });
  if (globalLayoutPBR != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->getPipelineLayout(), 2, 1,
                            &_gameState->getLightManager()->getDSGlobalPBR()->getDescriptorSets(), 0, nullptr);
  }

  // Render all nodes at top-level
  for (auto& node : _nodes) {
    _drawNode(commandBuffer, pipeline, pipelineCullOff, nullptr, _cameraUBOFull[currentFrame],
              _gameState->getCameraManager()->getCurrentCamera()->getView(),
              _gameState->getCameraManager()->getCurrentCamera()->getProjection(), node);
  }
}

void Model3D::drawShadow(LightType lightType, int lightIndex, int face, std::shared_ptr<CommandBuffer> commandBuffer) {
  if (_enableDepth == false) return;

  int currentFrame = _engineState->getFrameInFlight();
  auto pipeline = _pipelineDirectional;
  if (lightType == LightType::POINT) pipeline = _pipelinePoint;

  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());

  std::tuple<int, int> resolution;
  if (lightType == LightType::DIRECTIONAL) {
    resolution = _gameState->getLightManager()
                     ->getDirectionalShadows()[lightIndex]
                     ->getShadowMapTexture()[currentFrame]
                     ->getImageView()
                     ->getImage()
                     ->getResolution();
  }
  if (lightType == LightType::POINT) {
    resolution = _gameState->getLightManager()
                     ->getPointShadows()[lightIndex]
                     ->getShadowMapCubemap()[currentFrame]
                     ->getTexture()
                     ->getImageView()
                     ->getImage()
                     ->getResolution();
  }

  // Cube Maps have been specified to follow the RenderMan specification (for whatever reason),
  // and RenderMan assumes the images' origin being in the upper left so we don't need to swap anything
  VkViewport viewport{};
  if (lightType == LightType::DIRECTIONAL) {
    viewport = {.x = 0.f,
                .y = static_cast<float>(std::get<1>(resolution)),
                .width = static_cast<float>(std::get<0>(resolution)),
                .height = static_cast<float>(-std::get<1>(resolution))};
  } else if (lightType == LightType::POINT) {
    viewport = {.x = 0.f,
                .y = 0.f,
                .width = static_cast<float>(std::get<0>(resolution)),
                .height = static_cast<float>(std::get<1>(resolution))};
  }
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer->getCommandBuffer(), 0, 1, &viewport);

  VkRect2D scissor{.offset = {0, 0}, .extent = VkExtent2D(std::get<0>(resolution), std::get<1>(resolution))};
  vkCmdSetScissor(commandBuffer->getCommandBuffer(), 0, 1, &scissor);

  if (lightType == LightType::POINT) {
    if (pipeline->getPushConstants().find("constants") != pipeline->getPushConstants().end()) {
      FragmentPointLightPushDepth pushConstants;
      pushConstants.lightPosition =
          _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getPosition();
      // light camera
      pushConstants.far = _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getFar();
      auto info = pipeline->getPushConstants()["constants"];
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }
  }

  glm::mat4 view(1.f);
  glm::mat4 projection(1.f);
  int lightIndexTotal = lightIndex;
  if (lightType == LightType::DIRECTIONAL) {
    view = _gameState->getLightManager()->getDirectionalLights()[lightIndex]->getCamera()->getView();
    projection = _gameState->getLightManager()->getDirectionalLights()[lightIndex]->getCamera()->getProjection();
  }
  if (lightType == LightType::POINT) {
    lightIndexTotal += _engineState->getSettings()->getMaxDirectionalLights();
    view = _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getView(face);
    projection = _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getProjection();
  }
  // Render all nodes at top-level
  for (auto& node : _nodes) {
    _drawNode(commandBuffer, pipeline, pipeline, _descriptorSetCameraDepth[currentFrame][lightIndexTotal][face],
              _cameraUBODepth[currentFrame][lightIndexTotal][face], view, projection, node);
  }
}