#include <cstring>
#include <chrono>
#include <optional>
#include "Primitive/Sprite.h"

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

Sprite::Sprite(const RenderGraph::CommandBuffer* commandBufferTransfer,
               std::shared_ptr<GameState> gameState,
               std::shared_ptr<EngineState> engineState) {
  setName("Sprite");
  _engineState = engineState;
  _gameState = gameState;
  auto settings = engineState->getSettings();
  // default material if model doesn't have material at all, we still have to send data to shader
  _defaultMaterialPhong = std::make_shared<MaterialPhong>(MaterialTarget::SIMPLE, engineState);
  _defaultMaterialPhong->setBaseColor({_gameState->getResourceManager()->getTextureOne()});
  _defaultMaterialPhong->setNormal({_gameState->getResourceManager()->getTextureZero()});
  _defaultMaterialPhong->setSpecular({_gameState->getResourceManager()->getTextureZero()});
  _material = _defaultMaterialPhong;
  _changedMaterialRender.resize(engineState->getSettings()->getMaxFramesInFlight());
  _changedMaterialShadow.resize(engineState->getSettings()->getMaxFramesInFlight());
  _mesh = std::make_shared<MeshStatic2D>(engineState);
  // 3   0
  // 2   1
  _mesh->setVertices(
      {Vertex2D{{0.5f, 0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}},
       Vertex2D{{0.5f, -0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
       Vertex2D{{-0.5f, -0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
       Vertex2D{{-0.5f, 0.5f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}, {0.f, 0.f}, {1.f, 0.f, 0.f, 1.f}}});
  _mesh->setIndexes({0, 3, 2, 2, 1, 0});
  const auto vertexBinding = _mesh->getBindingDescription();
  const auto normalVertexAttributes = _mesh->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
       {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, normal)},
       {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, color)}});
  const VkPipelineVertexInputStateCreateInfo normalVertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(normalVertexAttributes.size()),
      .pVertexAttributeDescriptions = normalVertexAttributes.data(),
  };
  const auto tangentVertexAttributes = _mesh->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
       {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, color)},
       {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex2D, tangent)}});
  const VkPipelineVertexInputStateCreateInfo tangentVertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(tangentVertexAttributes.size()),
      .pVertexAttributeDescriptions = tangentVertexAttributes.data(),
  };
  const auto materialVertexAttributes = _mesh->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
       {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, color)},
       {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)}});
  const VkPipelineVertexInputStateCreateInfo materialVertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(materialVertexAttributes.size()),
      .pVertexAttributeDescriptions = materialVertexAttributes.data(),
  };
  const auto litVertexAttributes = _mesh->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, pos)},
       {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, normal)},
       {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, color)},
       {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, texCoord)},
       {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex2D, tangent)}});
  const VkPipelineVertexInputStateCreateInfo litVertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(litVertexAttributes.size()),
      .pVertexAttributeDescriptions = litVertexAttributes.data(),
  };

  // initialize UBO
  _cameraUBOFull.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _cameraUBOFull[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());

  // setup Normal
  {
    _shaderNormal = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    _shaderNormal->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                     "shaders/sprite/spriteNormal_vertex.spv"));
    _shaderNormal->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                     "shaders/shape/cubeNormal_fragment.spv"));
    _shaderNormal->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                     "shaders/shape/cubeNormal_geometry.spv"));
    _descriptorSetLayoutNormalsMesh = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    _descriptorSetLayoutNormalsMesh->createCustom(_shaderNormal->getDescriptorSetLayoutBindings().at(0));

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
    _pipelineNormal = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelineNormal->createGraphic(normalPipelineGraphic, _shaderNormal->getShaderStageInfo(), normalLayouts, {},
                                   normalVertexInput);

    // initialize Tangent (per vertex)
    {
      _shaderTangent = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
      _shaderTangent->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                        "shaders/sprite/spriteTangent_vertex.spv"));
      _shaderTangent->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                        "shaders/shape/cubeNormal_fragment.spv"));
      _shaderTangent->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                        "shaders/shape/cubeNormal_geometry.spv"));

      RenderGraph::PipelineGraphic tangentPipelineGraphic;
      tangentPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      tangentPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      tangentPipelineGraphic.setDepthTest(true);
      tangentPipelineGraphic.setDepthWrite(true);
      tangentPipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
      _pipelineTangent = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipelineTangent->createGraphic(tangentPipelineGraphic, _shaderTangent->getShaderStageInfo(), normalLayouts, {},
                                      tangentVertexInput);
    }
  }

  // setup color
  {
    _shader[MaterialType::COLOR] = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    _shader[MaterialType::COLOR]->add(_engineState->getFilesystem()->readFile<char>(
        _engineState->getSettings()->getShadersPath() + "shaders/sprite/spriteColor_vertex.spv"));
    _shader[MaterialType::COLOR]->add(_engineState->getFilesystem()->readFile<char>(
        _engineState->getSettings()->getShadersPath() + "shaders/sprite/spriteColor_fragment.spv"));
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(_shader[MaterialType::COLOR]->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetLayout[MaterialType::COLOR].push_back(descriptorSetLayout);

    _descriptorSetColor.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetColor[i] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(descriptorSetLayout).get()},
          *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
    }

    // phong is default, will form default descriptor only for it here
    // descriptors are formed in setMaterial

    std::vector<RenderGraph::DescriptorSetLayout*> colorLayouts;
    for (const auto& layout : _descriptorSetLayout[MaterialType::COLOR]) colorLayouts.push_back(layout.get());
    RenderGraph::PipelineGraphic colorPipelineGraphic;
    colorPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    colorPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    colorPipelineGraphic.setDepthTest(true);
    colorPipelineGraphic.setDepthWrite(true);
    colorPipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
    _pipeline[MaterialType::COLOR] = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipeline[MaterialType::COLOR]->createGraphic(colorPipelineGraphic,
                                                  _shader[MaterialType::COLOR]->getShaderStageInfo(), colorLayouts, {},
                                                  materialVertexInput);

    RenderGraph::PipelineGraphic colorWireframePipelineGraphic;
    colorWireframePipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    colorWireframePipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    colorWireframePipelineGraphic.setDepthTest(true);
    colorWireframePipelineGraphic.setDepthWrite(true);
    colorWireframePipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
