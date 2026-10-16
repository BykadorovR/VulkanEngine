#include <cstring>
#include <algorithm>
#include <type_traits>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <future>
#include <unordered_map>
#include <glm/gtx/norm.hpp>
#include "Primitive/Model.h"

import Shader;

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
                 const RenderGraph::CommandBuffer* commandBufferTransfer,
                 std::shared_ptr<GameState> gameState,
                 std::shared_ptr<EngineState> engineState) {
  setName("Model3D");
  _engineState = engineState;
  _nodes = nodes;
  _meshes = meshes;
  _gameState = gameState;
  _changedMaterial.resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  _changedShadowDescriptors.resize(_engineState->getSettings()->getMaxFramesInFlight(), true);
  _changedJointsDescriptors.resize(_engineState->getSettings()->getMaxFramesInFlight(), true);
  // default material if model doesn't have material at all, we still have to send data to shader
  _defaultMaterialPhong = std::make_shared<MaterialPhong>(MaterialTarget::SIMPLE, engineState);
  _defaultMaterialPhong->setBaseColor({_gameState->getResourceManager()->getTextureOne()});
  _defaultMaterialPhong->setNormal({_gameState->getResourceManager()->getTextureZero()});
  _defaultMaterialPhong->setSpecular({_gameState->getResourceManager()->getTextureZero()});
  _defaultMaterialColor = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, engineState);
  _materials = {_defaultMaterialPhong};

  _mesh = std::make_shared<MeshStatic3D>(engineState);
  const auto vertexBinding = _mesh->getBindingDescription();
  const auto normalVertexAttributes = _mesh->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
       {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
       {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)}});
  const VkPipelineVertexInputStateCreateInfo normalVertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(normalVertexAttributes.size()),
      .pVertexAttributeDescriptions = normalVertexAttributes.data(),
  };
  const auto tangentVertexAttributes = _mesh->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
       {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)},
       {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, tangent)}});
  const VkPipelineVertexInputStateCreateInfo tangentVertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(tangentVertexAttributes.size()),
      .pVertexAttributeDescriptions = tangentVertexAttributes.data(),
  };
  const auto colorVertexAttributes = _mesh->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
       {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)},
       {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)},
       {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, jointIndices)},
       {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, jointWeights)}});
  const VkPipelineVertexInputStateCreateInfo colorVertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(colorVertexAttributes.size()),
      .pVertexAttributeDescriptions = colorVertexAttributes.data(),
  };
  const auto litVertexAttributes = _mesh->getAttributeDescriptions();
  const VkPipelineVertexInputStateCreateInfo litVertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(litVertexAttributes.size()),
      .pVertexAttributeDescriptions = litVertexAttributes.data(),
  };
  const auto shadowVertexAttributes = _mesh->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
       {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, jointIndices)},
       {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, jointWeights)}});
  const VkPipelineVertexInputStateCreateInfo shadowVertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(shadowVertexAttributes.size()),
      .pVertexAttributeDescriptions = shadowVertexAttributes.data(),
  };
  _defaultAnimation = std::make_shared<Animation>(std::vector<std::shared_ptr<NodeGLTF>>{},
                                                  std::vector<std::shared_ptr<SkinGLTF>>{},
                                                  std::vector<std::shared_ptr<AnimationGLTF>>{}, engineState);
  _animation = _defaultAnimation;

  // initialize UBO
  _cameraUBOFull.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _cameraUBOFull[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());

  auto shaderColor = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
  shaderColor->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                 "shaders/model/modelColor_vertex.spv"));
  shaderColor->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                 "shaders/model/modelColor_fragment.spv"));

  // setup joints
  {
    _descriptorSetLayoutJoints = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    _descriptorSetLayoutJoints->createCustom(shaderColor->getDescriptorSetLayoutBindings().at(1));
    _descriptorSetJoints.resize(_engineState->getSettings()->getMaxFramesInFlight());
  }

  // setup Normal
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/shape/cubeNormal_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/shape/cubeNormal_fragment.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/shape/cubeNormal_geometry.spv"));
    _descriptorSetLayoutNormalsMesh = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    _descriptorSetLayoutNormalsMesh->createCustom(shader->getDescriptorSetLayoutBindings().at(0));

    // TODO: we can just have one buffer and put it twice to descriptor
    _descriptorSetNormalsMesh.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetNormalsMesh[i] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayoutNormalsMesh).get()},
          *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
      _descriptorSetNormalsMesh[i]->add({_cameraUBOFull[i].get()});
      _descriptorSetNormalsMesh[i]->add({_cameraUBOFull[i].get()});
      _descriptorSetNormalsMesh[i]->initialize(*commandBufferTransfer);
    }

    std::vector<RenderGraph::DescriptorSetLayout*> normalLayouts{_descriptorSetLayoutNormalsMesh.get()};
    RenderGraph::PipelineGraphic normalPipelineGraphic;
    normalPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    normalPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    normalPipelineGraphic.setDepthTest(true);
    normalPipelineGraphic.setDepthWrite(true);
    normalPipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
    _pipelineNormalMesh = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelineNormalMesh->createGraphic(normalPipelineGraphic, shader->getShaderStageInfo(), normalLayouts, {},
                                       normalVertexInput);

    RenderGraph::PipelineGraphic normalCullOffPipelineGraphic;
    normalCullOffPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    normalCullOffPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    normalCullOffPipelineGraphic.setDepthTest(true);
    normalCullOffPipelineGraphic.setDepthWrite(true);
    normalCullOffPipelineGraphic.setCullMode(VK_CULL_MODE_NONE);
    _pipelineNormalMeshCullOff = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelineNormalMeshCullOff->createGraphic(normalCullOffPipelineGraphic, shader->getShaderStageInfo(), normalLayouts,
                                              {}, normalVertexInput);

    // initialize Tangent (per vertex)
    {
      auto tangentShader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
      tangentShader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                       "shaders/shape/cubeTangent_vertex.spv"));
      tangentShader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                       "shaders/shape/cubeNormal_fragment.spv"));
      tangentShader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                       "shaders/shape/cubeNormal_geometry.spv"));

      RenderGraph::PipelineGraphic tangentPipelineGraphic;
      tangentPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      tangentPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      tangentPipelineGraphic.setDepthTest(true);
      tangentPipelineGraphic.setDepthWrite(true);
      tangentPipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipelineTangentMesh = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipelineTangentMesh->createGraphic(tangentPipelineGraphic, tangentShader->getShaderStageInfo(), normalLayouts,
                                          {}, tangentVertexInput);

      RenderGraph::PipelineGraphic tangentCullOffPipelineGraphic;
      tangentCullOffPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      tangentCullOffPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      tangentCullOffPipelineGraphic.setDepthTest(true);
      tangentCullOffPipelineGraphic.setDepthWrite(true);
      tangentCullOffPipelineGraphic.setCullMode(VK_CULL_MODE_NONE);
      _pipelineTangentMeshCullOff = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipelineTangentMeshCullOff->createGraphic(tangentCullOffPipelineGraphic, tangentShader->getShaderStageInfo(),
                                                 normalLayouts, {}, tangentVertexInput);
    }
  }

  // setup color
  {
    _descriptorSetLayoutColor = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    _descriptorSetLayoutColor->createCustom(shaderColor->getDescriptorSetLayoutBindings().at(0));

    _descriptorSetColor.resize(_engineState->getSettings()->getMaxFramesInFlight());
    // phong is default, will form default descriptor only for it here
    // descriptors are formed in setMaterial

    std::vector<RenderGraph::DescriptorSetLayout*> colorLayouts{_descriptorSetLayoutColor.get(),
                                                                _descriptorSetLayoutJoints.get()};
    RenderGraph::PipelineGraphic colorPipelineGraphic;
    colorPipelineGraphic.setColorAttachments({engineState->getSettings()->getGraphicColorFormat()});
    colorPipelineGraphic.setDepthAttachment(engineState->getSettings()->getDepthFormat());
    colorPipelineGraphic.setDepthTest(true);
    colorPipelineGraphic.setDepthWrite(true);
    colorPipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
    _pipeline[MaterialType::COLOR] = std::make_shared<RenderGraph::Pipeline>(*engineState->getDevice());
    _pipeline[MaterialType::COLOR]->createGraphic(colorPipelineGraphic, shaderColor->getShaderStageInfo(), colorLayouts,
                                                  {}, colorVertexInput);

    RenderGraph::PipelineGraphic colorCullOffPipelineGraphic;
    colorCullOffPipelineGraphic.setColorAttachments({engineState->getSettings()->getGraphicColorFormat()});
    colorCullOffPipelineGraphic.setDepthAttachment(engineState->getSettings()->getDepthFormat());
    colorCullOffPipelineGraphic.setDepthTest(true);
    colorCullOffPipelineGraphic.setDepthWrite(true);
    colorCullOffPipelineGraphic.setCullMode(VK_CULL_MODE_NONE);
    _pipelineCullOff[MaterialType::COLOR] = std::make_shared<RenderGraph::Pipeline>(*engineState->getDevice());
    _pipelineCullOff[MaterialType::COLOR]->createGraphic(colorCullOffPipelineGraphic, shaderColor->getShaderStageInfo(),
                                                         colorLayouts, {}, colorVertexInput);

    RenderGraph::PipelineGraphic colorWireframePipelineGraphic;
    colorWireframePipelineGraphic.setColorAttachments({engineState->getSettings()->getGraphicColorFormat()});
    colorWireframePipelineGraphic.setDepthAttachment(engineState->getSettings()->getDepthFormat());
    colorWireframePipelineGraphic.setDepthTest(true);
    colorWireframePipelineGraphic.setDepthWrite(true);
    colorWireframePipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
#ifndef __ANDROID__
    colorWireframePipelineGraphic.setPolygonMode(VK_POLYGON_MODE_LINE);
#endif
    _pipelineWireframe[MaterialType::COLOR] = std::make_shared<RenderGraph::Pipeline>(*engineState->getDevice());
    _pipelineWireframe[MaterialType::COLOR]->createGraphic(
        colorWireframePipelineGraphic, shaderColor->getShaderStageInfo(), colorLayouts, {}, colorVertexInput);
  }

  // setup Phong
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/model/modelPhong_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/model/modelPhong_fragment.spv"));
    _descriptorSetLayoutPhong = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    _descriptorSetLayoutPhong->createCustom(shader->getDescriptorSetLayoutBindings().at(0));

    _descriptorSetPhong.resize(_engineState->getSettings()->getMaxFramesInFlight());
    setMaterial({_defaultMaterialPhong});

    std::unordered_map<std::string, VkPushConstantRange> defaultPushConstants;
    defaultPushConstants["constants"] =
        VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(FragmentPush)};
    const auto globalPhongLayout = _gameState->getLightManager()->getDSLGlobalPhong();
    std::vector<RenderGraph::DescriptorSetLayout*> phongLayouts{
        _descriptorSetLayoutPhong.get(), _descriptorSetLayoutJoints.get(), globalPhongLayout.get()};
    RenderGraph::PipelineGraphic phongPipelineGraphic;
    phongPipelineGraphic.setColorAttachments({engineState->getSettings()->getGraphicColorFormat()});
    phongPipelineGraphic.setDepthAttachment(engineState->getSettings()->getDepthFormat());
    phongPipelineGraphic.setDepthTest(true);
    phongPipelineGraphic.setDepthWrite(true);
    phongPipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
    _pipeline[MaterialType::PHONG] = std::make_shared<RenderGraph::Pipeline>(*engineState->getDevice());
    _pipeline[MaterialType::PHONG]->createGraphic(phongPipelineGraphic, shader->getShaderStageInfo(), phongLayouts,
                                                  defaultPushConstants, litVertexInput);

    RenderGraph::PipelineGraphic phongCullOffPipelineGraphic;
    phongCullOffPipelineGraphic.setColorAttachments({engineState->getSettings()->getGraphicColorFormat()});
    phongCullOffPipelineGraphic.setDepthAttachment(engineState->getSettings()->getDepthFormat());
    phongCullOffPipelineGraphic.setDepthTest(true);
    phongCullOffPipelineGraphic.setDepthWrite(true);
    phongCullOffPipelineGraphic.setCullMode(VK_CULL_MODE_NONE);
    _pipelineCullOff[MaterialType::PHONG] = std::make_shared<RenderGraph::Pipeline>(*engineState->getDevice());
    _pipelineCullOff[MaterialType::PHONG]->createGraphic(phongCullOffPipelineGraphic, shader->getShaderStageInfo(),
                                                         phongLayouts, defaultPushConstants, litVertexInput);

    RenderGraph::PipelineGraphic phongWireframePipelineGraphic;
    phongWireframePipelineGraphic.setColorAttachments({engineState->getSettings()->getGraphicColorFormat()});
    phongWireframePipelineGraphic.setDepthAttachment(engineState->getSettings()->getDepthFormat());
    phongWireframePipelineGraphic.setDepthTest(true);
    phongWireframePipelineGraphic.setDepthWrite(true);
    phongWireframePipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
