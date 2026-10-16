#include <cstring>
#include <type_traits>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <chrono>
#include <future>
#include <ranges>
#include "Primitive/Shape3D.h"

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
                 const RenderGraph::CommandBuffer* commandBufferTransfer,
                 std::shared_ptr<GameState> gameState,
                 std::shared_ptr<EngineState> engineState) {
  setName("Shape3D");
  _shapeType = shapeType;
  _engineState = engineState;
  _gameState = gameState;
  _mesh = mesh;
  _changedMaterial.resize(_engineState->getSettings()->getMaxFramesInFlight(), false);
  // needed for layout
  _defaultMaterialColor = std::make_shared<MaterialColor>(MaterialTarget::SIMPLE, engineState);

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
  const auto colorVertexAttributes =
      shapeType == ShapeType::CUBE
          ? _mesh->getAttributeDescriptions(
                {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)}})
          : _mesh->getAttributeDescriptions(
                {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)},
                 {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)}});
  const VkPipelineVertexInputStateCreateInfo colorVertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(colorVertexAttributes.size()),
      .pVertexAttributeDescriptions = colorVertexAttributes.data(),
  };
  const auto litVertexAttributes =
      shapeType == ShapeType::CUBE
          ? _mesh->getAttributeDescriptions(
                {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)},
                 {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, tangent)}})
          : _mesh->getAttributeDescriptions(
                {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)},
                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)},
                 {VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color)},
                 {VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, texCoord)},
                 {VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex3D, tangent)}});
  const VkPipelineVertexInputStateCreateInfo litVertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(litVertexAttributes.size()),
      .pVertexAttributeDescriptions = litVertexAttributes.data(),
  };
  const auto shadowVertexAttributes = _mesh->getAttributeDescriptions(
      {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}});
  const VkPipelineVertexInputStateCreateInfo shadowVertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(shadowVertexAttributes.size()),
      .pVertexAttributeDescriptions = shadowVertexAttributes.data(),
  };

  _material = _defaultMaterialColor;

  _uniformBufferCamera.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _uniformBufferCamera[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *engineState->getMemoryAllocator());
  // setup normals
  {
    auto shaderNormal = std::make_shared<RenderGraph::Shader>(*engineState->getDevice());
    shaderNormal->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                    _shadersNormalsMesh[_shapeType][0]));
    shaderNormal->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                    _shadersNormalsMesh[_shapeType][1]));
    shaderNormal->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                    _shadersNormalsMesh[_shapeType][2]));
    _descriptorSetLayoutNormalsMesh = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    _descriptorSetLayoutNormalsMesh->createCustom(shaderNormal->getDescriptorSetLayoutBindings().at(0));

    _descriptorSetNormalsMesh.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetNormalsMesh[i] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayoutNormalsMesh).get()},
          *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
      _descriptorSetNormalsMesh[i]->add({_uniformBufferCamera[i].get()});
      _descriptorSetNormalsMesh[i]->add({_uniformBufferCamera[i].get()});
      _descriptorSetNormalsMesh[i]->initialize(*commandBufferTransfer);
    }

    std::vector<RenderGraph::DescriptorSetLayout*> normalLayouts{_descriptorSetLayoutNormalsMesh.get()};
    RenderGraph::PipelineGraphic normalPipelineGraphic;
    normalPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    normalPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    normalPipelineGraphic.setDepthTest(true);
    normalPipelineGraphic.setDepthWrite(true);
    normalPipelineGraphic.setCullMode(cullMode);
    _pipelineNormalMesh = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelineNormalMesh->createGraphic(normalPipelineGraphic, shaderNormal->getShaderStageInfo(), normalLayouts, {},
                                       normalVertexInput);
    // initialize Tangent (per vertex)
    {
      auto shader = std::make_shared<RenderGraph::Shader>(*engineState->getDevice());
      shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                _shadersTangentMesh[_shapeType][0]));
      shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                _shadersTangentMesh[_shapeType][1]));
      shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                _shadersTangentMesh[_shapeType][2]));

      RenderGraph::PipelineGraphic tangentPipelineGraphic;
      tangentPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      tangentPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      tangentPipelineGraphic.setDepthTest(true);
      tangentPipelineGraphic.setDepthWrite(true);
      tangentPipelineGraphic.setCullMode(cullMode);
      _pipelineTangentMesh = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipelineTangentMesh->createGraphic(tangentPipelineGraphic, shader->getShaderStageInfo(), normalLayouts, {},
                                          tangentVertexInput);
    }
  }

  auto shaderDirectional = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
  shaderDirectional->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                       _shadersLightDirectional[_shapeType][0]));
  shaderDirectional->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                                       _shadersLightDirectional[_shapeType][1]));
  _descriptorSetLayoutDepth = std::make_shared<RenderGraph::DescriptorSetLayout>(*engineState->getDevice());
  _descriptorSetLayoutDepth->createCustom(shaderDirectional->getDescriptorSetLayoutBindings().at(0));

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
          std::vector<RenderGraph::DescriptorSetLayout*>{_descriptorSetLayoutDepth.get()},
          *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
      cameraSet->add({buffer.get()});
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
            std::vector<RenderGraph::DescriptorSetLayout*>{_descriptorSetLayoutDepth.get()},
            *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
        facesSet[f]->add({facesBuffer[f].get()});
        facesSet[f]->initialize(*commandBufferTransfer);
      }
      _cameraUBODepth[i].push_back(facesBuffer);
      _descriptorSetCameraDepth[i].push_back(facesSet);
    }
  }

  // setup color
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              _shadersColor[_shapeType][MaterialType::COLOR][0]));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              _shadersColor[_shapeType][MaterialType::COLOR][1]));
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetLayout[MaterialType::COLOR].push_back(descriptorSetLayout);

    _descriptorSetColor.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetColor[i] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(descriptorSetLayout).get()},
          *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
    }
    setMaterial(_defaultMaterialColor);

    std::vector<RenderGraph::DescriptorSetLayout*> colorLayouts;
    for (const auto& layout : _descriptorSetLayout[MaterialType::COLOR]) colorLayouts.push_back(layout.get());
    RenderGraph::PipelineGraphic colorPipelineGraphic;
    colorPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    colorPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    colorPipelineGraphic.setDepthTest(true);
    colorPipelineGraphic.setDepthWrite(true);
    colorPipelineGraphic.setCullMode(cullMode);
    _pipeline[_shapeType][MaterialType::COLOR] = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipeline[_shapeType][MaterialType::COLOR]->createGraphic(colorPipelineGraphic, shader->getShaderStageInfo(),
                                                              colorLayouts, {}, colorVertexInput);

    RenderGraph::PipelineGraphic colorWireframePipelineGraphic;
    colorWireframePipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    colorWireframePipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    colorWireframePipelineGraphic.setDepthTest(true);
    colorWireframePipelineGraphic.setDepthWrite(true);
    colorWireframePipelineGraphic.setCullMode(cullMode);