#ifndef __ANDROID__
    colorWireframePipelineGraphic.setPolygonMode(VK_POLYGON_MODE_LINE);
#endif
    _pipelineWireframe[MaterialType::COLOR] = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelineWireframe[MaterialType::COLOR]->createGraphic(colorWireframePipelineGraphic,
                                                           _shader[MaterialType::COLOR]->getShaderStageInfo(),
                                                           colorLayouts, {}, materialVertexInput);
  }

  _shaderDirectional = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
  _shaderDirectional->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                        "shaders/sprite/spriteDepth_vertex.spv"));
  _shaderDirectional->add(_engineState->getFilesystem()->readFile<char>(
      _engineState->getSettings()->getShadersPath() + "shaders/sprite/spriteDepthDirectional_fragment.spv"));
  _descriptorSetLayoutDepth = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
  _descriptorSetLayoutDepth->createCustom(_shaderDirectional->getDescriptorSetLayoutBindings().at(0));

  // initialize camera UBO and descriptor sets for shadow
  // initialize UBO
  _cameraUBODepth.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _descriptorSetCameraDepth.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    for (int l = 0; l < _engineState->getSettings()->getMaxDirectionalLights(); l++) {
      auto buffer = std::make_shared<RenderGraph::Buffer>(
          sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
          *_engineState->getMemoryAllocator());
      _cameraUBODepth[i].push_back({buffer});

      auto cameraSet = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayoutDepth).get()},
          *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
      cameraSet->add({buffer.get()});
      cameraSet->add({_defaultMaterialPhong->getBaseColor()[0].get()});
      cameraSet->initialize(*commandBufferTransfer);
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
            std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayoutDepth).get()},
            *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
        facesSet[f]->add({facesBuffer[f].get()});
        facesSet[f]->add({_defaultMaterialPhong->getBaseColor()[0].get()});
        facesSet[f]->initialize(*commandBufferTransfer);
      }
      _cameraUBODepth[i].push_back(facesBuffer);
      _descriptorSetCameraDepth[i].push_back(facesSet);
    }
  }

  // setup Phong
  {
    _shader[MaterialType::PHONG] = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    _shader[MaterialType::PHONG]->add(_engineState->getFilesystem()->readFile<char>(
        _engineState->getSettings()->getShadersPath() + "shaders/sprite/spritePhong_vertex.spv"));
    _shader[MaterialType::PHONG]->add(_engineState->getFilesystem()->readFile<char>(
        _engineState->getSettings()->getShadersPath() + "shaders/sprite/spritePhong_fragment.spv"));
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(_shader[MaterialType::PHONG]->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetLayout[MaterialType::PHONG].push_back(descriptorSetLayout);
    _descriptorSetLayout[MaterialType::PHONG].push_back(_gameState->getLightManager()->getDSLGlobalPhong());

    _descriptorSetPhong.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetPhong[i] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(descriptorSetLayout).get()},
          *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
    }
    setMaterial(_defaultMaterialPhong);

    const std::unordered_map<std::string, VkPushConstantRange> phongPushConstants{
        {"constants", {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(FragmentPush)}}};
    std::vector<RenderGraph::DescriptorSetLayout*> phongLayouts;
    for (const auto& layout : _descriptorSetLayout[MaterialType::PHONG]) phongLayouts.push_back(layout.get());
    RenderGraph::PipelineGraphic phongPipelineGraphic;
    phongPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    phongPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    phongPipelineGraphic.setDepthTest(true);
    phongPipelineGraphic.setDepthWrite(true);
    phongPipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
    _pipeline[MaterialType::PHONG] = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipeline[MaterialType::PHONG]->createGraphic(phongPipelineGraphic,
                                                  _shader[MaterialType::PHONG]->getShaderStageInfo(), phongLayouts,
                                                  phongPushConstants, litVertexInput);

    RenderGraph::PipelineGraphic phongWireframePipelineGraphic;
    phongWireframePipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    phongWireframePipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    phongWireframePipelineGraphic.setDepthTest(true);
    phongWireframePipelineGraphic.setDepthWrite(true);
    phongWireframePipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
