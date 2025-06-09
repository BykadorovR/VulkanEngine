#include "Primitive/Shape3D.h"
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
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

Shape3DPhysics::Shape3DPhysics(glm::vec3 translate, glm::vec3 size, std::shared_ptr<PhysicsManager> physicsManager) {
  _physicsManager = physicsManager;
  JPH::BodyCreationSettings boxSettings(new JPH::BoxShape(JPH::Vec3(size.x, size.y, size.z)),
                                        JPH::RVec3(translate.x, translate.y, translate.z), JPH::Quat::sIdentity(),
                                        JPH::EMotionType::Dynamic, Layers::MOVING);
  _shapeBody = _physicsManager->getBodyInterface().CreateBody(boxSettings);
  _physicsManager->getBodyInterface().AddBody(_shapeBody->GetID(), JPH::EActivation::Activate);
}

void Shape3DPhysics::setTranslate(glm::vec3 translate) {
  _physicsManager->getBodyInterface().SetPosition(
      _shapeBody->GetID(), JPH::RVec3(translate.x, translate.y, translate.z), JPH::EActivation::Activate);
}

void Shape3DPhysics::setLinearVelocity(glm::vec3 velocity) {
  _physicsManager->getBodyInterface().SetLinearVelocity(_shapeBody->GetID(), {velocity.x, velocity.y, velocity.z});
}

glm::vec3 Shape3DPhysics::getTranslate() {
  auto position = _physicsManager->getBodyInterface().GetPosition(_shapeBody->GetID());
  return glm::vec3(position.GetX(), position.GetY(), position.GetZ());
}

glm::quat Shape3DPhysics::getRotate() {
  auto rotation = _physicsManager->getBodyInterface().GetRotation(_shapeBody->GetID());
  return glm::quat{rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ()};
}

Shape3DPhysics::~Shape3DPhysics() {
  _physicsManager->getBodyInterface().RemoveBody(_shapeBody->GetID());
  _physicsManager->getBodyInterface().DestroyBody(_shapeBody->GetID());
}