#ifndef __ANDROID__
    colorWireframePipelineGraphic.setPolygonMode(VK_POLYGON_MODE_LINE);
#endif
    _pipelineWireframe[_shapeType][MaterialType::COLOR] = std::make_shared<RenderGraph::Pipeline>(
        *_engineState->getDevice());
    _pipelineWireframe[_shapeType][MaterialType::COLOR]->createGraphic(
        colorWireframePipelineGraphic, shader->getShaderStageInfo(), colorLayouts, {}, colorVertexInput);
  }

  // setup Phong
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              _shadersColor[_shapeType][MaterialType::PHONG][0]));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              _shadersColor[_shapeType][MaterialType::PHONG][1]));
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetLayout[MaterialType::PHONG].push_back(descriptorSetLayout);
    _descriptorSetLayout[MaterialType::PHONG].push_back(_gameState->getLightManager()->getDSLGlobalPhong());

    _descriptorSetPhong.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetPhong[i] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(descriptorSetLayout).get()},
          *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
    }
    // update descriptor set in setMaterial

    const std::unordered_map<std::string, VkPushConstantRange> phongPushConstants{
        {"constants", {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(FragmentPush)}}};
    std::vector<RenderGraph::DescriptorSetLayout*> phongLayouts;
    for (const auto& layout : _descriptorSetLayout[MaterialType::PHONG]) phongLayouts.push_back(layout.get());
    RenderGraph::PipelineGraphic phongPipelineGraphic;
    phongPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    phongPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    phongPipelineGraphic.setDepthTest(true);
    phongPipelineGraphic.setDepthWrite(true);
    phongPipelineGraphic.setCullMode(cullMode);
    _pipeline[_shapeType][MaterialType::PHONG] = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipeline[_shapeType][MaterialType::PHONG]->createGraphic(phongPipelineGraphic, shader->getShaderStageInfo(),
                                                              phongLayouts, phongPushConstants, litVertexInput);

    RenderGraph::PipelineGraphic phongWireframePipelineGraphic;
    phongWireframePipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    phongWireframePipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    phongWireframePipelineGraphic.setDepthTest(true);
    phongWireframePipelineGraphic.setDepthWrite(true);
    phongWireframePipelineGraphic.setCullMode(cullMode);