#ifndef __ANDROID__
    phongWireframePipelineGraphic.setPolygonMode(VK_POLYGON_MODE_LINE);
#endif
    _pipelineWireframe[MaterialType::PHONG] = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelineWireframe[MaterialType::PHONG]->createGraphic(phongWireframePipelineGraphic,
                                                           _shader[MaterialType::PHONG]->getShaderStageInfo(),
                                                           phongLayouts, phongPushConstants, litVertexInput);
  }

  // setup PBR
  {
    _shader[MaterialType::PBR] = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    _shader[MaterialType::PBR]->add(_engineState->getFilesystem()->readFile<char>(
        _engineState->getSettings()->getShadersPath() + "shaders/sprite/spritePBR_vertex.spv"));
    _shader[MaterialType::PBR]->add(_engineState->getFilesystem()->readFile<char>(
        _engineState->getSettings()->getShadersPath() + "shaders/sprite/spritePBR_fragment.spv"));
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(_shader[MaterialType::PBR]->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetLayout[MaterialType::PBR].push_back(descriptorSetLayout);
    _descriptorSetLayout[MaterialType::PBR].push_back(_gameState->getLightManager()->getDSLGlobalPBR());

    _descriptorSetPBR.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetPBR[i] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(descriptorSetLayout).get()},
          *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
    }
    // phong is default, will form default descriptor only for it here
    // descriptors are formed in setMaterial

    const std::unordered_map<std::string, VkPushConstantRange> pbrPushConstants{
        {"constants", {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(FragmentPush)}}};
    std::vector<RenderGraph::DescriptorSetLayout*> pbrLayouts;
    for (const auto& layout : _descriptorSetLayout[MaterialType::PBR]) pbrLayouts.push_back(layout.get());
    RenderGraph::PipelineGraphic pbrPipelineGraphic;
    pbrPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    pbrPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    pbrPipelineGraphic.setDepthTest(true);
    pbrPipelineGraphic.setDepthWrite(true);
    pbrPipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
    _pipeline[MaterialType::PBR] = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipeline[MaterialType::PBR]->createGraphic(pbrPipelineGraphic, _shader[MaterialType::PBR]->getShaderStageInfo(),
                                                pbrLayouts, pbrPushConstants, litVertexInput);

    RenderGraph::PipelineGraphic pbrWireframePipelineGraphic;
    pbrWireframePipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    pbrWireframePipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    pbrWireframePipelineGraphic.setDepthTest(true);
    pbrWireframePipelineGraphic.setDepthWrite(true);
    pbrWireframePipelineGraphic.setCullMode(VK_CULL_MODE_BACK_BIT);