Shape3D::Shape3D(ShapeType shapeType,
                 std::shared_ptr<Mesh3D> mesh,
                 VkCullModeFlags cullMode,
                 std::shared_ptr<CommandBuffer> commandBufferTransfer,
                 std::shared_ptr<GameState> gameState,
                 std::shared_ptr<EngineState> engineState) {
  setName("Shape3D");
  _shapeType = shapeType;
  _engineState = engineState;
  _gameState = gameState;
  _cullMode = cullMode;
  _mesh = mesh;
  _changedMaterial.resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  // needed for layout
  _defaultMaterialColor = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, commandBufferTransfer, engineState);
  _defaultMaterialPhong = std::make_shared<MaterialPhong>(MaterialTarget::SIMPLE, commandBufferTransfer, engineState);
  _defaultMaterialPBR = std::make_shared<MaterialPBR>(MaterialTarget::SIMPLE, commandBufferTransfer, engineState);

  if (shapeType == ShapeType::CUBE) {
    _defaultMaterialColor->setBaseColor({_gameState->getResourceManager()->getCubemapOne()->getTexture()});
    _shadersColor[ShapeType::CUBE][MaterialType::COLOR] = {"shaders/shape/cubeColor_vertex.spv",
                                                           "shaders/shape/cubeColor_fragment.spv"};
    _shadersColor[ShapeType::CUBE][MaterialType::PHONG] = {"shaders/shape/cubePhong_vertex.spv",
                                                           "shaders/shape/cubePhong_fragment.spv"};
    _shadersColor[ShapeType::CUBE][MaterialType::PBR] = {"shaders/shape/cubePhong_vertex.spv",
                                                         "shaders/shape/cubePBR_fragment.spv"};
    _shadersLightDirectional[ShapeType::CUBE] = {"shaders/shape/cubeDepth_vertex.spv",
                                                 "shaders/shape/cubeDepthDirectional_fragment.spv"};
    _shadersLightPoint[ShapeType::CUBE] = {"shaders/shape/cubeDepth_vertex.spv",
                                           "shaders/shape/cubeDepthPoint_fragment.spv"};
    _shadersNormalsMesh[ShapeType::CUBE] = {"shaders/shape/cubeNormal_vertex.spv",
                                            "shaders/shape/cubeNormal_fragment.spv",
                                            "shaders/shape/cubeNormal_geometry.spv"};
    _shadersTangentMesh[ShapeType::CUBE] = {"shaders/shape/cubeTangent_vertex.spv",
                                            "shaders/shape/cubeNormal_fragment.spv",
                                            "shaders/shape/cubeNormal_geometry.spv"};
  } else {
    _defaultMaterialColor->setBaseColor({_gameState->getResourceManager()->getTextureOne()});
    _shadersColor[shapeType][MaterialType::COLOR] = {"shaders/shape/sphereColor_vertex.spv",
                                                     "shaders/shape/sphereColor_fragment.spv"};
    _shadersColor[shapeType][MaterialType::PHONG] = {"shaders/shape/spherePhong_vertex.spv",
                                                     "shaders/shape/spherePhong_fragment.spv"};
    _shadersColor[shapeType][MaterialType::PBR] = {"shaders/shape/spherePhong_vertex.spv",
                                                   "shaders/shape/spherePBR_fragment.spv"};
    _shadersLightDirectional[shapeType] = {"shaders/shape/sphereDepth_vertex.spv",
                                           "shaders/shape/sphereDepthDirectional_fragment.spv"};
    _shadersLightPoint[shapeType] = {"shaders/shape/sphereDepth_vertex.spv",
                                     "shaders/shape/sphereDepthPoint_fragment.spv"};
    _shadersNormalsMesh[shapeType] = {"shaders/shape/cubeNormal_vertex.spv", "shaders/shape/cubeNormal_fragment.spv",
                                      "shaders/shape/cubeNormal_geometry.spv"};
    _shadersTangentMesh[shapeType] = {"shaders/shape/cubeTangent_vertex.spv", "shaders/shape/cubeNormal_fragment.spv",
                                      "shaders/shape/cubeNormal_geometry.spv"};
  }

  _material = _defaultMaterialColor;

  _uniformBufferCamera.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _uniformBufferCamera[i] = std::make_shared<Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, engineState);
  // setup normals
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

    _descriptorSetNormalsMesh.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetNormalsMesh[i] = std::make_shared<DescriptorSet>(_descriptorSetLayoutNormalsMesh, engineState);
      std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoNormalsMesh = {
          {0,
           {{.buffer = _uniformBufferCamera[i]->getData(), .offset = 0, .range = _uniformBufferCamera[i]->getSize()}}},
          {1,
           {{.buffer = _uniformBufferCamera[i]->getData(), .offset = 0, .range = _uniformBufferCamera[i]->getSize()}}}};
      _descriptorSetNormalsMesh[i]->createCustom(bufferInfoNormalsMesh, {});
    }

    // initialize Normal (per vertex)
    {
      auto shader = std::make_shared<Shader>(engineState);
      shader->add(_shadersNormalsMesh[_shapeType][0], VK_SHADER_STAGE_VERTEX_BIT);
      shader->add(_shadersNormalsMesh[_shapeType][1], VK_SHADER_STAGE_FRAGMENT_BIT);
      shader->add(_shadersNormalsMesh[_shapeType][2], VK_SHADER_STAGE_GEOMETRY_BIT);

      _pipelineNormalMesh = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                              _engineState->getDevice());
      _pipelineNormalMesh->setDepthTest(true);
      _pipelineNormalMesh->setDepthWrite(true);
      _pipelineNormalMesh->setCullMode(cullMode);
      _pipelineNormalMesh->createCustom(
          shader, std::vector{std::pair{std::string("normal"), _descriptorSetLayoutNormalsMesh}}, {},
          _mesh->getBindingDescription(),
          _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                   {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                                                   {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)}}),
          RenderPassScenario::GRAPHIC);
    }
    // initialize Tangent (per vertex)
    {
      auto shader = std::make_shared<Shader>(engineState);
      shader->add(_shadersTangentMesh[_shapeType][0], VK_SHADER_STAGE_VERTEX_BIT);
      shader->add(_shadersTangentMesh[_shapeType][1], VK_SHADER_STAGE_FRAGMENT_BIT);
      shader->add(_shadersTangentMesh[_shapeType][2], VK_SHADER_STAGE_GEOMETRY_BIT);

      _pipelineTangentMesh = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                               _engineState->getDevice());
      _pipelineTangentMesh->setDepthTest(true);
      _pipelineTangentMesh->setDepthWrite(true);
      _pipelineTangentMesh->setCullMode(cullMode);
      _pipelineTangentMesh->createCustom(
          shader, std::vector{std::pair{std::string("normal"), _descriptorSetLayoutNormalsMesh}}, {},
          _mesh->getBindingDescription(),
          _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                                                   {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)},
                                                   {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, tangent)}}),
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

  // initialize camera UBO and descriptor sets for shadow
  // initialize UBO
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

  // setup color
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
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
    descriptorSetLayout->createCustom(layoutColor);
    _descriptorSetLayout[MaterialType::COLOR].push_back({"color", descriptorSetLayout});

    _descriptorSetColor.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetColor[i] = std::make_shared<DescriptorSet>(descriptorSetLayout, engineState);
    }
    setMaterial(_defaultMaterialColor);

    // initialize color
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add(_shadersColor[_shapeType][MaterialType::COLOR][0], VK_SHADER_STAGE_VERTEX_BIT);
      shader->add(_shadersColor[_shapeType][MaterialType::COLOR][1], VK_SHADER_STAGE_FRAGMENT_BIT);
      _pipeline[_shapeType][MaterialType::COLOR] = std::make_shared<PipelineGraphic>(
          _engineState->getRenderPassManager(), _engineState->getDevice());
      std::vector<std::tuple<VkFormat, uint32_t>> attributes;
      if (_shapeType == ShapeType::CUBE) {
        attributes = {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                      {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                      {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)}};
      } else {
        attributes = {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                      {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                      {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)},
                      {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}};
      }
      _pipeline[_shapeType][MaterialType::COLOR]->setDepthTest(true);
      _pipeline[_shapeType][MaterialType::COLOR]->setDepthWrite(true);
      _pipeline[_shapeType][MaterialType::COLOR]->setCullMode(cullMode);
      _pipeline[_shapeType][MaterialType::COLOR]->createCustom(
          shader, _descriptorSetLayout[MaterialType::COLOR], {}, _mesh->getBindingDescription(),
          _mesh->Mesh3D::getAttributeDescriptions(attributes), RenderPassScenario::GRAPHIC);

      _pipelineWireframe[_shapeType][MaterialType::COLOR] = std::make_shared<PipelineGraphic>(
          _engineState->getRenderPassManager(), _engineState->getDevice());
      _pipelineWireframe[_shapeType][MaterialType::COLOR]->setDepthTest(true);
      _pipelineWireframe[_shapeType][MaterialType::COLOR]->setDepthWrite(true);
      _pipelineWireframe[_shapeType][MaterialType::COLOR]->setCullMode(cullMode);
      _pipelineWireframe[_shapeType][MaterialType::COLOR]->setPolygonMode(VK_POLYGON_MODE_LINE);
      _pipelineWireframe[_shapeType][MaterialType::COLOR]->createCustom(
          shader, _descriptorSetLayout[MaterialType::COLOR], {}, _mesh->getBindingDescription(),
          _mesh->Mesh3D::getAttributeDescriptions(attributes), RenderPassScenario::GRAPHIC);
    }
  }

  // setup Phong
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
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
                                                           .pImmutableSamplers = nullptr}};
    descriptorSetLayout->createCustom(layoutPhong);
    _descriptorSetLayout[MaterialType::PHONG].push_back({"phong", descriptorSetLayout});
    _descriptorSetLayout[MaterialType::PHONG].push_back(
        {"globalPhong", _gameState->getLightManager()->getDSLGlobalPhong()});

    _descriptorSetPhong.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetPhong[i] = std::make_shared<DescriptorSet>(descriptorSetLayout, engineState);
    }
    // update descriptor set in setMaterial

    // initialize Phong
    {
      auto shader = std::make_shared<Shader>(engineState);
      shader->add(_shadersColor[_shapeType][MaterialType::PHONG][0], VK_SHADER_STAGE_VERTEX_BIT);
      shader->add(_shadersColor[_shapeType][MaterialType::PHONG][1], VK_SHADER_STAGE_FRAGMENT_BIT);

      _pipeline[_shapeType][MaterialType::PHONG] = std::make_shared<PipelineGraphic>(
          _engineState->getRenderPassManager(), _engineState->getDevice());

      std::vector<std::tuple<VkFormat, uint32_t>> attributes;
      if (_shapeType == ShapeType::CUBE) {
        attributes = {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                      {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                      {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)},
                      {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, tangent)}};
      } else {
        attributes = {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                      {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                      {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)},
                      {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)},
                      {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, tangent)}};
      }

      _pipeline[_shapeType][MaterialType::PHONG]->setDepthTest(true);
      _pipeline[_shapeType][MaterialType::PHONG]->setDepthWrite(true);
      _pipeline[_shapeType][MaterialType::PHONG]->setCullMode(cullMode);
      _pipeline[_shapeType][MaterialType::PHONG]->createCustom(
          shader, _descriptorSetLayout[MaterialType::PHONG],
          std::map<std::string, VkPushConstantRange>{
              {std::string("constants"), VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                             .offset = 0,
                                                             .size = sizeof(FragmentPush)}}},
          _mesh->getBindingDescription(), _mesh->Mesh3D::getAttributeDescriptions(attributes),
          RenderPassScenario::GRAPHIC);
      // wireframe one
      _pipelineWireframe[_shapeType][MaterialType::PHONG] = std::make_shared<PipelineGraphic>(
          _engineState->getRenderPassManager(), _engineState->getDevice());
      _pipelineWireframe[_shapeType][MaterialType::PHONG]->setDepthTest(true);
      _pipelineWireframe[_shapeType][MaterialType::PHONG]->setDepthWrite(true);
      _pipelineWireframe[_shapeType][MaterialType::PHONG]->setCullMode(cullMode);
      _pipelineWireframe[_shapeType][MaterialType::PHONG]->setPolygonMode(VK_POLYGON_MODE_LINE);
      _pipelineWireframe[_shapeType][MaterialType::PHONG]->createCustom(
          shader, _descriptorSetLayout[MaterialType::PHONG],
          std::map<std::string, VkPushConstantRange>{
              {std::string("constants"), VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                             .offset = 0,
                                                             .size = sizeof(FragmentPush)}}},
          _mesh->getBindingDescription(), _mesh->Mesh3D::getAttributeDescriptions(attributes),
          RenderPassScenario::GRAPHIC);
    }
  }

  // setup PBR
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
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
    descriptorSetLayout->createCustom(layoutPBR);
    _descriptorSetLayout[MaterialType::PBR].push_back({"pbr", descriptorSetLayout});
    _descriptorSetLayout[MaterialType::PBR].push_back({"globalPBR", _gameState->getLightManager()->getDSLGlobalPBR()});

    _descriptorSetPBR.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetPBR[i] = std::make_shared<DescriptorSet>(descriptorSetLayout, engineState);
    }
    // update descriptor set in setMaterial

    // initialize PBR
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add(_shadersColor[_shapeType][MaterialType::PBR][0], VK_SHADER_STAGE_VERTEX_BIT);
      shader->add(_shadersColor[_shapeType][MaterialType::PBR][1], VK_SHADER_STAGE_FRAGMENT_BIT);

      _pipeline[_shapeType][MaterialType::PBR] = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                                                   _engineState->getDevice());
      std::vector<std::tuple<VkFormat, uint32_t>> attributes;
      if (_shapeType == ShapeType::CUBE) {
        attributes = {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                      {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                      {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)},
                      {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, tangent)}};
      } else {
        attributes = {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                      {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                      {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)},
                      {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)},
                      {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, tangent)}};
      }
      _pipeline[_shapeType][MaterialType::PBR]->setDepthTest(true);
      _pipeline[_shapeType][MaterialType::PBR]->setDepthWrite(true);
      _pipeline[_shapeType][MaterialType::PBR]->setCullMode(cullMode);
      _pipeline[_shapeType][MaterialType::PBR]->createCustom(
          shader, _descriptorSetLayout[MaterialType::PBR],
          std::map<std::string, VkPushConstantRange>{
              {"constants", {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(FragmentPush)}}},
          _mesh->getBindingDescription(), _mesh->Mesh3D::getAttributeDescriptions(attributes),
          RenderPassScenario::GRAPHIC);
      // wireframe one
      _pipelineWireframe[_shapeType][MaterialType::PBR] = std::make_shared<PipelineGraphic>(
          _engineState->getRenderPassManager(), _engineState->getDevice());
      _pipelineWireframe[_shapeType][MaterialType::PBR]->setDepthTest(true);
      _pipelineWireframe[_shapeType][MaterialType::PBR]->setDepthWrite(true);
      _pipelineWireframe[_shapeType][MaterialType::PBR]->setCullMode(cullMode);
      _pipelineWireframe[_shapeType][MaterialType::PBR]->setPolygonMode(VK_POLYGON_MODE_LINE);
      _pipelineWireframe[_shapeType][MaterialType::PBR]->createCustom(
          shader, _descriptorSetLayout[MaterialType::PBR],
          std::map<std::string, VkPushConstantRange>{
              {"constants", {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(FragmentPush)}}},
          _mesh->getBindingDescription(), _mesh->Mesh3D::getAttributeDescriptions(attributes),
          RenderPassScenario::GRAPHIC);
    }
  }

  // initialize shadows directional
  {
    auto shader = std::make_shared<Shader>(_engineState);
    shader->add(_shadersLightDirectional[_shapeType][0], VK_SHADER_STAGE_VERTEX_BIT);
    shader->add(_shadersLightDirectional[_shapeType][1], VK_SHADER_STAGE_FRAGMENT_BIT);
    _pipelineDirectional = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                             _engineState->getDevice());
    _pipelineDirectional->setDepthBias(true);
    _pipelineDirectional->setColorBlendOp(VK_BLEND_OP_MIN);
    _pipelineDirectional->createCustom(
        shader, {{"depth", cameraLayout}}, {}, _mesh->getBindingDescription(),
        _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}}),
        RenderPassScenario::SHADOW);
  }

  // initialize shadows point
  {
    std::map<std::string, VkPushConstantRange> defaultPushConstants;
    defaultPushConstants["constants"] = VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(FragmentPointLightPushDepth),
    };

    auto shader = std::make_shared<Shader>(_engineState);
    shader->add(_shadersLightPoint[_shapeType][0], VK_SHADER_STAGE_VERTEX_BIT);
    shader->add(_shadersLightPoint[_shapeType][1], VK_SHADER_STAGE_FRAGMENT_BIT);
    _pipelinePoint = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(), _engineState->getDevice());
    _pipelinePoint->setDepthBias(true);
    _pipelinePoint->setColorBlendOp(VK_BLEND_OP_MIN);
    _pipelinePoint->createCustom(
        shader, {{"depth", cameraLayout}}, defaultPushConstants, _mesh->getBindingDescription(),
        _mesh->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}}),
        RenderPassScenario::SHADOW);
  }
}