#ifndef __ANDROID__
    phongWireframePipelineGraphic.setPolygonMode(VK_POLYGON_MODE_LINE);
#endif
    _pipelineWireframe[MaterialType::PHONG] = std::make_shared<RenderGraph::Pipeline>(*engineState->getDevice());
    _pipelineWireframe[MaterialType::PHONG]->createGraphic(phongWireframePipelineGraphic, shader->getShaderStageInfo(),
                                                           phongLayouts, defaultPushConstants, litVertexInput);
  }

  // setup PBR
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/model/modelPBR_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/model/modelPBR_fragment.spv"));
    _descriptorSetLayoutPBR = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    _descriptorSetLayoutPBR->createCustom(shader->getDescriptorSetLayoutBindings().at(0));

    _descriptorSetPBR.resize(_engineState->getSettings()->getMaxFramesInFlight());
    // phong is default, will form default descriptor only for it here
    // descriptors are formed in setMaterial

    const std::unordered_map<std::string, VkPushConstantRange> defaultPushConstants{
        {"constants", {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(FragmentPush)}}};
    const auto globalPBRLayout = _gameState->getLightManager()->getDSLGlobalPBR();
    std::vector<RenderGraph::DescriptorSetLayout*> pbrLayouts{_descriptorSetLayoutPBR.get(),
                                                              _descriptorSetLayoutJoints.get(), globalPBRLayout.get()};
    RenderGraph::PipelineGraphic pbrPipelineGraphic;
    pbrPipelineGraphic.setColorAttachments({engineState->getSettings()->getGraphicColorFormat()});
    pbrPipelineGraphic.setDepthAttachment(engineState->getSettings()->getDepthFormat());
    pbrPipelineGraphic.setDepthTest(true);
    pbrPipelineGraphic.setDepthWrite(true);
    pbrPipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
    _pipeline[MaterialType::PBR] = std::make_shared<RenderGraph::Pipeline>(*engineState->getDevice());
    _pipeline[MaterialType::PBR]->createGraphic(pbrPipelineGraphic, shader->getShaderStageInfo(), pbrLayouts,
                                                defaultPushConstants, litVertexInput);

    RenderGraph::PipelineGraphic pbrCullOffPipelineGraphic;
    pbrCullOffPipelineGraphic.setColorAttachments({engineState->getSettings()->getGraphicColorFormat()});
    pbrCullOffPipelineGraphic.setDepthAttachment(engineState->getSettings()->getDepthFormat());
    pbrCullOffPipelineGraphic.setDepthTest(true);
    pbrCullOffPipelineGraphic.setDepthWrite(true);
    pbrCullOffPipelineGraphic.setCullMode(VK_CULL_MODE_NONE);
    _pipelineCullOff[MaterialType::PBR] = std::make_shared<RenderGraph::Pipeline>(*engineState->getDevice());
    _pipelineCullOff[MaterialType::PBR]->createGraphic(pbrCullOffPipelineGraphic, shader->getShaderStageInfo(),
                                                       pbrLayouts, defaultPushConstants, litVertexInput);

    RenderGraph::PipelineGraphic pbrWireframePipelineGraphic;
    pbrWireframePipelineGraphic.setColorAttachments({engineState->getSettings()->getGraphicColorFormat()});
    pbrWireframePipelineGraphic.setDepthAttachment(engineState->getSettings()->getDepthFormat());
    pbrWireframePipelineGraphic.setDepthTest(true);
    pbrWireframePipelineGraphic.setDepthWrite(true);