#ifndef __ANDROID__
    phongWireframePipelineGraphic.setPolygonMode(VK_POLYGON_MODE_LINE);
#endif
    _pipelineWireframe[_shapeType][MaterialType::PHONG] = std::make_shared<RenderGraph::Pipeline>(
        *_engineState->getDevice());
    _pipelineWireframe[_shapeType][MaterialType::PHONG]->createGraphic(
        phongWireframePipelineGraphic, shader->getShaderStageInfo(), phongLayouts, phongPushConstants, litVertexInput);
  }

  // setup PBR
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              _shadersColor[_shapeType][MaterialType::PBR][0]));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              _shadersColor[_shapeType][MaterialType::PBR][1]));
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetLayout[MaterialType::PBR].push_back(descriptorSetLayout);
    _descriptorSetLayout[MaterialType::PBR].push_back(_gameState->getLightManager()->getDSLGlobalPBR());

    _descriptorSetPBR.resize(engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < engineState->getSettings()->getMaxFramesInFlight(); i++) {
      _descriptorSetPBR[i] = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{(descriptorSetLayout).get()},
          *engineState->getDescriptorPool(), *engineState->getDevice(), 0);
    }
    // update descriptor set in setMaterial

    const std::unordered_map<std::string, VkPushConstantRange> pbrPushConstants{
        {"constants", {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(FragmentPush)}}};
    std::vector<RenderGraph::DescriptorSetLayout*> pbrLayouts;
    for (const auto& layout : _descriptorSetLayout[MaterialType::PBR]) pbrLayouts.push_back(layout.get());
    RenderGraph::PipelineGraphic pbrPipelineGraphic;
    pbrPipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    pbrPipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    pbrPipelineGraphic.setDepthTest(true);
    pbrPipelineGraphic.setDepthWrite(true);
    pbrPipelineGraphic.setCullMode(cullMode);
    _pipeline[_shapeType][MaterialType::PBR] = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipeline[_shapeType][MaterialType::PBR]->createGraphic(pbrPipelineGraphic, shader->getShaderStageInfo(),
                                                            pbrLayouts, pbrPushConstants, litVertexInput);

    RenderGraph::PipelineGraphic pbrWireframePipelineGraphic;
    pbrWireframePipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
    pbrWireframePipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
    pbrWireframePipelineGraphic.setDepthTest(true);
    pbrWireframePipelineGraphic.setDepthWrite(true);
    pbrWireframePipelineGraphic.setCullMode(cullMode);
#ifndef __ANDROID__
    pbrWireframePipelineGraphic.setPolygonMode(VK_POLYGON_MODE_LINE);