void Shape3D::_updateColorDescriptor() {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialColor>(_material);

  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
      {0,
       {{.buffer = _uniformBufferCamera[frameInFlight]->getData(),
         .offset = 0,
         .range = _uniformBufferCamera[frameInFlight]->getSize()}}}};
  std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
      {1,
       {{.sampler = material->getBaseColor()[0]->getSampler()->getSampler(),
         .imageView = material->getBaseColor()[0]->getImageView()->getImageView(),
         .imageLayout = material->getBaseColor()[0]->getImageView()->getImage()->getImageLayout()}}}};
  _descriptorSetColor[frameInFlight]->createCustom(bufferInfoColor, textureInfoColor);
}

void Shape3D::_updatePhongDescriptor() {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialPhong>(_material);

  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
      {0,
       {{.buffer = _uniformBufferCamera[frameInFlight]->getData(),
         .offset = 0,
         .range = _uniformBufferCamera[frameInFlight]->getSize()}}},
      {4,
       {{.buffer = material->getBufferCoefficients()[frameInFlight]->getData(),
         .offset = 0,
         .range = material->getBufferCoefficients()[frameInFlight]->getSize()}}}};
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
  _descriptorSetPhong[frameInFlight]->createCustom(bufferInfoColor, textureInfoColor);
}