#ifndef __ANDROID__
    pbrWireframePipelineGraphic.setPolygonMode(VK_POLYGON_MODE_LINE);
#endif
    _pipelineWireframe[MaterialType::PBR] = std::make_shared<RenderGraph::Pipeline>(*engineState->getDevice());
    _pipelineWireframe[MaterialType::PBR]->createGraphic(pbrWireframePipelineGraphic, shader->getShaderStageInfo(),
                                                         pbrLayouts, defaultPushConstants, litVertexInput);
  }

  auto shaderDirectional = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
  shaderDirectional->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                       "shaders/model/modelDepth_vertex.spv"));
  shaderDirectional->add(_engineState->getFilesystem()->readFile<char>(
      _engineState->getSettings()->getShadersPath() + "shaders/model/modelDepthDirectional_fragment.spv"));
  _descriptorSetLayoutDepth = std::make_shared<RenderGraph::DescriptorSetLayout>(*engineState->getDevice());
  _descriptorSetLayoutDepth->createCustom(shaderDirectional->getDescriptorSetLayoutBindings().at(0));

  _cameraUBODepth.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _descriptorSetCameraDepth.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    for (int l = 0; l < _engineState->getSettings()->getMaxDirectionalLights(); l++) {
      auto buffer = std::make_shared<RenderGraph::Buffer>(
          sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
          *_engineState->getMemoryAllocator());
      _cameraUBODepth[i].push_back({buffer});

      _descriptorSetCameraDepth[i].push_back({nullptr});
    }
    for (int l = 0; l < _engineState->getSettings()->getMaxPointLights(); l++) {
      std::vector<std::shared_ptr<RenderGraph::Buffer>> facesBuffer(6);
      for (int f = 0; f < 6; f++) {
        facesBuffer[f] = std::make_shared<RenderGraph::Buffer>(
            sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            *_engineState->getMemoryAllocator());
      }
      _cameraUBODepth[i].push_back(facesBuffer);
      _descriptorSetCameraDepth[i].push_back(std::vector<std::shared_ptr<RenderGraph::DescriptorSet>>(6));
    }
  }

  // initialize depth directional
  {
    RenderGraph::PipelineGraphic pipelineGraphic;
    pipelineGraphic.setColorAttachments({_engineState->getSettings()->getShadowMapFormat()});
    pipelineGraphic.setDepthAttachment(std::nullopt);
    pipelineGraphic.setDepthBias(true);
    pipelineGraphic.setColorBlendOp(VK_BLEND_OP_MIN);
    std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_descriptorSetLayoutDepth.get(),
                                                                        _descriptorSetLayoutJoints.get()};
    _pipelineDirectional = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelineDirectional->createGraphic(pipelineGraphic, shaderDirectional->getShaderStageInfo(), descriptorSetLayouts,
                                        {}, shadowVertexInput);
  }

  // initialize depth point
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/model/modelDepth_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/model/modelDepthPoint_fragment.spv"));
    std::unordered_map<std::string, VkPushConstantRange> defaultPushConstants;
    defaultPushConstants["constants"] = VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(FragmentPointLightPushDepth),
    };

    RenderGraph::PipelineGraphic pipelineGraphic;
    pipelineGraphic.setColorAttachments({_engineState->getSettings()->getShadowMapFormat()});
    pipelineGraphic.setDepthAttachment(std::nullopt);
    pipelineGraphic.setDepthBias(true);
    pipelineGraphic.setColorBlendOp(VK_BLEND_OP_MIN);
    std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_descriptorSetLayoutDepth.get(),
                                                                        _descriptorSetLayoutJoints.get()};
    _pipelinePoint = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelinePoint->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts,
                                  defaultPushConstants, shadowVertexInput);
  }
}