#ifndef __ANDROID__
    pbrWireframePipelineGraphic.setPolygonMode(VK_POLYGON_MODE_LINE);
#endif
    _pipelineWireframe[MaterialType::PBR] = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelineWireframe[MaterialType::PBR]->createGraphic(pbrWireframePipelineGraphic,
                                                         _shader[MaterialType::PBR]->getShaderStageInfo(), pbrLayouts,
                                                         pbrPushConstants, litVertexInput);
  }

  // initialize depth directional
  RenderGraph::PipelineGraphic directionalPipelineGraphic;
  directionalPipelineGraphic.setColorAttachments({_engineState->getSettings()->getShadowMapFormat()});
  directionalPipelineGraphic.setDepthAttachment(std::nullopt);
  directionalPipelineGraphic.setDepthBias(true);
  directionalPipelineGraphic.setColorBlendOp(VK_BLEND_OP_MIN);
  std::vector<RenderGraph::DescriptorSetLayout*> shadowLayouts{_descriptorSetLayoutDepth.get()};
  _pipelineDirectional = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
  _pipelineDirectional->createGraphic(directionalPipelineGraphic, _shaderDirectional->getShaderStageInfo(),
                                      shadowLayouts, {}, materialVertexInput);

  // initialize depth point
  {
    std::unordered_map<std::string, VkPushConstantRange> defaultPushConstants;
    defaultPushConstants["constants"] = VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(FragmentPointLightPushDepth),
    };

    _shaderPoint = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    _shaderPoint->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                    "shaders/sprite/spriteDepth_vertex.spv"));
    _shaderPoint->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                    "shaders/sprite/spriteDepthPoint_fragment.spv"));
    RenderGraph::PipelineGraphic pointPipelineGraphic;
    pointPipelineGraphic.setColorAttachments({_engineState->getSettings()->getShadowMapFormat()});
    pointPipelineGraphic.setDepthAttachment(std::nullopt);
    pointPipelineGraphic.setDepthBias(true);
    pointPipelineGraphic.setColorBlendOp(VK_BLEND_OP_MIN);
    _pipelinePoint = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelinePoint->createGraphic(pointPipelineGraphic, _shaderPoint->getShaderStageInfo(), shadowLayouts,
                                  defaultPushConstants, materialVertexInput);
  }
}

void Sprite::_updateColorDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialColor>(_material);
  material->unregisterUpdate(_descriptorSetColor[currentFrame]);
  _descriptorSetColor[currentFrame] = std::make_shared<RenderGraph::DescriptorSet>(
      std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout[MaterialType::COLOR][0]).get()},
      *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
  _descriptorSetColor[currentFrame]->add({_cameraUBOFull[currentFrame].get()});
  _descriptorSetColor[currentFrame]->add({material->getBaseColor()[0].get()});
  _descriptorSetColor[currentFrame]->initialize(commandBuffer);
  material->registerUpdate(_descriptorSetColor[currentFrame],
                           {.frameInFlight = currentFrame, .materialInfo = {{MaterialTexture::COLOR, 1}}});
}

void Sprite::_updatePhongDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialPhong>(_material);
  material->unregisterUpdate(_descriptorSetPhong[currentFrame]);
  _descriptorSetPhong[currentFrame] = std::make_shared<RenderGraph::DescriptorSet>(
      std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout[MaterialType::PHONG][0]).get()},
      *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
  _descriptorSetPhong[currentFrame]->add({_cameraUBOFull[currentFrame].get()});
  _descriptorSetPhong[currentFrame]->add({material->getBaseColor()[0].get()});
  _descriptorSetPhong[currentFrame]->add({material->getNormal()[0].get()});
  _descriptorSetPhong[currentFrame]->add({material->getSpecular()[0].get()});
  _descriptorSetPhong[currentFrame]->add({material->getBufferCoefficients()[currentFrame].get()});
  _descriptorSetPhong[currentFrame]->initialize(commandBuffer);
  material->registerUpdate(
      _descriptorSetPhong[currentFrame],
      {.frameInFlight = currentFrame,
       .materialInfo = {{MaterialTexture::COLOR, 1}, {MaterialTexture::NORMAL, 2}, {MaterialTexture::SPECULAR, 3}}});
}