#endif
    _pipelineWireframe[_shapeType][MaterialType::PBR] = std::make_shared<RenderGraph::Pipeline>(
        *_engineState->getDevice());
    _pipelineWireframe[_shapeType][MaterialType::PBR]->createGraphic(
        pbrWireframePipelineGraphic, shader->getShaderStageInfo(), pbrLayouts, pbrPushConstants, litVertexInput);
  }

  // initialize shadows directional
  RenderGraph::PipelineGraphic directionalPipelineGraphic;
  directionalPipelineGraphic.setColorAttachments({_engineState->getSettings()->getShadowMapFormat()});
  directionalPipelineGraphic.setDepthAttachment(std::nullopt);
  directionalPipelineGraphic.setDepthBias(true);
  directionalPipelineGraphic.setColorBlendOp(VK_BLEND_OP_MIN);
  std::vector<RenderGraph::DescriptorSetLayout*> shadowLayouts{_descriptorSetLayoutDepth.get()};
  _pipelineDirectional = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
  _pipelineDirectional->createGraphic(directionalPipelineGraphic, shaderDirectional->getShaderStageInfo(),
                                      shadowLayouts, {}, shadowVertexInput);

  // initialize shadows point
  {
    std::unordered_map<std::string, VkPushConstantRange> defaultPushConstants;
    defaultPushConstants["constants"] = VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(FragmentPointLightPushDepth),
    };

    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              _shadersLightPoint[_shapeType][0]));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              _shadersLightPoint[_shapeType][1]));
    RenderGraph::PipelineGraphic pointPipelineGraphic;
    pointPipelineGraphic.setColorAttachments({_engineState->getSettings()->getShadowMapFormat()});
    pointPipelineGraphic.setDepthAttachment(std::nullopt);
    pointPipelineGraphic.setDepthBias(true);
    pointPipelineGraphic.setColorBlendOp(VK_BLEND_OP_MIN);
    _pipelinePoint = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
    _pipelinePoint->createGraphic(pointPipelineGraphic, shader->getShaderStageInfo(), shadowLayouts,
                                  defaultPushConstants, shadowVertexInput);
  }
}

void Shape3D::_updateColorDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialColor>(_material);

  material->unregisterUpdate(_descriptorSetColor[frameInFlight]);
  _descriptorSetColor[frameInFlight] = std::make_shared<RenderGraph::DescriptorSet>(
      std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout[MaterialType::COLOR][0]).get()},
      *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
  _descriptorSetColor[frameInFlight]->add({_uniformBufferCamera[frameInFlight].get()});
  _descriptorSetColor[frameInFlight]->add({material->getBaseColor()[0].get()});
  _descriptorSetColor[frameInFlight]->initialize(commandBuffer);
  material->registerUpdate(_descriptorSetColor[frameInFlight],
                           {.frameInFlight = frameInFlight, .materialInfo = {{MaterialTexture::COLOR, 1}}});
}

void Shape3D::_updatePhongDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialPhong>(_material);

  material->unregisterUpdate(_descriptorSetPhong[frameInFlight]);
  _descriptorSetPhong[frameInFlight] = std::make_shared<RenderGraph::DescriptorSet>(
      std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout[MaterialType::PHONG][0]).get()},
      *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
  _descriptorSetPhong[frameInFlight]->add({_uniformBufferCamera[frameInFlight].get()});
  _descriptorSetPhong[frameInFlight]->add({material->getBaseColor()[0].get()});
  _descriptorSetPhong[frameInFlight]->add({material->getNormal()[0].get()});
  _descriptorSetPhong[frameInFlight]->add({material->getSpecular()[0].get()});
  _descriptorSetPhong[frameInFlight]->add({material->getBufferCoefficients()[frameInFlight].get()});
  _descriptorSetPhong[frameInFlight]->initialize(commandBuffer);
  material->registerUpdate(
      _descriptorSetPhong[frameInFlight],
      {.frameInFlight = frameInFlight,
       .materialInfo = {{MaterialTexture::COLOR, 1}, {MaterialTexture::NORMAL, 2}, {MaterialTexture::SPECULAR, 3}}});
}