void Model3D::_updateDepthDescriptors(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  const std::vector<RenderGraph::DescriptorSetLayout*> layouts{_descriptorSetLayoutDepth.get()};
  for (int light = 0; light < _descriptorSetCameraDepth[currentFrame].size(); ++light) {
    for (int face = 0; face < _descriptorSetCameraDepth[currentFrame][light].size(); ++face) {
      auto descriptor = std::make_shared<RenderGraph::DescriptorSet>(layouts, *_engineState->getDescriptorPool(),
                                                                     *_engineState->getDevice());
      descriptor->add({_cameraUBODepth[currentFrame][light][face].get()});
      descriptor->initialize(commandBuffer);
      _descriptorSetCameraDepth[currentFrame][light][face] = std::move(descriptor);
    }
  }
}

void Model3D::_updateJointsDescriptor(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  const std::vector<RenderGraph::DescriptorSetLayout*> layouts{_descriptorSetLayoutJoints.get()};
  const auto& jointBuffers = _animation->getGlobalMatricesBuffer();
  _descriptorSetJoints[currentFrame].resize(jointBuffers.size());
  for (int skin = 0; skin < jointBuffers.size(); ++skin) {
    auto descriptor = std::make_shared<RenderGraph::DescriptorSet>(layouts, *_engineState->getDescriptorPool(),
                                                                   *_engineState->getDevice(), 1);
    descriptor->add({jointBuffers[skin][currentFrame].get()});
    descriptor->initialize(commandBuffer);
    _descriptorSetJoints[currentFrame][skin] = std::move(descriptor);
  }
}