void Sprite::_updatePBRDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialPBR>(_material);
  material->unregisterUpdate(_descriptorSetPBR[currentFrame]);
  _descriptorSetPBR[currentFrame] = std::make_shared<RenderGraph::DescriptorSet>(
      std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout[MaterialType::PBR][0]).get()},
      *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
  _descriptorSetPBR[currentFrame]->add({_cameraUBOFull[currentFrame].get()});
  _descriptorSetPBR[currentFrame]->add({material->getBaseColor()[0].get()});
  _descriptorSetPBR[currentFrame]->add({material->getNormal()[0].get()});
  _descriptorSetPBR[currentFrame]->add({material->getMetallic()[0].get()});
  _descriptorSetPBR[currentFrame]->add({material->getRoughness()[0].get()});
  _descriptorSetPBR[currentFrame]->add({material->getOccluded()[0].get()});
  _descriptorSetPBR[currentFrame]->add({material->getEmissive()[0].get()});
  _descriptorSetPBR[currentFrame]->add({material->getDiffuseIBL().get()});
  _descriptorSetPBR[currentFrame]->add({material->getSpecularIBL().get()});
  _descriptorSetPBR[currentFrame]->add({material->getSpecularBRDF().get()});
  _descriptorSetPBR[currentFrame]->add({material->getBufferCoefficients()[currentFrame].get()});
  _descriptorSetPBR[currentFrame]->initialize(commandBuffer);
  material->registerUpdate(_descriptorSetPBR[currentFrame], {.frameInFlight = currentFrame,
                                                             .materialInfo = {{MaterialTexture::COLOR, 1},
                                                                              {MaterialTexture::NORMAL, 2},
                                                                              {MaterialTexture::METALLIC, 3},
                                                                              {MaterialTexture::ROUGHNESS, 4},
                                                                              {MaterialTexture::OCCLUSION, 5},
                                                                              {MaterialTexture::EMISSIVE, 6},
                                                                              {MaterialTexture::IBL_DIFFUSE, 7},
                                                                              {MaterialTexture::IBL_SPECULAR, 8},
                                                                              {MaterialTexture::BRDF_SPECULAR, 9}}});
}

void Sprite::_unregisterMaterialDescriptors() {
  if (!_material) return;
  for (int frame = 0; frame < _changedMaterialRender.size(); ++frame) {
    if (_materialType == MaterialType::COLOR && frame < _descriptorSetColor.size())
      _material->unregisterUpdate(_descriptorSetColor[frame]);
    if (_materialType == MaterialType::PHONG && frame < _descriptorSetPhong.size())
      _material->unregisterUpdate(_descriptorSetPhong[frame]);
    if (_materialType == MaterialType::PBR && frame < _descriptorSetPBR.size())
      _material->unregisterUpdate(_descriptorSetPBR[frame]);
    if (frame < _descriptorSetCameraDepth.size()) {
      for (const auto& light : _descriptorSetCameraDepth[frame]) {
        for (const auto& descriptor : light) _material->unregisterUpdate(descriptor);
      }
    }
  }
}

void Sprite::setMaterial(std::shared_ptr<MaterialPBR> material) {
  _unregisterMaterialDescriptors();
  _materialType = MaterialType::PBR;
  _material = material;
  for (int i = 0; i < _changedMaterialRender.size(); i++) {
    _changedMaterialRender[i] = true;
    _changedMaterialShadow[i] = true;
  }
}

void Sprite::setMaterial(std::shared_ptr<MaterialPhong> material) {
  _unregisterMaterialDescriptors();
  _materialType = MaterialType::PHONG;
  _material = material;
  for (int i = 0; i < _changedMaterialRender.size(); i++) {
    _changedMaterialRender[i] = true;
    _changedMaterialShadow[i] = true;
  }
}