void Shape3D::_updatePBRDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto material = std::dynamic_pointer_cast<MaterialPBR>(_material);

  material->unregisterUpdate(_descriptorSetPBR[frameInFlight]);
  _descriptorSetPBR[frameInFlight] = std::make_shared<RenderGraph::DescriptorSet>(
      std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout[MaterialType::PBR][0]).get()},
      *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
  _descriptorSetPBR[frameInFlight]->add({_uniformBufferCamera[frameInFlight].get()});
  _descriptorSetPBR[frameInFlight]->add({material->getBaseColor()[0].get()});
  _descriptorSetPBR[frameInFlight]->add({material->getNormal()[0].get()});
  _descriptorSetPBR[frameInFlight]->add({material->getMetallic()[0].get()});
  _descriptorSetPBR[frameInFlight]->add({material->getRoughness()[0].get()});
  _descriptorSetPBR[frameInFlight]->add({material->getOccluded()[0].get()});
  _descriptorSetPBR[frameInFlight]->add({material->getEmissive()[0].get()});
  _descriptorSetPBR[frameInFlight]->add({material->getDiffuseIBL().get()});
  _descriptorSetPBR[frameInFlight]->add({material->getSpecularIBL().get()});
  _descriptorSetPBR[frameInFlight]->add({material->getSpecularBRDF().get()});
  _descriptorSetPBR[frameInFlight]->add({material->getBufferCoefficients()[frameInFlight].get()});
  _descriptorSetPBR[frameInFlight]->add({material->getBufferAlphaCutoff()[frameInFlight].get()});
  _descriptorSetPBR[frameInFlight]->initialize(commandBuffer);
  material->registerUpdate(_descriptorSetPBR[frameInFlight], {.frameInFlight = frameInFlight,
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

void Shape3D::enableShadow(bool enable) { _enableShadow = enable; }

void Shape3D::enableLighting(bool enable) { _enableLighting = enable; }

void Shape3D::_unregisterMaterialUpdates() {
  if (!_material) return;

  const auto unregisterDescriptors = [&](const auto& descriptorSets) {
    for (const auto& descriptorSet : descriptorSets) {
      if (descriptorSet) _material->unregisterUpdate(descriptorSet);
    }
  };

  switch (_materialType) {
    case MaterialType::COLOR:
      unregisterDescriptors(_descriptorSetColor);
      break;
    case MaterialType::PHONG:
      unregisterDescriptors(_descriptorSetPhong);
      break;
    case MaterialType::PBR:
      unregisterDescriptors(_descriptorSetPBR);
      break;
  }
}

void Shape3D::setMaterial(std::shared_ptr<MaterialColor> material) {
  _unregisterMaterialUpdates();
  _materialType = MaterialType::COLOR;
  _material = material;
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void Shape3D::setMaterial(std::shared_ptr<MaterialPhong> material) {
  _unregisterMaterialUpdates();
  _materialType = MaterialType::PHONG;
  _material = material;
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void Shape3D::setMaterial(std::shared_ptr<MaterialPBR> material) {
  _unregisterMaterialUpdates();
  _materialType = MaterialType::PBR;
  _material = material;
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}
void Shape3D::setDrawType(DrawType drawType) { _drawType = drawType; }

std::shared_ptr<Mesh3D> Shape3D::getMesh() { return _mesh; }

void Shape3D::draw(const RenderGraph::CommandBuffer* commandBuffer) {
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

  auto drawShape3D = [&](std::shared_ptr<RenderGraph::Pipeline> pipeline) {
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
      auto info = pipeline->getPushConstants().at("constants");
      vkCmdPushConstants(commandBuffer->getCommandBuffer(), pipeline->getPipelineLayout(), info.stageFlags, info.offset,
                         info.size, &pushConstants);
    }

    BufferMVP cameraUBO{.model = getModel(),
                        .view = _gameState->getCameraManager()->getCurrentCamera()->getView(),
                        .projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection()};
    std::memcpy(static_cast<std::byte*>(_uniformBufferCamera[currentFrame]->getAllocationInfo().pMappedData),
                &cameraUBO, static_cast<std::size_t>(_uniformBufferCamera[currentFrame]->getSize()));

    VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()->getBuffer()->getBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);

    vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), _mesh->getIndexBuffer()->getBuffer()->getBuffer(), 0,
                         VK_INDEX_TYPE_UINT32);

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

    vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_mesh->getIndexData().size()), 1, 0, 0,
                     0);
  };

  auto pipeline = _pipeline[_shapeType][_materialType];
  if (_drawType == DrawType::WIREFRAME) pipeline = _pipelineWireframe[_shapeType][_materialType];
  if (_drawType == DrawType::NORMAL) pipeline = _pipelineNormalMesh;
  if (_drawType == DrawType::TANGENT) pipeline = _pipelineTangentMesh;
  drawShape3D(pipeline);
}

void Shape3D::drawShadow(LightType lightType,
                         int lightIndex,
                         int face,
                         const RenderGraph::CommandBuffer* commandBuffer) {
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