void Model3D::_updatePBRDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  const std::vector<RenderGraph::DescriptorSetLayout*> layouts{_descriptorSetLayoutPBR.get()};
  _descriptorSetPBR[currentFrame].resize(_materials.size());
  for (int i = 0; i < _materials.size(); i++) {
    auto material = std::dynamic_pointer_cast<MaterialPBR>(_materials[i]);
    material->unregisterUpdate(_descriptorSetPBR[currentFrame][i]);
    auto descriptor = std::make_shared<RenderGraph::DescriptorSet>(layouts, *_engineState->getDescriptorPool(),
                                                                   *_engineState->getDevice());
    descriptor->add({_cameraUBOFull[currentFrame].get()});
    descriptor->add({material->getBaseColor()[0].get()});
    descriptor->add({material->getNormal()[0].get()});
    descriptor->add({material->getMetallic()[0].get()});
    descriptor->add({material->getRoughness()[0].get()});
    descriptor->add({material->getOccluded()[0].get()});
    descriptor->add({material->getEmissive()[0].get()});
    descriptor->add({material->getDiffuseIBL().get()});
    descriptor->add({material->getSpecularIBL().get()});
    descriptor->add({material->getSpecularBRDF().get()});
    descriptor->add({material->getBufferAlphaCutoff()[currentFrame].get()});
    descriptor->add({material->getBufferCoefficients()[currentFrame].get()});
    descriptor->initialize(commandBuffer);
    material->registerUpdate(descriptor, {.frameInFlight = currentFrame,
                                          .materialInfo = {{MaterialTexture::COLOR, 1},
                                                           {MaterialTexture::NORMAL, 2},
                                                           {MaterialTexture::METALLIC, 3},
                                                           {MaterialTexture::ROUGHNESS, 4},
                                                           {MaterialTexture::OCCLUSION, 5},
                                                           {MaterialTexture::EMISSIVE, 6},
                                                           {MaterialTexture::IBL_DIFFUSE, 7},
                                                           {MaterialTexture::IBL_SPECULAR, 8},
                                                           {MaterialTexture::BRDF_SPECULAR, 9}}});
    _descriptorSetPBR[currentFrame][i] = std::move(descriptor);
  }
}