void Sprite::setMaterial(std::shared_ptr<MaterialColor> material) {
  _unregisterMaterialDescriptors();
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

void Sprite::draw(const RenderGraph::CommandBuffer* commandBuffer) {
  const int currentFrame = _engineState->getFrameInFlight();
  if (_changedMaterialRender[currentFrame]) {
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
    _changedMaterialRender[currentFrame] = false;
  }

  auto pipeline = _pipeline[_materialType];
  if (_drawType == DrawType::WIREFRAME) pipeline = _pipelineWireframe[_materialType];
  if (_drawType == DrawType::NORMAL) pipeline = _pipelineNormal;
  if (_drawType == DrawType::TANGENT) pipeline = _pipelineTangent;

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

  if (pipeline->getPushConstants().find("constants") != pipeline->getPushConstants().end()) {
    FragmentPush pushConstants{.enableShadow = _enableShadow,
                               .enableLighting = _enableLighting,
                               .cameraPosition = _gameState->getCameraManager()->getCurrentCamera()->getEye()};
    auto info = pipeline->getPushConstants().at("constants");
    vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                       info.size, &pushConstants);
  }

  BufferMVP cameraMVP{.model = getModel(), .view = glm::mat4(1.f), .projection = glm::mat4(1.f)};
  if (_enableHUD == false) {
    cameraMVP.view = _gameState->getCameraManager()->getCurrentCamera()->getView();
    cameraMVP.projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection();
  }

  std::memcpy(static_cast<std::byte*>(_cameraUBOFull[currentFrame]->getAllocationInfo().pMappedData), &cameraMVP,
              static_cast<std::size_t>(_cameraUBOFull[currentFrame]->getSize()));

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), _mesh->getIndexBuffer()->getBuffer()->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());

  if (_drawType == DrawType::NORMAL || _drawType == DrawType::TANGENT) {
    _descriptorSetNormalsMesh[currentFrame]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(),
                                                  *commandBuffer);
  } else if (_materialType == MaterialType::COLOR) {
    _descriptorSetColor[currentFrame]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(),
                                            *commandBuffer);
  } else if (_materialType == MaterialType::PHONG) {
    _descriptorSetPhong[currentFrame]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(),
                                            *commandBuffer);
    _gameState->getLightManager()->getDSGlobalPhong()->bind(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                            pipeline->getPipelineLayout(), *commandBuffer);
  } else if (_materialType == MaterialType::PBR) {
    _descriptorSetPBR[currentFrame]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(),
                                          *commandBuffer);
    _gameState->getLightManager()->getDSGlobalPBR()->bind(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                          pipeline->getPipelineLayout(), *commandBuffer);
  }

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getIndexData().size()), 1, 0, 0, 0);
}

void Sprite::drawShadow(LightType lightType,
                        int lightIndex,
                        int face,
                        const RenderGraph::CommandBuffer* commandBuffer) {
  if (_enableDepth == false) return;

  int currentFrame = _engineState->getFrameInFlight();
  {
    // we have to update only once during the first drawShadow call
    // (for point lights this function is called multiple times in different threads)
    std::unique_lock<std::mutex> lock(_updateShadow);
    if (_changedMaterialShadow[currentFrame]) {
      switch (_materialType) {
        case MaterialType::COLOR:
          _updateShadowDescriptor(std::dynamic_pointer_cast<MaterialColor>(_material), *commandBuffer);
          break;
        case MaterialType::PHONG:
          _updateShadowDescriptor(std::dynamic_pointer_cast<MaterialPhong>(_material), *commandBuffer);
          break;
        case MaterialType::PBR:
          _updateShadowDescriptor(std::dynamic_pointer_cast<MaterialPBR>(_material), *commandBuffer);
          break;
      }
      _changedMaterialShadow[currentFrame] = false;
    }
  }

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

  if (pipeline->getPushConstants().find("constants") != pipeline->getPushConstants().end()) {
    if (lightType == LightType::POINT) {
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

  BufferMVP cameraMVP{.model = getModel(), .view = view, .projection = projection};
  std::memcpy(
      static_cast<std::byte*>(_cameraUBODepth[currentFrame][lightIndexTotal][face]->getAllocationInfo().pMappedData) +
          (0),
      &cameraMVP, static_cast<std::size_t>(_cameraUBODepth[currentFrame][lightIndexTotal][face]->getSize()));

  VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), _mesh->getIndexBuffer()->getBuffer()->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  _descriptorSetCameraDepth[currentFrame][lightIndexTotal][face]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                       pipeline->getPipelineLayout(), *commandBuffer);

  vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getIndexData().size()), 1, 0, 0, 0);
}