void Shape3D::_updatePBRDescriptor() {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialPBR>(_material);

  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor = {
      {0,
       {{.buffer = _uniformBufferCamera[frameInFlight]->getData(),
         .offset = 0,
         .range = _uniformBufferCamera[frameInFlight]->getSize()}}},
      {10,
       {{.buffer = material->getBufferCoefficients()[frameInFlight]->getData(),
         .offset = 0,
         .range = material->getBufferCoefficients()[frameInFlight]->getSize()}}},
      {11,
       {{.buffer = material->getBufferAlphaCutoff()[frameInFlight]->getData(),
         .offset = 0,
         .range = material->getBufferAlphaCutoff()[frameInFlight]->getSize()}}}};
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
  _descriptorSetPBR[frameInFlight]->createCustom(bufferInfoColor, textureInfoColor);
}

void Shape3D::enableShadow(bool enable) { _enableShadow = enable; }

void Shape3D::enableLighting(bool enable) { _enableLighting = enable; }

void Shape3D::setMaterial(std::shared_ptr<MaterialColor> material) {
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    if (_material) _material->unregisterUpdate(_descriptorSetColor[i]);
    material->registerUpdate(_descriptorSetColor[i],
                             {._frameInFlight = i, ._materialInfo = {{MaterialTexture::COLOR, 1}}});
  }
  _materialType = MaterialType::COLOR;
  _material = material;
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void Shape3D::setMaterial(std::shared_ptr<MaterialPhong> material) {
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    if (_material) _material->unregisterUpdate(_descriptorSetPhong[i]);
    material->registerUpdate(
        _descriptorSetPhong[i],
        {._frameInFlight = i,
         ._materialInfo = {{MaterialTexture::COLOR, 1}, {MaterialTexture::NORMAL, 2}, {MaterialTexture::SPECULAR, 3}}});
  }
  _materialType = MaterialType::PHONG;
  _material = material;
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void Shape3D::setMaterial(std::shared_ptr<MaterialPBR> material) {
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    if (_material) _material->unregisterUpdate(_descriptorSetPBR[i]);
    material->registerUpdate(_descriptorSetPBR[i], {._frameInFlight = i,
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
  _materialType = MaterialType::PBR;
  _material = material;
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void Shape3D::setDrawType(DrawType drawType) { _drawType = drawType; }

std::shared_ptr<Mesh3D> Shape3D::getMesh() { return _mesh; }

void Shape3D::draw(std::shared_ptr<CommandBuffer> commandBuffer) {
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

  auto drawShape3D = [&](std::shared_ptr<Pipeline> pipeline) {
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

    // light push constants
    if (pipeline->getPushConstants().find("constants") != pipeline->getPushConstants().end()) {
      FragmentPush pushConstants{.enableShadow = _enableShadow,
                                 .enableLighting = _enableLighting,
                                 .cameraPosition = _gameState->getCameraManager()->getCurrentCamera()->getEye()};
      auto info = pipeline->getPushConstants()["constants"];
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    BufferMVP cameraUBO{.model = getModel(),
                        .view = _gameState->getCameraManager()->getCurrentCamera()->getView(),
                        .projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection()};
    _uniformBufferCamera[currentFrame]->setData(&cameraUBO);

    VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getData()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

    vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), _mesh->getIndexBuffer()->getBuffer()->getData(), 0,
                         VK_INDEX_TYPE_UINT32);

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
      vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->getPipelineLayout(), 1, 1,
                              &_gameState->getLightManager()->getDSGlobalPhong()->getDescriptorSets(), 0, nullptr);
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
                              &_gameState->getLightManager()->getDSGlobalPBR()->getDescriptorSets(), 0, nullptr);
    }

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

    vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getIndexData().size()), 1, 0, 0,
                     0);
  };

  auto pipeline = _pipeline[_shapeType][_materialType];
  if (_drawType == DrawType::WIREFRAME) pipeline = _pipelineWireframe[_shapeType][_materialType];
  if (_drawType == DrawType::NORMAL) pipeline = _pipelineNormalMesh;
  if (_drawType == DrawType::TANGENT) pipeline = _pipelineTangentMesh;
  drawShape3D(pipeline);
}