void Model3D::_updatePhongDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  const std::vector<RenderGraph::DescriptorSetLayout*> layouts{_descriptorSetLayoutPhong.get()};
  _descriptorSetPhong[currentFrame].resize(_materials.size());
  for (int i = 0; i < _materials.size(); i++) {
    auto material = std::dynamic_pointer_cast<MaterialPhong>(_materials[i]);
    material->unregisterUpdate(_descriptorSetPhong[currentFrame][i]);
    auto descriptor = std::make_shared<RenderGraph::DescriptorSet>(layouts, *_engineState->getDescriptorPool(),
                                                                   *_engineState->getDevice());
    descriptor->add({_cameraUBOFull[currentFrame].get()});
    descriptor->add({material->getBaseColor()[0].get()});
    descriptor->add({material->getNormal()[0].get()});
    descriptor->add({material->getSpecular()[0].get()});
    descriptor->add({material->getBufferAlphaCutoff()[currentFrame].get()});
    descriptor->add({material->getBufferCoefficients()[currentFrame].get()});
    descriptor->initialize(commandBuffer);
    material->registerUpdate(
        descriptor,
        {.frameInFlight = currentFrame,
         .materialInfo = {{MaterialTexture::COLOR, 1}, {MaterialTexture::NORMAL, 2}, {MaterialTexture::SPECULAR, 3}}});
    _descriptorSetPhong[currentFrame][i] = std::move(descriptor);
  }
}

