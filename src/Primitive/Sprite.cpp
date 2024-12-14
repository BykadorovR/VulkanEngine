#include "Primitive/Sprite.h"
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

Sprite::Sprite(std::shared_ptr<CommandBuffer> commandBufferTransfer,
               std::shared_ptr<GameState> gameState,
               std::shared_ptr<EngineState> engineState) {
  setName("Sprite");
  _engineState = engineState;
  _gameState = gameState;
  auto loggerUtils = std::make_shared<LoggerUtils>(_engineState);
  auto settings = engineState->getSettings();
  // default material if model doesn't have material at all, we still have to send data to shader
  _defaultMaterialPhong = std::make_shared<MaterialPhong>(MaterialTarget::SIMPLE, commandBufferTransfer, engineState);
  _defaultMaterialPhong->setBaseColor({_gameState->getResourceManager()->getTextureOne()});
  _defaultMaterialPhong->setNormal({_gameState->getResourceManager()->getTextureZero()});
  _defaultMaterialPhong->setSpecular({_gameState->getResourceManager()->getTextureZero()});
  _defaultMaterialPBR = std::make_shared<MaterialPBR>(MaterialTarget::SIMPLE, commandBufferTransfer, engineState);
  _defaultMaterialColor = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, commandBufferTransfer, engineState);
  _material = _defaultMaterialPhong;
  _changedMaterialRender.resize(engineState->getSettings()->getMaxFramesInFlight());
  _changedMaterialShadow.resize(engineState->getSettings()->getMaxFramesInFlight());
  _mesh = std::make_shared<MeshStatic2D>(engineState);
  // 3   0
  // 2   1
  _mesh->setVertices({Vertex2D{{0.5f, 0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}},
                      Vertex2D{{0.5f, -0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
                      Vertex2D{{-0.5f, -0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
                      Vertex2D{{-0.5f, 0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 0.f}, {1.f, 0.f, 0.f, 1.f}}},
                     commandBufferTransfer);
  _mesh->setIndexes({0, 3, 2, 2, 1, 0}, commandBufferTransfer);

  _renderPass = std::make_shared<RenderPass>(_engineState->getSettings(), _engineState->getDevice());
  _renderPass->initializeGraphic();
  _renderPassDepth = std::make_shared<RenderPass>(_engineState->getSettings(), _engineState->getDevice());
  _renderPassDepth->initializeLightDepth();

  // initialize UBO
  _cameraUBOFull.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _cameraUBOFull[i] = std::make_shared<Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);

  // setup Normal
  {
    _descriptorSetLayoutNormalsMesh = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutNormalsMesh(2);
    layoutNormalsMesh[0].binding = 0;
    layoutNormalsMesh[0].descriptorCount = 1;
    layoutNormalsMesh[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutNormalsMesh[0].pImmutableSamplers = nullptr;
    layoutNormalsMesh[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutNormalsMesh[1].binding = 1;
    layoutNormalsMesh[1].descriptorCount = 1;
    layoutNormalsMesh[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutNormalsMesh[1].pImmutableSamplers = nullptr;
    layoutNormalsMesh[1].stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
    _descriptorSetLayoutNormalsMesh->createCustom(layoutNormalsMesh);

    // TODO: we can just have one buffer and put it twice to descriptor

    _descriptorSetNormalsMesh = std::make_shared<DescriptorSet>(
        engineState->getSettings()->getMaxFramesInFlight(), _descriptorSetLayoutNormalsMesh,
        engineState->getDescriptorPool(), engineState->getDevice());
    loggerUtils->setName("Descriptor set sprite normals mesh", VkObjectType::VK_OBJECT_TYPE_DESCRIPTOR_SET,
                         _descriptorSetNormalsMesh->getDescriptorSets());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoNormalsMesh;
      std::vector<VkDescriptorBufferInfo> bufferInfoVertex(1);
      // write to binding = 0 for vertex shader
      bufferInfoVertex[0].buffer = _cameraUBOFull[i]->getData();
      bufferInfoVertex[0].offset = 0;
      bufferInfoVertex[0].range = sizeof(BufferMVP);
      bufferInfoNormalsMesh[0] = bufferInfoVertex;
      // write for binding = 1 for geometry shader
      std::vector<VkDescriptorBufferInfo> bufferInfoGeometry(1);
      bufferInfoGeometry[0].buffer = _cameraUBOFull[i]->getData();
      bufferInfoGeometry[0].offset = 0;
      bufferInfoGeometry[0].range = sizeof(BufferMVP);
      bufferInfoNormalsMesh[1] = bufferInfoGeometry;
      _descriptorSetNormalsMesh->createCustom(i, bufferInfoNormalsMesh, {});
    }

    // initialize Normal (per vertex)
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/sprite/spriteNormal_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/shape/cubeNormal_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      shader->add("shaders/shape/cubeNormal_geometry.spv", VK_SHADER_STAGE_GEOMETRY_BIT);

      _pipelineNormal = std::make_shared<Pipeline>(_engineState->getSettings(), _engineState->getDevice());
      _pipelineNormal->createGraphic2D(
          VK_CULL_MODE_BACK_BIT, VK_POLYGON_MODE_FILL, true,
          {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
           shader->getShaderStageInfo(VK_SHADER_STAGE_GEOMETRY_BIT),
           shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
          {{"normal", _descriptorSetLayoutNormalsMesh}}, {}, _mesh->getBindingDescription(),
          _mesh->Mesh::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
                                                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, normal)},
                                                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, color)}}),
          _renderPass);
    }

    // initialize Tangent (per vertex)
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/sprite/spriteTangent_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/shape/cubeNormal_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      shader->add("shaders/shape/cubeNormal_geometry.spv", VK_SHADER_STAGE_GEOMETRY_BIT);

      _pipelineTangent = std::make_shared<Pipeline>(_engineState->getSettings(), _engineState->getDevice());
      _pipelineTangent->createGraphic2D(
          VK_CULL_MODE_BACK_BIT, VK_POLYGON_MODE_FILL, true,
          {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
           shader->getShaderStageInfo(VK_SHADER_STAGE_GEOMETRY_BIT),
           shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
          {{"normal", _descriptorSetLayoutNormalsMesh}}, {}, _mesh->getBindingDescription(),
          _mesh->Mesh::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
                                                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, color)},
                                                 {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex2D, tangent)}}),
          _renderPass);
    }
  }

  // setup color
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutColor(2);
    layoutColor[0].binding = 0;
    layoutColor[0].descriptorCount = 1;
    layoutColor[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutColor[0].pImmutableSamplers = nullptr;
    layoutColor[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutColor[1].binding = 1;
    layoutColor[1].descriptorCount = 1;
    layoutColor[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutColor[1].pImmutableSamplers = nullptr;
    layoutColor[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptorSetLayout->createCustom(layoutColor);
    _descriptorSetLayout[MaterialType::COLOR].push_back({"color", descriptorSetLayout});
    // depth has the same layout
    _descriptorSetLayoutDepth = descriptorSetLayout;

    _descriptorSetColor = std::make_shared<DescriptorSet>(engineState->getSettings()->getMaxFramesInFlight(),
                                                          descriptorSetLayout, engineState->getDescriptorPool(),
                                                          engineState->getDevice());
    loggerUtils->setName("Descriptor set sprite color", VkObjectType::VK_OBJECT_TYPE_DESCRIPTOR_SET,
                         _descriptorSetColor->getDescriptorSets());

    // phong is default, will form default descriptor only for it here
    // descriptors are formed in setMaterial

    // initialize Color
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/sprite/spriteColor_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/sprite/spriteColor_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

      _pipeline[MaterialType::COLOR] = std::make_shared<Pipeline>(_engineState->getSettings(),
                                                                  _engineState->getDevice());
      _pipeline[MaterialType::COLOR]->createGraphic2D(
          VK_CULL_MODE_BACK_BIT, VK_POLYGON_MODE_FILL, true,
          {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
           shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
          _descriptorSetLayout[MaterialType::COLOR], {}, _mesh->getBindingDescription(),
          _mesh->Mesh::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
                                                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, color)},
                                                 {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}}),

          _renderPass);
      // wireframe one
      _pipelineWireframe[MaterialType::COLOR] = std::make_shared<Pipeline>(_engineState->getSettings(),
                                                                           _engineState->getDevice());
      _pipelineWireframe[MaterialType::COLOR]->createGraphic2D(
          VK_CULL_MODE_BACK_BIT, VK_POLYGON_MODE_LINE, false,
          {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
           shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
          _descriptorSetLayout[MaterialType::COLOR], {}, _mesh->getBindingDescription(),
          _mesh->Mesh::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
                                                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, color)},
                                                 {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}}),
          _renderPass);
    }
  }

  // initialize camera UBO and descriptor sets for shadow
  // initialize UBO
  for (int i = 0; i < _engineState->getSettings()->getMaxDirectionalLights(); i++) {
    std::vector<std::shared_ptr<Buffer>> buffer(_engineState->getSettings()->getMaxFramesInFlight());
    for (int j = 0; j < _engineState->getSettings()->getMaxFramesInFlight(); j++)
      buffer[j] = std::make_shared<Buffer>(sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                           _engineState);
    _cameraUBODepth.push_back({buffer});
    auto cameraSet = std::make_shared<DescriptorSet>(_engineState->getSettings()->getMaxFramesInFlight(),
                                                     _descriptorSetLayoutDepth, _engineState->getDescriptorPool(),
                                                     _engineState->getDevice());
    loggerUtils->setName("Descriptor set sprite directional camera " + std::to_string(i),
                         VkObjectType::VK_OBJECT_TYPE_DESCRIPTOR_SET, cameraSet->getDescriptorSets());
    _descriptorSetCameraDepth.push_back({cameraSet});
  }

  for (int i = 0; i < _engineState->getSettings()->getMaxPointLights(); i++) {
    std::vector<std::vector<std::shared_ptr<Buffer>>> facesBuffer(6);
    std::vector<std::shared_ptr<DescriptorSet>> facesSet(6);
    for (int j = 0; j < 6; j++) {
      facesBuffer[j].resize(_engineState->getSettings()->getMaxFramesInFlight());
      for (int k = 0; k < _engineState->getSettings()->getMaxFramesInFlight(); k++)
        facesBuffer[j][k] = std::make_shared<Buffer>(
            sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);
      facesSet[j] = std::make_shared<DescriptorSet>(_engineState->getSettings()->getMaxFramesInFlight(),
                                                    _descriptorSetLayoutDepth, _engineState->getDescriptorPool(),
                                                    _engineState->getDevice());
      loggerUtils->setName("Descriptor set sprite point camera " + std::to_string(i) + "x" + std::to_string(j),
                           VkObjectType::VK_OBJECT_TYPE_DESCRIPTOR_SET, facesSet[j]->getDescriptorSets());
    }
    _cameraUBODepth.push_back(facesBuffer);
    _descriptorSetCameraDepth.push_back(facesSet);
  }

  // setup Phong
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutPhong(5);
    layoutPhong[0].binding = 0;
    layoutPhong[0].descriptorCount = 1;
    layoutPhong[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutPhong[0].pImmutableSamplers = nullptr;
    layoutPhong[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutPhong[1].binding = 1;
    layoutPhong[1].descriptorCount = 1;
    layoutPhong[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutPhong[1].pImmutableSamplers = nullptr;
    layoutPhong[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutPhong[2].binding = 2;
    layoutPhong[2].descriptorCount = 1;
    layoutPhong[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutPhong[2].pImmutableSamplers = nullptr;
    layoutPhong[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutPhong[3].binding = 3;
    layoutPhong[3].descriptorCount = 1;
    layoutPhong[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutPhong[3].pImmutableSamplers = nullptr;
    layoutPhong[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutPhong[4].binding = 4;
    layoutPhong[4].descriptorCount = 1;
    layoutPhong[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutPhong[4].pImmutableSamplers = nullptr;
    layoutPhong[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptorSetLayout->createCustom(layoutPhong);
    _descriptorSetLayout[MaterialType::PHONG].push_back({"phong", descriptorSetLayout});
    _descriptorSetLayout[MaterialType::PHONG].push_back(
        {"globalPhong", _gameState->getLightManager()->getDSLGlobalPhong()});

    _descriptorSetPhong = std::make_shared<DescriptorSet>(engineState->getSettings()->getMaxFramesInFlight(),
                                                          descriptorSetLayout, engineState->getDescriptorPool(),
                                                          engineState->getDevice());
    loggerUtils->setName("Descriptor set sprite Phong", VkObjectType::VK_OBJECT_TYPE_DESCRIPTOR_SET,
                         _descriptorSetPhong->getDescriptorSets());

    setMaterial(_defaultMaterialPhong);

    // initialize Phong
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/sprite/spritePhong_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/sprite/spritePhong_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

      _pipeline[MaterialType::PHONG] = std::make_shared<Pipeline>(_engineState->getSettings(),
                                                                  _engineState->getDevice());
      _pipeline[MaterialType::PHONG]->createGraphic2D(
          VK_CULL_MODE_BACK_BIT, VK_POLYGON_MODE_FILL, true,
          {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
           shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
          _descriptorSetLayout[MaterialType::PHONG],
          std::map<std::string, VkPushConstantRange>{
              {std::string("constants"), VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                             .offset = 0,
                                                             .size = sizeof(FragmentPush)}}},
          _mesh->getBindingDescription(), _mesh->getAttributeDescriptions(), _renderPass);
      // wireframe one
      _pipelineWireframe[MaterialType::PHONG] = std::make_shared<Pipeline>(_engineState->getSettings(),
                                                                           _engineState->getDevice());
      _pipelineWireframe[MaterialType::PHONG]->createGraphic2D(
          VK_CULL_MODE_BACK_BIT, VK_POLYGON_MODE_LINE, false,
          {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
           shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
          _descriptorSetLayout[MaterialType::PHONG],
          std::map<std::string, VkPushConstantRange>{
              {std::string("constants"), VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                             .offset = 0,
                                                             .size = sizeof(FragmentPush)}}},
          _mesh->getBindingDescription(), _mesh->getAttributeDescriptions(), _renderPass);
    }
  }

  // setup PBR
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutPBR(11);
    layoutPBR[0].binding = 0;
    layoutPBR[0].descriptorCount = 1;
    layoutPBR[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutPBR[0].pImmutableSamplers = nullptr;
    layoutPBR[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutPBR[1].binding = 1;
    layoutPBR[1].descriptorCount = 1;
    layoutPBR[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutPBR[1].pImmutableSamplers = nullptr;
    layoutPBR[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutPBR[2].binding = 2;
    layoutPBR[2].descriptorCount = 1;
    layoutPBR[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutPBR[2].pImmutableSamplers = nullptr;
    layoutPBR[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutPBR[3].binding = 3;
    layoutPBR[3].descriptorCount = 1;
    layoutPBR[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutPBR[3].pImmutableSamplers = nullptr;
    layoutPBR[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutPBR[4].binding = 4;
    layoutPBR[4].descriptorCount = 1;
    layoutPBR[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutPBR[4].pImmutableSamplers = nullptr;
    layoutPBR[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutPBR[5].binding = 5;
    layoutPBR[5].descriptorCount = 1;
    layoutPBR[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutPBR[5].pImmutableSamplers = nullptr;
    layoutPBR[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutPBR[6].binding = 6;
    layoutPBR[6].descriptorCount = 1;
    layoutPBR[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutPBR[6].pImmutableSamplers = nullptr;
    layoutPBR[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutPBR[7].binding = 7;
    layoutPBR[7].descriptorCount = 1;
    layoutPBR[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutPBR[7].pImmutableSamplers = nullptr;
    layoutPBR[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutPBR[8].binding = 8;
    layoutPBR[8].descriptorCount = 1;
    layoutPBR[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutPBR[8].pImmutableSamplers = nullptr;
    layoutPBR[8].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutPBR[9].binding = 9;
    layoutPBR[9].descriptorCount = 1;
    layoutPBR[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutPBR[9].pImmutableSamplers = nullptr;
    layoutPBR[9].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutPBR[10].binding = 10;
    layoutPBR[10].descriptorCount = 1;
    layoutPBR[10].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutPBR[10].pImmutableSamplers = nullptr;
    layoutPBR[10].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptorSetLayout->createCustom(layoutPBR);
    _descriptorSetLayout[MaterialType::PBR].push_back({"pbr", descriptorSetLayout});
    _descriptorSetLayout[MaterialType::PBR].push_back({"globalPBR", _gameState->getLightManager()->getDSLGlobalPBR()});

    _descriptorSetPBR = std::make_shared<DescriptorSet>(engineState->getSettings()->getMaxFramesInFlight(),
                                                        descriptorSetLayout, engineState->getDescriptorPool(),
                                                        engineState->getDevice());
    loggerUtils->setName("Descriptor set sprite PBR", VkObjectType::VK_OBJECT_TYPE_DESCRIPTOR_SET,
                         _descriptorSetPBR->getDescriptorSets());
    // phong is default, will form default descriptor only for it here
    // descriptors are formed in setMaterial

    // initialize PBR
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/sprite/spritePBR_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/sprite/spritePBR_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

      _pipeline[MaterialType::PBR] = std::make_shared<Pipeline>(_engineState->getSettings(), _engineState->getDevice());
      _pipeline[MaterialType::PBR]->createGraphic2D(
          VK_CULL_MODE_BACK_BIT, VK_POLYGON_MODE_FILL, true,
          {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
           shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
          _descriptorSetLayout[MaterialType::PBR],
          std::map<std::string, VkPushConstantRange>{
              {std::string("constants"), VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                             .offset = 0,
                                                             .size = sizeof(FragmentPush)}}},
          _mesh->getBindingDescription(), _mesh->getAttributeDescriptions(), _renderPass);
      // wireframe one
      _pipelineWireframe[MaterialType::PBR] = std::make_shared<Pipeline>(_engineState->getSettings(),
                                                                         _engineState->getDevice());
      _pipelineWireframe[MaterialType::PBR]->createGraphic2D(
          VK_CULL_MODE_BACK_BIT, VK_POLYGON_MODE_LINE, false,
          {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
           shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
          _descriptorSetLayout[MaterialType::PBR],
          std::map<std::string, VkPushConstantRange>{
              {std::string("constants"), VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                             .offset = 0,
                                                             .size = sizeof(FragmentPush)}}},
          _mesh->getBindingDescription(), _mesh->getAttributeDescriptions(), _renderPass);
    }
  }

  // initialize depth directional
  {
    auto shader = std::make_shared<Shader>(_engineState);
    shader->add("shaders/sprite/spriteDepth_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
    shader->add("shaders/sprite/spriteDepthDirectional_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    _pipelineDirectional = std::make_shared<Pipeline>(_engineState->getSettings(), _engineState->getDevice());
    _pipelineDirectional->createGraphic2DShadow(
        VK_CULL_MODE_NONE,
        {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
         shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
        {{"depth", _descriptorSetLayoutDepth}}, {}, _mesh->getBindingDescription(),
        _mesh->Mesh::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
                                               {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, color)},
                                               {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}}),
        _renderPassDepth);
  }

  // initialize depth point
  {
    std::map<std::string, VkPushConstantRange> defaultPushConstants;
    defaultPushConstants["constants"] = VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(FragmentPointLightPushDepth),
    };

    auto shader = std::make_shared<Shader>(_engineState);
    shader->add("shaders/sprite/spriteDepth_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
    shader->add("shaders/sprite/spriteDepthPoint_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    _pipelinePoint = std::make_shared<Pipeline>(_engineState->getSettings(), _engineState->getDevice());
    _pipelinePoint->createGraphic2DShadow(
        VK_CULL_MODE_NONE,
        {shader->getShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT),
         shader->getShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT)},
        {{"depth", _descriptorSetLayoutDepth}}, defaultPushConstants, _mesh->getBindingDescription(),
        _mesh->Mesh::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
                                               {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, color)},
                                               {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}}),
        _renderPassDepth);
  }
}

void Sprite::_updateColorDescriptor() {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialColor>(_material);
  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor;
  std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor;
  std::vector<VkDescriptorBufferInfo> bufferInfoCamera(1);
  // write to binding = 0 for vertex shader
  bufferInfoCamera[0].buffer = _cameraUBOFull[currentFrame]->getData();
  bufferInfoCamera[0].offset = 0;
  bufferInfoCamera[0].range = sizeof(BufferMVP);
  bufferInfoColor[0] = bufferInfoCamera;

  // write for binding = 1 for textures
  std::vector<VkDescriptorImageInfo> bufferInfoTexture(1);
  bufferInfoTexture[0].imageLayout = material->getBaseColor()[0]->getImageView()->getImage()->getImageLayout();
  bufferInfoTexture[0].imageView = material->getBaseColor()[0]->getImageView()->getImageView();
  bufferInfoTexture[0].sampler = material->getBaseColor()[0]->getSampler()->getSampler();
  textureInfoColor[1] = bufferInfoTexture;
  _descriptorSetColor->createCustom(currentFrame, bufferInfoColor, textureInfoColor);
}

void Sprite::_updatePhongDescriptor() {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialPhong>(_material);
  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor;
  std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor;
  std::vector<VkDescriptorBufferInfo> bufferInfoCamera(1);
  // write to binding = 0 for vertex shader
  bufferInfoCamera[0].buffer = _cameraUBOFull[currentFrame]->getData();
  bufferInfoCamera[0].offset = 0;
  bufferInfoCamera[0].range = sizeof(BufferMVP);
  bufferInfoColor[0] = bufferInfoCamera;

  // write for binding = 1 for textures
  std::vector<VkDescriptorImageInfo> bufferInfoBaseColor(1);
  bufferInfoBaseColor[0].imageLayout = material->getBaseColor()[0]->getImageView()->getImage()->getImageLayout();
  bufferInfoBaseColor[0].imageView = material->getBaseColor()[0]->getImageView()->getImageView();
  bufferInfoBaseColor[0].sampler = material->getBaseColor()[0]->getSampler()->getSampler();
  textureInfoColor[1] = bufferInfoBaseColor;

  std::vector<VkDescriptorImageInfo> bufferInfoNormal(1);
  bufferInfoNormal[0].imageLayout = material->getNormal()[0]->getImageView()->getImage()->getImageLayout();
  bufferInfoNormal[0].imageView = material->getNormal()[0]->getImageView()->getImageView();
  bufferInfoNormal[0].sampler = material->getNormal()[0]->getSampler()->getSampler();
  textureInfoColor[2] = bufferInfoNormal;

  std::vector<VkDescriptorImageInfo> bufferInfoSpecular(1);
  bufferInfoSpecular[0].imageLayout = material->getSpecular()[0]->getImageView()->getImage()->getImageLayout();
  bufferInfoSpecular[0].imageView = material->getSpecular()[0]->getImageView()->getImageView();
  bufferInfoSpecular[0].sampler = material->getSpecular()[0]->getSampler()->getSampler();
  textureInfoColor[3] = bufferInfoSpecular;

  std::vector<VkDescriptorBufferInfo> bufferInfoCoefficients(1);
  // write to binding = 0 for vertex shader
  bufferInfoCoefficients[0].buffer = material->getBufferCoefficients()[currentFrame]->getData();
  bufferInfoCoefficients[0].offset = 0;
  bufferInfoCoefficients[0].range = material->getBufferCoefficients()[currentFrame]->getSize();
  bufferInfoColor[4] = bufferInfoCoefficients;
  _descriptorSetPhong->createCustom(currentFrame, bufferInfoColor, textureInfoColor);
}

void Sprite::_updatePBRDescriptor() {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialPBR>(_material);
  std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor;
  std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor;
  std::vector<VkDescriptorBufferInfo> bufferInfoCamera(1);
  // write to binding = 0 for vertex shader
  bufferInfoCamera[0].buffer = _cameraUBOFull[currentFrame]->getData();
  bufferInfoCamera[0].offset = 0;
  bufferInfoCamera[0].range = sizeof(BufferMVP);
  bufferInfoColor[0] = bufferInfoCamera;

  // write for binding = 1 for textures
  std::vector<VkDescriptorImageInfo> bufferInfoBaseColor(1);
  bufferInfoBaseColor[0].imageLayout = material->getBaseColor()[0]->getImageView()->getImage()->getImageLayout();
  bufferInfoBaseColor[0].imageView = material->getBaseColor()[0]->getImageView()->getImageView();
  bufferInfoBaseColor[0].sampler = material->getBaseColor()[0]->getSampler()->getSampler();
  textureInfoColor[1] = bufferInfoBaseColor;

  std::vector<VkDescriptorImageInfo> bufferInfoNormal(1);
  bufferInfoNormal[0].imageLayout = material->getNormal()[0]->getImageView()->getImage()->getImageLayout();
  bufferInfoNormal[0].imageView = material->getNormal()[0]->getImageView()->getImageView();
  bufferInfoNormal[0].sampler = material->getNormal()[0]->getSampler()->getSampler();
  textureInfoColor[2] = bufferInfoNormal;

  std::vector<VkDescriptorImageInfo> bufferInfoMetallic(1);
  bufferInfoMetallic[0].imageLayout = material->getMetallic()[0]->getImageView()->getImage()->getImageLayout();
  bufferInfoMetallic[0].imageView = material->getMetallic()[0]->getImageView()->getImageView();
  bufferInfoMetallic[0].sampler = material->getMetallic()[0]->getSampler()->getSampler();
  textureInfoColor[3] = bufferInfoMetallic;

  std::vector<VkDescriptorImageInfo> bufferInfoRoughness(1);
  bufferInfoRoughness[0].imageLayout = material->getRoughness()[0]->getImageView()->getImage()->getImageLayout();
  bufferInfoRoughness[0].imageView = material->getRoughness()[0]->getImageView()->getImageView();
  bufferInfoRoughness[0].sampler = material->getRoughness()[0]->getSampler()->getSampler();
  textureInfoColor[4] = bufferInfoRoughness;

  std::vector<VkDescriptorImageInfo> bufferInfoOcclusion(1);
  bufferInfoOcclusion[0].imageLayout = material->getOccluded()[0]->getImageView()->getImage()->getImageLayout();
  bufferInfoOcclusion[0].imageView = material->getOccluded()[0]->getImageView()->getImageView();
  bufferInfoOcclusion[0].sampler = material->getOccluded()[0]->getSampler()->getSampler();
  textureInfoColor[5] = bufferInfoOcclusion;

  std::vector<VkDescriptorImageInfo> bufferInfoEmissive(1);
  bufferInfoEmissive[0].imageLayout = material->getEmissive()[0]->getImageView()->getImage()->getImageLayout();
  bufferInfoEmissive[0].imageView = material->getEmissive()[0]->getImageView()->getImageView();
  bufferInfoEmissive[0].sampler = material->getEmissive()[0]->getSampler()->getSampler();
  textureInfoColor[6] = bufferInfoEmissive;

  // TODO: this textures are part of global engineState for PBR
  std::vector<VkDescriptorImageInfo> bufferInfoIrradiance(1);
  bufferInfoIrradiance[0].imageLayout = material->getDiffuseIBL()->getImageView()->getImage()->getImageLayout();
  bufferInfoIrradiance[0].imageView = material->getDiffuseIBL()->getImageView()->getImageView();
  bufferInfoIrradiance[0].sampler = material->getDiffuseIBL()->getSampler()->getSampler();
  textureInfoColor[7] = bufferInfoIrradiance;

  std::vector<VkDescriptorImageInfo> bufferInfoSpecularIBL(1);
  bufferInfoSpecularIBL[0].imageLayout = material->getSpecularIBL()->getImageView()->getImage()->getImageLayout();
  bufferInfoSpecularIBL[0].imageView = material->getSpecularIBL()->getImageView()->getImageView();
  bufferInfoSpecularIBL[0].sampler = material->getSpecularIBL()->getSampler()->getSampler();
  textureInfoColor[8] = bufferInfoSpecularIBL;

  std::vector<VkDescriptorImageInfo> bufferInfoSpecularBRDF(1);
  bufferInfoSpecularBRDF[0].imageLayout = material->getSpecularBRDF()->getImageView()->getImage()->getImageLayout();
  bufferInfoSpecularBRDF[0].imageView = material->getSpecularBRDF()->getImageView()->getImageView();
  bufferInfoSpecularBRDF[0].sampler = material->getSpecularBRDF()->getSampler()->getSampler();
  textureInfoColor[9] = bufferInfoSpecularBRDF;

  std::vector<VkDescriptorBufferInfo> bufferInfoCoefficients(1);
  // write to binding = 10 for coefficients
  bufferInfoCoefficients[0].buffer = material->getBufferCoefficients()[currentFrame]->getData();
  bufferInfoCoefficients[0].offset = 0;
  bufferInfoCoefficients[0].range = material->getBufferCoefficients()[currentFrame]->getSize();
  bufferInfoColor[10] = bufferInfoCoefficients;

  _descriptorSetPBR->createCustom(currentFrame, bufferInfoColor, textureInfoColor);
}

void Sprite::setMaterial(std::shared_ptr<MaterialPBR> material) {
  if (_material) _material->unregisterUpdate(_descriptorSetPBR);
  material->registerUpdate(_descriptorSetPBR, {{MaterialTexture::COLOR, 1},
                                               {MaterialTexture::NORMAL, 2},
                                               {MaterialTexture::METALLIC, 3},
                                               {MaterialTexture::ROUGHNESS, 4},
                                               {MaterialTexture::OCCLUSION, 5},
                                               {MaterialTexture::EMISSIVE, 6},
                                               {MaterialTexture::IBL_DIFFUSE, 7},
                                               {MaterialTexture::IBL_SPECULAR, 8},
                                               {MaterialTexture::BRDF_SPECULAR, 9}});
  for (int d = 0; d < _engineState->getSettings()->getMaxDirectionalLights(); d++) {
    if (_material) _material->unregisterUpdate(_descriptorSetCameraDepth[d][0]);
    material->registerUpdate(_descriptorSetCameraDepth[d][0], {{MaterialTexture::COLOR, 1}});
  }
  for (int p = 0; p < _engineState->getSettings()->getMaxPointLights(); p++) {
    for (int f = 0; f < 6; f++) {
      if (_material)
        _material->unregisterUpdate(
            _descriptorSetCameraDepth[_engineState->getSettings()->getMaxDirectionalLights() + p][f]);
      material->registerUpdate(_descriptorSetCameraDepth[_engineState->getSettings()->getMaxDirectionalLights() + p][f],
                               {{MaterialTexture::COLOR, 1}});
    }
  }
  _materialType = MaterialType::PBR;
  _material = material;
  for (int i = 0; i < _changedMaterialRender.size(); i++) {
    _changedMaterialRender[i] = true;
    _changedMaterialShadow[i] = true;
  }
}

void Sprite::setMaterial(std::shared_ptr<MaterialPhong> material) {
  if (_material) _material->unregisterUpdate(_descriptorSetPhong);
  material->registerUpdate(_descriptorSetPhong,
                           {{MaterialTexture::COLOR, 1}, {MaterialTexture::NORMAL, 2}, {MaterialTexture::SPECULAR, 3}});
  for (int d = 0; d < _engineState->getSettings()->getMaxDirectionalLights(); d++) {
    if (_material) _material->unregisterUpdate(_descriptorSetCameraDepth[d][0]);
    material->registerUpdate(_descriptorSetCameraDepth[d][0], {{MaterialTexture::COLOR, 1}});
  }
  for (int p = 0; p < _engineState->getSettings()->getMaxPointLights(); p++) {
    for (int f = 0; f < 6; f++) {
      if (_material)
        _material->unregisterUpdate(
            _descriptorSetCameraDepth[_engineState->getSettings()->getMaxDirectionalLights() + p][f]);
      material->registerUpdate(_descriptorSetCameraDepth[_engineState->getSettings()->getMaxDirectionalLights() + p][f],
                               {{MaterialTexture::COLOR, 1}});
    }
  }
  _materialType = MaterialType::PHONG;
  _material = material;
  for (int i = 0; i < _changedMaterialRender.size(); i++) {
    _changedMaterialRender[i] = true;
    _changedMaterialShadow[i] = true;
  }
}

void Sprite::setMaterial(std::shared_ptr<MaterialColor> material) {
  if (_material) _material->unregisterUpdate(_descriptorSetColor);
  material->registerUpdate(_descriptorSetColor, {{MaterialTexture::COLOR, 1}});
  for (int d = 0; d < _engineState->getSettings()->getMaxDirectionalLights(); d++) {
    if (_material) _material->unregisterUpdate(_descriptorSetCameraDepth[d][0]);
    material->registerUpdate(_descriptorSetCameraDepth[d][0], {{MaterialTexture::COLOR, 1}});
  }
  for (int p = 0; p < _engineState->getSettings()->getMaxPointLights(); p++) {
    for (int f = 0; f < 6; f++) {
      if (_material)
        _material->unregisterUpdate(
            _descriptorSetCameraDepth[_engineState->getSettings()->getMaxDirectionalLights() + p][f]);
      material->registerUpdate(_descriptorSetCameraDepth[_engineState->getSettings()->getMaxDirectionalLights() + p][f],
                               {{MaterialTexture::COLOR, 1}});
    }
  }

  _materialType = MaterialType::COLOR;
  _material = material;
  for (int i = 0; i < _changedMaterialRender.size(); i++) {
    _changedMaterialRender[i] = true;
    _changedMaterialShadow[i] = true;
  }
}

MaterialType Sprite::getMaterialType() { return _materialType; }

void Sprite::enableDepth(bool enable) { _enableDepth = enable; }

void Sprite::enableHUD(bool enable) { _enableHUD = enable; }

bool Sprite::isDepthEnabled() { return _enableDepth; }

void Sprite::enableShadow(bool enable) { _enableShadow = enable; }

void Sprite::enableLighting(bool enable) { _enableLighting = enable; }

void Sprite::setDrawType(DrawType drawType) { _drawType = drawType; }

DrawType Sprite::getDrawType() { return _drawType; }

void Sprite::draw(std::shared_ptr<CommandBuffer> commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  if (_changedMaterialRender[currentFrame]) {
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
    _changedMaterialRender[currentFrame] = false;
  }

  auto pipeline = _pipeline[_materialType];
  if (_drawType == DrawType::WIREFRAME) pipeline = _pipelineWireframe[_materialType];
  if (_drawType == DrawType::NORMAL) pipeline = _pipelineNormal;
  if (_drawType == DrawType::TANGENT) pipeline = _pipelineTangent;

  auto resolution = _engineState->getSettings()->getResolution();
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = std::get<1>(resolution);
  viewport.width = std::get<0>(resolution);
  viewport.height = -std::get<1>(resolution);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer->getCommandBuffer()[currentFrame], 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = VkExtent2D(std::get<0>(resolution), std::get<1>(resolution));
  vkCmdSetScissor(commandBuffer->getCommandBuffer()[currentFrame], 0, 1, &scissor);

  if (pipeline->getPushConstants().find("constants") != pipeline->getPushConstants().end()) {
    FragmentPush pushConstants;
    pushConstants.enableShadow = _enableShadow;
    pushConstants.enableLighting = _enableLighting;
    pushConstants.cameraPosition = _gameState->getCameraManager()->getCurrentCamera()->getEye();
    auto info = pipeline->getPushConstants()["constants"];
    vkCmdPushConstants(commandBuffer->getCommandBuffer()[currentFrame], pipeline->getPipelineLayout(), info.stageFlags,
                       info.offset, info.size, &pushConstants);
  }

  BufferMVP cameraMVP{};
  cameraMVP.model = getModel();
  cameraMVP.view = glm::mat4(1.f);
  cameraMVP.projection = glm::mat4(1.f);
  if (_enableHUD == false) {
    cameraMVP.view = _gameState->getCameraManager()->getCurrentCamera()->getView();
    cameraMVP.projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection();
  }

  _cameraUBOFull[currentFrame]->setData(&cameraMVP);

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getData()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer()[currentFrame], 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer()[currentFrame], _mesh->getIndexBuffer()->getBuffer()->getData(),
                       0, VK_INDEX_TYPE_UINT32);

  vkCmdBindPipeline(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline->getPipeline());

  // color
  auto pipelineLayout = pipeline->getDescriptorSetLayout();
  auto colorLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                  [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                    return info.first == std::string("color");
                                  });
  if (colorLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->getPipelineLayout(), 0, 1,
                            &_descriptorSetColor->getDescriptorSets()[currentFrame], 0, nullptr);
  }

  // Phong
  auto phongLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                  [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                    return info.first == std::string("phong");
                                  });
  if (phongLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->getPipelineLayout(), 0, 1,
                            &_descriptorSetPhong->getDescriptorSets()[currentFrame], 0, nullptr);
  }

  // global Phong
  auto globalLayoutPhong = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                        [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                          return info.first == std::string("globalPhong");
                                        });
  if (globalLayoutPhong != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(
        commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(),
        1, 1, &_gameState->getLightManager()->getDSGlobalPhong()->getDescriptorSets()[currentFrame], 0, nullptr);
  }

  // PBR
  auto pbrLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                  return info.first == std::string("pbr");
                                });
  if (pbrLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->getPipelineLayout(), 0, 1, &_descriptorSetPBR->getDescriptorSets()[currentFrame],
                            0, nullptr);
  }

  // global PBR
  auto globalLayoutPBR = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                      [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                        return info.first == std::string("globalPBR");
                                      });
  if (globalLayoutPBR != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(
        commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(),
        1, 1, &_gameState->getLightManager()->getDSGlobalPBR()->getDescriptorSets()[currentFrame], 0, nullptr);
  }

  // normals and tangents
  auto normalTangentLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                          [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                            return info.first == std::string("normal");
                                          });
  if (normalTangentLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->getPipelineLayout(), 0, 1,
                            &_descriptorSetNormalsMesh->getDescriptorSets()[currentFrame], 0, nullptr);
  }

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer()[currentFrame], static_cast<uint32_t>(_mesh->getIndexData().size()),
                   1, 0, 0, 0);
}

void Sprite::drawShadow(LightType lightType, int lightIndex, int face, std::shared_ptr<CommandBuffer> commandBuffer) {
  if (_enableDepth == false) return;

  int currentFrame = _engineState->getFrameInFlight();
  {
    // we have to update only once during the first drawShadow call
    // (for point lights this function is called multiple times in different threads)
    std::unique_lock<std::mutex> lock(_updateShadow);
    if (_changedMaterialShadow[currentFrame]) {
      switch (_materialType) {
        case MaterialType::COLOR:
          _updateShadowDescriptor(std::dynamic_pointer_cast<MaterialColor>(_material));
          break;
        case MaterialType::PHONG:
          _updateShadowDescriptor(std::dynamic_pointer_cast<MaterialPhong>(_material));
          break;
        case MaterialType::PBR:
          _updateShadowDescriptor(std::dynamic_pointer_cast<MaterialPBR>(_material));
          break;
      }
      _changedMaterialShadow[currentFrame] = false;
    }
  }

  auto pipeline = _pipelineDirectional;
  if (lightType == LightType::POINT) pipeline = _pipelinePoint;

  vkCmdBindPipeline(commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline->getPipeline());
  std::tuple<int, int> resolution = _engineState->getSettings()->getShadowMapResolution();
  // Cube Maps have been specified to follow the RenderMan specification (for whatever reason),
  // and RenderMan assumes the images' origin being in the upper left so we don't need to swap anything
  // if we swap, we need to change shader as well, so swap there. But we can't do it there because we sample from
  // cubemap and we can't just (1 - y)
  VkViewport viewport{};
  if (lightType == LightType::DIRECTIONAL) {
    viewport.x = 0.0f;
    viewport.y = std::get<1>(resolution);
    viewport.width = std::get<0>(resolution);
    viewport.height = -std::get<1>(resolution);
  } else if (lightType == LightType::POINT) {
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = std::get<0>(resolution);
    viewport.height = std::get<1>(resolution);
  }

  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer->getCommandBuffer()[currentFrame], 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = VkExtent2D(std::get<0>(resolution), std::get<1>(resolution));
  vkCmdSetScissor(commandBuffer->getCommandBuffer()[currentFrame], 0, 1, &scissor);

  if (pipeline->getPushConstants().find("constants") != pipeline->getPushConstants().end()) {
    if (lightType == LightType::POINT) {
      FragmentPointLightPushDepth pushConstants;
      pushConstants.lightPosition =
          _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getPosition();
      // light camera
      pushConstants.far = _gameState->getLightManager()->getPointLights()[lightIndex]->getCamera()->getFar();
      auto info = pipeline->getPushConstants()["constants"];
      vkCmdPushConstants(commandBuffer->getCommandBuffer()[currentFrame], pipeline->getPipelineLayout(),
                         info.stageFlags, info.offset, info.size, &pushConstants);
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

  BufferMVP cameraMVP{};
  cameraMVP.model = getModel();
  cameraMVP.view = view;
  cameraMVP.projection = projection;

  _cameraUBODepth[lightIndexTotal][face][currentFrame]->setData(&cameraMVP);

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getData()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer()[currentFrame], 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer()[currentFrame], _mesh->getIndexBuffer()->getBuffer()->getData(),
                       0, VK_INDEX_TYPE_UINT32);

  auto pipelineLayout = pipeline->getDescriptorSetLayout();
  auto depthLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                  [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                    return info.first == std::string("depth");
                                  });
  if (depthLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(
        commandBuffer->getCommandBuffer()[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(),
        0, 1, &_descriptorSetCameraDepth[lightIndexTotal][face]->getDescriptorSets()[currentFrame], 0, nullptr);
  }

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer()[currentFrame], static_cast<uint32_t>(_mesh->getIndexData().size()),
                   1, 0, 0, 0);
}