void Shape3D::drawShadow(LightType lightType, int lightIndex, int face, std::shared_ptr<CommandBuffer> commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  auto pipeline = _pipelineDirectional;
  if (lightType == LightType::POINT) pipeline = _pipelinePoint;

  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());
  std::tuple<int, int> resolution = _engineState->getSettings()->getShadowMapResolution();

  // Cube Maps have been specified to follow the RenderMan specification (for whatever reason),
  // and RenderMan assumes the images' origin being in the upper left so we don't need to swap anything
  // if we swap, we need to change shader as well, so swap there. But we can't do it there because we sample from
  // cubemap and we can't just (1 - y)
  VkViewport viewport{};
  if (lightType == LightType::DIRECTIONAL) {
    viewport = {.x = 0.0f,
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

  if (pipeline->getPushConstants().find("constants") != pipeline->getPushConstants().end()) {
    if (lightType == LightType::POINT) {
      FragmentPointLightPushDepth pushConstants{
          .lightPosition = _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getPosition(),
          // light camera
          .far = static_cast<int>(_gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getFar())};
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

  BufferMVP cameraMVP{.model = getModel(), .view = view, .projection = projection};
  _cameraUBODepth[currentFrame][lightIndexTotal][face]->setData(&cameraMVP);

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getData()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), _mesh->getIndexBuffer()->getBuffer()->getData(), 0,
                       VK_INDEX_TYPE_UINT32);

  auto pipelineLayout = pipeline->getDescriptorSetLayout();
  auto depthLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                  [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                    return info.first == std::string("depth");
                                  });
  if (depthLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(
        commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(), 0, 1,
        &_descriptorSetCameraDepth[currentFrame][lightIndexTotal][face]->getDescriptorSets(), 0, nullptr);
  }

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getIndexData().size()), 1, 0, 0, 0);
}