void Model3D::_updateColorDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  const std::vector<RenderGraph::DescriptorSetLayout*> layouts{_descriptorSetLayoutColor.get()};
  _descriptorSetColor[currentFrame].resize(_materials.size());
  for (int i = 0; i < _materials.size(); i++) {
    auto material = std::dynamic_pointer_cast<MaterialColor>(_materials[i]);
    auto texture = _defaultMaterialColor->getBaseColor();
    if (!material->getBaseColor().empty()) texture = material->getBaseColor();
    material->unregisterUpdate(_descriptorSetColor[currentFrame][i]);
    auto descriptor = std::make_shared<RenderGraph::DescriptorSet>(layouts, *_engineState->getDescriptorPool(),
                                                                   *_engineState->getDevice());
    descriptor->add({_cameraUBOFull[currentFrame].get()});
    descriptor->add({texture[0].get()});
    descriptor->initialize(commandBuffer);
    material->registerUpdate(descriptor,
                             {.frameInFlight = currentFrame, .materialInfo = {{MaterialTexture::COLOR, 1}}});
    _descriptorSetColor[currentFrame][i] = std::move(descriptor);
  }
}

void Model3D::_unregisterMaterialDescriptors() {
  for (int frame = 0; frame < _descriptorSetColor.size(); ++frame) {
    for (int materialIndex = 0; materialIndex < _materials.size(); ++materialIndex) {
      const auto& material = _materials[materialIndex];
      const auto unregister = [&](const auto& descriptors) {
        if (frame >= descriptors.size() || materialIndex >= descriptors[frame].size()) return;
        material->unregisterUpdate(descriptors[frame][materialIndex]);
      };
      unregister(_descriptorSetColor);
      unregister(_descriptorSetPhong);
      unregister(_descriptorSetPBR);
    }
  }
}

void Model3D::setMaterial(std::vector<std::shared_ptr<MaterialColor>> materials) {
  _materialType = MaterialType::COLOR;
  _unregisterMaterialDescriptors();
  _materials.assign(materials.begin(), materials.end());
  std::fill(_changedMaterial.begin(), _changedMaterial.end(), true);
}

void Model3D::setMaterial(std::vector<std::shared_ptr<MaterialPhong>> materials) {
  _materialType = MaterialType::PHONG;
  _unregisterMaterialDescriptors();
  _materials.assign(materials.begin(), materials.end());
  std::fill(_changedMaterial.begin(), _changedMaterial.end(), true);
}

void Model3D::setMaterial(std::vector<std::shared_ptr<MaterialPBR>> materials) {
  _materialType = MaterialType::PBR;
  _unregisterMaterialDescriptors();
  _materials.assign(materials.begin(), materials.end());
  std::fill(_changedMaterial.begin(), _changedMaterial.end(), true);
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
  for (auto& descriptors : _descriptorSetJoints) descriptors.clear();
  std::fill(_changedMaterial.begin(), _changedMaterial.end(), true);
  std::fill(_changedShadowDescriptors.begin(), _changedShadowDescriptors.end(), true);
  std::fill(_changedJointsDescriptors.begin(), _changedJointsDescriptors.end(), true);
}

void Model3D::_drawNode(const RenderGraph::CommandBuffer* commandBuffer,
                        std::shared_ptr<RenderGraph::Pipeline> pipeline,
                        std::shared_ptr<RenderGraph::Pipeline> pipelineCullOff,
                        std::shared_ptr<RenderGraph::DescriptorSet> cameraDS,
                        std::shared_ptr<RenderGraph::Buffer> cameraUBO,
                        glm::mat4 view,
                        glm::mat4 projection,
                        std::shared_ptr<NodeGLTF> node) {
  int currentFrame = _engineState->getFrameInFlight();
  if (node->mesh >= 0 && _meshes[node->mesh]->getPrimitives().size() > 0) {
    VkBuffer vertexBuffers[] = {_meshes[node->mesh]->getVertexBuffer()->getBuffer()->getBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(),
                         _meshes[node->mesh]->getIndexBuffer()->getBuffer()->getBuffer(), 0, VK_INDEX_TYPE_UINT32);

    // pass this matrix to uniforms
    BufferMVP cameraMVP{.model = getModel() * _meshes[node->mesh]->getGlobalMatrix(),
                        .view = view,
                        .projection = projection};

    std::memcpy(static_cast<std::byte*>(cameraUBO->getAllocationInfo().pMappedData), &cameraMVP,
                static_cast<std::size_t>(cameraUBO->getSize()));

    const auto skinIndex = std::max(0, node->skin);

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

        auto currentPipeline = pipeline;
        // by default double side is false
        if (material->getDoubleSided()) currentPipeline = pipelineCullOff;
        vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                          currentPipeline->getPipeline());
        if (cameraDS != nullptr) {
          cameraDS->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->getPipelineLayout(), *commandBuffer);
        } else if (_drawType == DrawType::NORMAL || _drawType == DrawType::TANGENT) {
          _descriptorSetNormalsMesh[currentFrame]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                        currentPipeline->getPipelineLayout(), *commandBuffer);
        } else {
          std::shared_ptr<RenderGraph::DescriptorSet> descriptor;
          if (_materialType == MaterialType::COLOR) descriptor = _descriptorSetColor[currentFrame][materialIndex];
          if (_materialType == MaterialType::PHONG) descriptor = _descriptorSetPhong[currentFrame][materialIndex];
          if (_materialType == MaterialType::PBR) descriptor = _descriptorSetPBR[currentFrame][materialIndex];
          descriptor->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->getPipelineLayout(), *commandBuffer);
          if (_materialType == MaterialType::PHONG) {
            _gameState->getLightManager()->getDSGlobalModelPhong()->bind(
                VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->getPipelineLayout(), *commandBuffer);
          } else if (_materialType == MaterialType::PBR) {
            _gameState->getLightManager()->getDSGlobalModelPBR()->bind(
                VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->getPipelineLayout(), *commandBuffer);
          }
        }
        const auto& descriptorLayouts = currentPipeline->getDescriptorSetLayout();
        if (std::find(descriptorLayouts.begin(), descriptorLayouts.end(), _descriptorSetLayoutJoints.get()) !=
            descriptorLayouts.end()) {
          _descriptorSetJoints[currentFrame][skinIndex]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                              currentPipeline->getPipelineLayout(), *commandBuffer);
        }
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

void Model3D::draw(const RenderGraph::CommandBuffer* commandBuffer) {
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
  const int currentFrame = _engineState->getFrameInFlight();
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
  {
    std::unique_lock<std::mutex> lock(_descriptorUpdateMutex);
    if (_changedJointsDescriptors[currentFrame]) {
      _updateJointsDescriptor(currentFrame, *commandBuffer);
      _changedJointsDescriptors[currentFrame] = false;
    }
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
    auto info = pipeline->getPushConstants().at("constants");
    vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                       info.size, &pushConstants);
  }

  // Render all nodes at top-level
  for (auto& node : _nodes) {
    _drawNode(commandBuffer, pipeline, pipelineCullOff, nullptr, _cameraUBOFull[currentFrame],
              _gameState->getCameraManager()->getCurrentCamera()->getView(),
              _gameState->getCameraManager()->getCurrentCamera()->getProjection(), node);
  }
}

void Model3D::drawShadow(LightType lightType,
                         int lightIndex,
                         int face,
                         const RenderGraph::CommandBuffer* commandBuffer) {
  if (_enableDepth == false) return;

  int currentFrame = _engineState->getFrameInFlight();
  {
    std::unique_lock<std::mutex> lock(_descriptorUpdateMutex);
    if (_changedShadowDescriptors[currentFrame]) {
      _updateDepthDescriptors(currentFrame, *commandBuffer);
      _changedShadowDescriptors[currentFrame] = false;
    }
    if (_changedJointsDescriptors[currentFrame]) {
      _updateJointsDescriptor(currentFrame, *commandBuffer);
      _changedJointsDescriptors[currentFrame] = false;
    }
  }
  auto pipeline = _pipelineDirectional;
  if (lightType == LightType::POINT) pipeline = _pipelinePoint;

  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());

  std::tuple<int, int> resolution;
  if (lightType == LightType::DIRECTIONAL) {
    const auto imageResolution = _gameState->getLightManager()
                                     ->getDirectionalShadows()[lightIndex]
                                     ->getShadowMapTexture()[currentFrame]
                                     ->getImageView()
                                     .getImage()
                                     .getResolution();
    resolution = {imageResolution.x, imageResolution.y};
  }
  if (lightType == LightType::POINT) {
    const auto imageResolution = _gameState->getLightManager()
                                     ->getPointShadows()[lightIndex]
                                     ->getShadowMapCubemap()[currentFrame]
                                     ->getTexture()
                                     ->getImageView()
                                     .getImage()
                                     .getResolution();
    resolution = {imageResolution.x, imageResolution.y};
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
      pushConstants.far = static_cast<int>(
          _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getFar());
      auto info = pipeline->getPushConstants().at("constants");
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
