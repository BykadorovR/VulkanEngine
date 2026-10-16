#include <cstring>
#include <algorithm>
#include <future>
#include <type_traits>
#include "stb_image_write.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include "Primitive/Terrain.h"

// Keep standard-library declarations outside the imported module with MSVC.
import Shader;
#undef far
#undef near

TerrainPhysics::TerrainPhysics(std::shared_ptr<ImageCPU<uint8_t>> heightmap,
                               glm::vec3 position,
                               glm::vec3 scale,
                               std::tuple<int, int> heightScaleOffset,
                               std::shared_ptr<PhysicsManager> physicsManager,
                               std::shared_ptr<GameState> gameState,
                               std::shared_ptr<EngineState> engineState) {
  _physicsManager = physicsManager;
  _gameState = gameState;
  _engineState = engineState;
  _resolution = heightmap->getResolution();
  _heightScaleOffset = heightScaleOffset;
  _scale = scale;
  _position = glm::vec3(position.x, position.y, position.z);
  _heightmap = heightmap;

  _initialize();
}

void TerrainPhysics::setFriction(float friction) {
  _physicsManager->getBodyInterface().SetFriction(_terrainID, friction);
}

void TerrainPhysics::reset(std::shared_ptr<ImageCPU<uint8_t>> heightmap) {
  _heightmap = heightmap;
  _physicsManager->getBodyInterface().RemoveBody(_terrainID);
  _physicsManager->getBodyInterface().DestroyBody(_terrainID);

  _initialize();
}

void TerrainPhysics::_initialize() {
  auto [w, h] = _resolution;

  _terrainPhysic.clear();
  int c = _heightmap->getChannels();
  for (int i = 0; i < w * h * c; i += c) {
    _terrainPhysic.push_back((float)_heightmap->getData()[i] / 255.f);
  }

  // Create height field
  JPH::HeightFieldShapeSettings settingsTerrain(
      _terrainPhysic.data(), JPH::Vec3(0.f, -static_cast<float>(std::get<1>(_heightScaleOffset)), 0.f),
      JPH::Vec3(1.f, static_cast<float>(std::get<0>(_heightScaleOffset)), 1.f), w);

  JPH::RefConst<JPH::HeightFieldShape> heightField = JPH::StaticCast<JPH::HeightFieldShape>(
      settingsTerrain.Create().Get());
  _heights.resize(w * h);
  heightField->GetHeights(0, 0, w, h, _heights.data(), w);

  // anchor point is top-left corner in physics, but center in graphic
  JPH::RefConst<JPH::RotatedTranslatedShape> rotateTranslatedHeightField = new JPH::RotatedTranslatedShape(
      JPH::Vec3(-w / 2.f, 0.f, -h / 2.f), JPH::Quat::sIdentity(), heightField);
  _terrainShape = new JPH::ScaledShape(rotateTranslatedHeightField, JPH::Vec3(_scale.x, _scale.y, _scale.z));

  auto terrainBody = _physicsManager->getBodyInterface().CreateBody(
      JPH::BodyCreationSettings(_terrainShape, JPH::Vec3(_position.x, _position.y, _position.z), JPH::Quat::sIdentity(),
                                JPH::EMotionType::Static, Layers::NON_MOVING));

  _terrainID = terrainBody->GetID();
  _physicsManager->getBodyInterface().AddBody(_terrainID, JPH::EActivation::DontActivate);
}

std::vector<float> TerrainPhysics::getHeights() { return _heights; }

void TerrainPhysics::setPosition(glm::vec3 position) {
  _position = glm::vec3(position.x, position.y, position.z);
  _physicsManager->getBodyInterface().SetPosition(_terrainID, JPH::Vec3(_position.x, _position.y, _position.z),
                                                  JPH::EActivation::DontActivate);
}

std::optional<glm::vec3> TerrainPhysics::getHit(glm::vec2 cursorPosition) {
  auto projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection();
  // forward transformation
  // x, y, z, 1 * MVP -> clip?
  // clip / w -> NDC [-1, 1]
  //(NDC + 1) / 2 -> normalized screen [0, 1]
  // normalized screen * screen size -> screen

  // backward transformation
  // x, y
  glm::vec2 normalizedScreen = glm::vec2(
      (2.0f * cursorPosition.x) / std::get<0>(_engineState->getSettings()->getResolution()) - 1.0f,
      1.0f - (2.0f * cursorPosition.y) / std::get<1>(_engineState->getSettings()->getResolution()));
  glm::vec4 clipSpacePos = glm::vec4(normalizedScreen, -1.0f, 1.0f);  // Z = -1 to specify the near plane

  // Convert to camera (view) space
  glm::vec4 viewSpacePos = glm::inverse(_gameState->getCameraManager()->getCurrentCamera()->getProjection()) *
                           clipSpacePos;
  viewSpacePos = glm::vec4(viewSpacePos.x, viewSpacePos.y, -1.0f, 0.0f);

  // Convert to world space
  glm::vec4 worldSpaceRay = glm::inverse(_gameState->getCameraManager()->getCurrentCamera()->getView()) * viewSpacePos;

  // normalize
  _rayDirection = glm::normalize(glm::vec3(worldSpaceRay));
  _rayOrigin = glm::vec3(glm::inverse(_gameState->getCameraManager()->getCurrentCamera()->getView())[3]);

  auto origin = _rayOrigin;
  auto direction = _gameState->getCameraManager()->getCurrentCamera()->getFar() * _rayDirection;
  JPH::RRayCast rray{JPH::RVec3(origin.x, origin.y, origin.z), JPH::Vec3(direction.x, direction.y, direction.z)};
  JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
  _physicsManager->getPhysicsSystem().GetNarrowPhaseQuery().CastRay(rray, JPH::RayCastSettings(), collector);

  _hitCoords = std::nullopt;
  if (collector.HadHit()) {
    for (auto& hit : collector.mHits) {
      if (hit.mBodyID == _terrainID) {
        auto outPosition = rray.GetPointOnRay(hit.mFraction);
        _hitCoords = glm::vec3(outPosition.GetX(), outPosition.GetY(), outPosition.GetZ());
        // the first intersection is the closest one
        break;
      }
    }
  }
  return _hitCoords;
}

std::tuple<int, int> TerrainPhysics::getResolution() { return _resolution; }

glm::vec3 TerrainPhysics::getPosition() { return _position; }

TerrainPhysics::~TerrainPhysics() {
  _physicsManager->getBodyInterface().RemoveBody(_terrainID);
  _physicsManager->getBodyInterface().DestroyBody(_terrainID);
}

TerrainCPU::TerrainCPU(std::shared_ptr<ImageCPU<uint8_t>> heightMap,
                       std::shared_ptr<GameState> gameState,
                       std::shared_ptr<EngineState> engineState) {
  setName("TerrainCPU");
  _engineState = engineState;
  _gameState = gameState;
  _heightMap = heightMap;

  _changeMeshHeightmap.resize(engineState->getSettings()->getMaxFramesInFlight());
  _mesh.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _mesh[i] = std::make_shared<MeshDynamic3D>(_engineState);
    _loadStrip(i);
  }
  _loadTerrain();
}

TerrainCPU::TerrainCPU(std::vector<float> heights,
                       std::tuple<int, int> resolution,
                       std::shared_ptr<GameState> gameState,
                       std::shared_ptr<EngineState> engineState) {
  setName("TerrainCPU");
  _engineState = engineState;
  _gameState = gameState;
  _resolution = resolution;
  _heights = heights;

  _changeMeshTriangles.resize(engineState->getSettings()->getMaxFramesInFlight());
  _mesh.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _mesh[i] = std::make_shared<MeshDynamic3D>(_engineState);
    _loadTriangles(i);
  }
  _loadTerrain();
}

void TerrainCPU::_updateColorDescriptor(const RenderGraph::CommandBuffer& commandBuffer) {
  std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts;
  for (const auto& layout : _descriptorSetLayout) descriptorSetLayouts.push_back(layout.get());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _descriptorSetColor[i] = std::make_shared<RenderGraph::DescriptorSet>(
        descriptorSetLayouts, *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
    _descriptorSetColor[i]->add({_cameraBuffer[i].get()});
    _descriptorSetColor[i]->initialize(commandBuffer);
  }
}

void TerrainCPU::_loadStrip(int currentFrame) {
  // vertex generation
  std::vector<Vertex3D> vertices;
  std::vector<uint32_t> indices;
  auto [width, height] = _heightMap->getResolution();
  auto channels = _heightMap->getChannels();
  auto data = _heightMap->getData();
  // 255 - max value from height map, 256 is number of colors
  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      // retrieve texel for (i,j) tex coord
      auto y = data[(j + width * i) * channels];

      // need to normalize [0, 255] to [0, 1]
      Vertex3D vertex{
          .pos = glm::vec3(-width / 2.0f + j, (y / 255.f) * _heightScale - _heightShift, -height / 2.0f + i)};
      vertices.push_back(vertex);
    }
  }

  _mesh[currentFrame]->setVertices(vertices);

  // index generation
  for (int i = 0; i < height - 1; ++i) {  // for each row a.k.a. each strip
    for (int j = 0; j < width; ++j) {     // for each column
      for (int k = 0; k < 2; ++k) {       // for each side of the strip
        indices.push_back(static_cast<uint32_t>(j + width * (i + k)));
      }
    }
  }

  _numStrips = height - 1;
  _numVertsPerStrip = width * 2;

  _mesh[currentFrame]->setIndexes(indices);

  _hasIndexes = true;
}

void TerrainCPU::_loadTriangles(int currentFrame) {
  auto [width, height] = _resolution;
  std::vector<Vertex3D> vertices;
  for (int y = 0; y < height - 1; y++) {
    for (int x = 0; x < width - 1; x++) {
      // define patch: 4 points (square)
      Vertex3D vertex1{.pos = glm::vec3(-width / 2.0f + x, _heights[x + width * y], -height / 2.0f + y)};
      vertices.push_back(vertex1);

      Vertex3D vertex2{.pos = glm::vec3(-width / 2.0f + (x + 1), _heights[x + 1 + width * y], -height / 2.0f + y)};
      vertices.push_back(vertex2);

      Vertex3D vertex3{.pos = glm::vec3(-width / 2.0f + x, _heights[x + width * (y + 1)], -height / 2.0f + (y + 1))};
      vertices.push_back(vertex3);

      vertices.push_back(vertex3);
      vertices.push_back(vertex2);

      Vertex3D vertex4{
          .pos = glm::vec3(-width / 2.0f + (x + 1), _heights[x + 1 + width * (y + 1)], -height / 2.0f + (y + 1))};
      vertices.push_back(vertex4);
    }
  }
  _mesh[currentFrame]->setVertices(vertices);

  _numStrips = height - 1;
  _numVertsPerStrip = (width - 1) * 6;
}

void TerrainCPU::_loadTerrain() {
  _cameraBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
    _cameraBuffer[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());

  // layout for Color
  {
    auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/terrainCPU_vertex.spv"));
    shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                              "shaders/terrain/terrainCPU_fragment.spv"));
    auto descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
    descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));
    _descriptorSetLayout.push_back(descriptorSetLayout);

    _descriptorSetColor.resize(_engineState->getSettings()->getMaxFramesInFlight());
    // initialize Color
    {
      std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts;
      for (const auto& layout : _descriptorSetLayout) descriptorSetLayouts.push_back(layout.get());

      RenderGraph::PipelineGraphic pipelineGraphic;
      pipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      pipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      pipelineGraphic.setDepthTest(true);
      pipelineGraphic.setDepthWrite(true);
      pipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
      const auto vertexBinding = _mesh[0]->getBindingDescription();
      const auto vertexAttributes = _mesh[0]->getAttributeDescriptions(
          {{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}});
      const VkPipelineVertexInputStateCreateInfo vertexInput{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
          .vertexBindingDescriptionCount = 1,
          .pVertexBindingDescriptions = &vertexBinding,
          .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
          .pVertexAttributeDescriptions = vertexAttributes.data(),
      };
      _pipeline = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipeline->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts, {}, vertexInput);

      RenderGraph::PipelineGraphic wireframePipelineGraphic;
      wireframePipelineGraphic.setColorAttachments({_engineState->getSettings()->getGraphicColorFormat()});
      wireframePipelineGraphic.setDepthAttachment(_engineState->getSettings()->getDepthFormat());
      wireframePipelineGraphic.setDepthTest(true);
      wireframePipelineGraphic.setDepthWrite(true);
#ifndef __ANDROID__
      wireframePipelineGraphic.setPolygonMode(VK_POLYGON_MODE_LINE);
#endif
      wireframePipelineGraphic.setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
      _pipelineWireframe = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
      _pipelineWireframe->createGraphic(wireframePipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts,
                                        {}, vertexInput);
    }
  }
}

void TerrainCPU::setHeightmap(std::shared_ptr<ImageCPU<uint8_t>> heightMap) {
  auto currentFrame = _engineState->getFrameInFlight();
  _heightMap = heightMap;
  _changeMeshHeightmap[currentFrame] = true;
}

void TerrainCPU::setHeightmap(std::vector<float> heights) {
  auto currentFrame = _engineState->getFrameInFlight();
  _heights = heights;
  _changeMeshTriangles[currentFrame] = true;
}

void TerrainCPU::setDrawType(DrawType drawType) { _drawType = drawType; }

DrawType TerrainCPU::getDrawType() { return _drawType; }

void TerrainCPU::patchEdge(bool enable) { _enableEdge = enable; }

void TerrainCPU::draw(const RenderGraph::CommandBuffer* commandBuffer) {
  const int currentFrame = _engineState->getFrameInFlight();
  if (!_descriptorSetColor[currentFrame]) _updateColorDescriptor(*commandBuffer);
  auto drawTerrain = [&](std::shared_ptr<RenderGraph::Pipeline> pipeline) {
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

    // same buffer to both tessellation shaders because we're not going to change camera between these 2 stages
    BufferMVP cameraUBO{.model = getModel(),
                        .view = _gameState->getCameraManager()->getCurrentCamera()->getView(),
                        .projection = _gameState->getCameraManager()->getCurrentCamera()->getProjection()};

    std::memcpy(static_cast<std::byte*>(_cameraBuffer[currentFrame]->getAllocationInfo().pMappedData), &cameraUBO,
                static_cast<std::size_t>(_cameraBuffer[currentFrame]->getSize()));

    VkBuffer vertexBuffers[] = {_mesh[currentFrame]->getVertexBuffer()->getBuffer()->getBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);
    if (_hasIndexes) {
      VkBuffer indexBuffers = _mesh[currentFrame]->getIndexBuffer()->getBuffer()->getBuffer();
      vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), indexBuffers, 0, VK_INDEX_TYPE_UINT32);
    }

    _descriptorSetColor[currentFrame]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(),
                                            *commandBuffer);

    if (_hasIndexes) {
      for (int strip = 0; strip < _numStrips; ++strip) {
        vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_numVertsPerStrip), 1,
                         static_cast<uint32_t>(strip * _numVertsPerStrip), 0, 0);
      }
    } else {
      for (int i = 0; i < _numStrips; i++)
        vkCmdDraw(commandBuffer->getCommandBuffer(), static_cast<uint32_t>(_numVertsPerStrip), 1,
                  static_cast<uint32_t>(i * _numVertsPerStrip), 0);
    }
  };

  if (!_changeMeshTriangles.empty() && _changeMeshTriangles[_engineState->getFrameInFlight()]) {
    _loadTriangles(currentFrame);
    _changeMeshTriangles[_engineState->getFrameInFlight()] = false;
  }
  if (_changeMeshHeightmap.size() > 0 && _changeMeshHeightmap[_engineState->getFrameInFlight()]) {
    _loadStrip(currentFrame);
    _changeMeshHeightmap[_engineState->getFrameInFlight()] = false;
  }

  auto pipeline = _pipeline;
  if (_drawType == DrawType::WIREFRAME) pipeline = _pipelineWireframe;
  drawTerrain(pipeline);
}

int TerrainDebug::_calculateTileByPosition(glm::vec3 position) {
  auto [width, height] = std::tuple{(_heightMap->getImageView().getImage()).getResolution().x,
                                    (_heightMap->getImageView().getImage()).getResolution().y};
  for (int y = 0; y < _patchNumber.second; y++)
    for (int x = 0; x < _patchNumber.first; x++) {
      // define patch: 4 points (square)
      auto vertex1 = glm::vec3(-width / 2.0f + (width - 1) * x / (float)_patchNumber.first, 0.f,
                               -height / 2.0f + (height - 1) * y / (float)_patchNumber.second);
      vertex1 = getModel() * glm::vec4(vertex1, 1.f);
      auto vertex2 = glm::vec3(-width / 2.0f + (width - 1) * (x + 1) / (float)_patchNumber.first, 0.f,
                               -height / 2.0f + (height - 1) * y / (float)_patchNumber.second);
      vertex2 = getModel() * glm::vec4(vertex2, 1.f);
      auto vertex3 = glm::vec3(-width / 2.0f + (width - 1) * x / (float)_patchNumber.first, 0.f,
                               -height / 2.0f + (height - 1) * (y + 1) / (float)_patchNumber.second);
      vertex3 = getModel() * glm::vec4(vertex3, 1.f);

      if (position.x > vertex1.x && position.x < vertex2.x && position.z > vertex1.z && position.z < vertex3.z)
        return x + y * _patchNumber.first;
    }

  return -1;
}

glm::ivec2 TerrainDebug::_calculatePixelByPosition(glm::vec3 position) {
  auto [width, height] = std::tuple{(_heightMap->getImageView().getImage()).getResolution().x,
                                    (_heightMap->getImageView().getImage()).getResolution().y};

  auto leftTop = getModel() * glm::vec4(-width / 2.0f, 0.f, -height / 2.0f, 1.f);
  auto rightTop = getModel() * glm::vec4(-width / 2.0f + width, 0.f, -height / 2.0f, 1.f);
  auto leftBot = getModel() * glm::vec4(-width / 2.0f, 0.f, -height / 2.0f + height, 1.f);
  auto rightBot = getModel() * glm::vec4(-width / 2.0f + width, 0.f, -height / 2.0f + height, 1.f);
  glm::vec3 terrainSize = rightBot - leftTop;
  glm::vec2 ratio = glm::vec2((glm::vec4(position, 1.f) - leftTop).x / terrainSize.x,
                              (glm::vec4(position, 1.f) - leftTop).z / terrainSize.z);
  return glm::ivec2(std::round(ratio.x * width), std::round(ratio.y * height));
}

void TerrainDebug::_changeHeightmap(glm::ivec2 position, int value) {
  auto [width, height] = _heightMapCPU->getResolution();
  auto data = _heightMapCPU->getData();
  auto index = (position.x + position.y * width) * _heightMapCPU->getChannels();
  int result = data[index] + value;
  result = std::min(result, 255);
  result = std::max(result, 0);
  const auto heightValue = static_cast<uint8_t>(result);
  data[index] = heightValue;
  data[index + 1] = heightValue;
  data[index + 2] = heightValue;
  data[index + 3] = heightValue;
  _heightMapCPU->setData(data);
  _heightMapGPU->setData(_heightMapCPU->getData().get());
}

void TerrainDebug::_calculateMesh(int index) {
  auto [width, height] = std::tuple{(_heightMap->getImageView().getImage()).getResolution().x,
                                    (_heightMap->getImageView().getImage()).getResolution().y};
  std::vector<Vertex3D> vertices;
  glm::vec2 scale = {(float)(width - 1) / _patchNumber.first, (float)(height - 1) / _patchNumber.second};
  glm::vec2 offset = {0.5f, 0.5f};  // to match the center of the pixels
  for (int y = 0; y < _patchNumber.second; y++) {
    for (int x = 0; x < _patchNumber.first; x++) {
      // define patch: 4 points (square)
      Vertex3D vertex1{
          .pos = glm::vec3(-width / 2.0f + (width - 1) * x / (float)_patchNumber.first, 0.f,
                           -height / 2.0f + (height - 1) * y / (float)_patchNumber.second),
          .texCoord = (glm::vec2((float)x, (float)y) * scale + offset) / glm::vec2((float)width, (float)height)};
      vertices.push_back(vertex1);

      Vertex3D vertex2{
          .pos = glm::vec3(-width / 2.0f + (width - 1) * (x + 1) / (float)_patchNumber.first, 0.f,
                           -height / 2.0f + (height - 1) * y / (float)_patchNumber.second),
          .texCoord = (glm::vec2((float)x + 1, (float)y) * scale + offset) / glm::vec2((float)width, (float)height)};
      vertices.push_back(vertex2);

      Vertex3D vertex3{
          .pos = glm::vec3(-width / 2.0f + (width - 1) * x / (float)_patchNumber.first, 0.f,
                           -height / 2.0f + (height - 1) * (y + 1) / (float)_patchNumber.second),
          .texCoord = (glm::vec2((float)x, (float)y + 1) * scale + offset) / glm::vec2((float)width, (float)height)};
      vertices.push_back(vertex3);

      Vertex3D vertex4{.pos = glm::vec3(-width / 2.0f + (width - 1) * (x + 1) / (float)_patchNumber.first, 0.f,
                                        -height / 2.0f + (height - 1) * (y + 1) / (float)_patchNumber.second),
                       .texCoord = (glm::vec2((float)x + 1, (float)y + 1) * scale + offset) /
                                   glm::vec2((float)width, (float)height)};
      vertices.push_back(vertex4);
    }
  }
  _mesh[index]->setVertices(vertices);
}

int TerrainDebug::_saveHeightmap(std::string path) {
  auto assetsPath = _engineState->getSettings()->getAssetsPath();
  path = assetsPath + path;
  auto [width, height] = _heightMapCPU->getResolution();
  int channels = _heightMapCPU->getChannels();
  auto rawData = _heightMapCPU->getData().get();
  int result = stbi_write_png((path + ".png").c_str(), width, height, channels, rawData, width * channels);
  return result;
}

void TerrainDebug::_loadHeightmap(std::string path) {
  auto assetsPath = _engineState->getSettings()->getAssetsPath();
  path = assetsPath + path;
  _heightMapCPU = _gameState->getResourceManager()->getImageManager().loadCPU<uint8_t>(path + ".png");
  _heightMapGPU->setData(_heightMapCPU->getData().get());
  // need to recreate TerrainPhysics because heightmapCPU was updated
  _terrainPhysics->reset(_heightMapCPU);
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) _changedHeightmap[i] = true;
}

void TerrainDebug::setTerrainPhysics(std::shared_ptr<TerrainPhysics> terrainPhysics,
                                     std::shared_ptr<TerrainCPU> terrainCPU) {
  _terrainPhysics = terrainPhysics;
  _terrainCPU = terrainCPU;
}

void TerrainDebug::setTessellationLevel(int min, int max) {
  _minTessellationLevel = min;
  _maxTessellationLevel = max;
}

void TerrainDebug::setTesselationDistance(int min, int max) {
  _minTesselationDistance = static_cast<float>(min);
  _maxTesselationDistance = static_cast<float>(max);
}

void TerrainDebug::setColorHeightLevels(std::array<float, 4> levels) { _heightLevels = levels; }

void TerrainDebug::setHeight(float scale, float shift) {
  _heightScale = scale;
  _heightShift = shift;
}

void TerrainDebug::setMaterial(std::shared_ptr<MaterialColor> material) {
  if (_material) {
    for (const auto& descriptor : _descriptorSetColor) _material->unregisterUpdate(descriptor);
  }
  _material = material;
  std::fill(_changedMaterial.begin(), _changedMaterial.end(), true);
}

void TerrainDebug::setDrawType(DrawType drawType) { _drawType = drawType; }

void TerrainDebug::setTileRotation(int tileID, int angle) { _patchRotationsIndex[tileID] = angle; }

void TerrainDebug::setTileTexture(int tileID, int textureID) { _patchTextures[tileID] = textureID; }

DrawType TerrainDebug::getDrawType() { return _drawType; }

void TerrainDebug::patchEdge(bool enable) { _enableEdge = enable; }

void TerrainDebug::showLoD(bool enable) { _showLoD = enable; }

std::shared_ptr<ImageCPU<uint8_t>> TerrainDebug::getHeightmap() { return _heightMapCPU; }

std::optional<glm::vec3> TerrainDebug::getHitCoords() { return _hitCoords; }

void TerrainGPU::_calculateMesh() {
  auto [width, height] = std::tuple{(_heightMap->getImageView().getImage()).getResolution().x,
                                    (_heightMap->getImageView().getImage()).getResolution().y};
  std::vector<Vertex3D> vertices;
  glm::vec2 scale = {(float)(width - 1) / _patchNumber.first, (float)(height - 1) / _patchNumber.second};
  glm::vec2 offset = {0.5f, 0.5f};  // to match the center of the pixels
  for (int y = 0; y < _patchNumber.second; y++) {
    for (int x = 0; x < _patchNumber.first; x++) {
      // define patch: 4 points (square)
      Vertex3D vertex1{
          .pos = glm::vec3(-width / 2.0f + (width - 1) * x / (float)_patchNumber.first, 0.f,
                           -height / 2.0f + (height - 1) * y / (float)_patchNumber.second),
          .texCoord = (glm::vec2((float)x, (float)y) * scale + offset) / glm::vec2((float)width, (float)height)};
      vertices.push_back(vertex1);

      Vertex3D vertex2{
          .pos = glm::vec3(-width / 2.0f + (width - 1) * (x + 1) / (float)_patchNumber.first, 0.f,
                           -height / 2.0f + (height - 1) * y / (float)_patchNumber.second),
          .texCoord = (glm::vec2((float)x + 1, (float)y) * scale + offset) / glm::vec2((float)width, (float)height)};
      vertices.push_back(vertex2);

      Vertex3D vertex3{
          .pos = glm::vec3(-width / 2.0f + (width - 1) * x / (float)_patchNumber.first, 0.f,
                           -height / 2.0f + (height - 1) * (y + 1) / (float)_patchNumber.second),
          .texCoord = (glm::vec2((float)x, (float)y + 1) * scale + offset) / glm::vec2((float)width, (float)height)};
      vertices.push_back(vertex3);

      Vertex3D vertex4{.pos = glm::vec3(-width / 2.0f + (width - 1) * (x + 1) / (float)_patchNumber.first, 0.f,
                                        -height / 2.0f + (height - 1) * (y + 1) / (float)_patchNumber.second),
                       .texCoord = (glm::vec2((float)x + 1, (float)y + 1) * scale + offset) /
                                   glm::vec2((float)width, (float)height)};
      vertices.push_back(vertex4);
    }
  }
  _mesh->setVertices(vertices);
}

void TerrainGPU::setPatchNumber(int x, int y) {
  _patchNumber.first = x;
  _patchNumber.second = y;
}

void TerrainGPU::setPatchRotations(std::vector<int> patchRotationsIndex) { _patchRotationsIndex = patchRotationsIndex; }

void TerrainGPU::setPatchTextures(std::vector<int> patchTextures) { _patchTextures = patchTextures; }

void TerrainGPU::setTessellationLevel(int min, int max) {
  _minTessellationLevel = min;
  _maxTessellationLevel = max;
}

void TerrainGPU::setTesselationDistance(int min, int max) {
  _minTesselationDistance = static_cast<float>(min);
  _maxTesselationDistance = static_cast<float>(max);
}

void TerrainGPU::setColorHeightLevels(std::array<float, 4> levels) { _heightLevels = levels; }

void TerrainGPU::setHeight(float scale, float shift) {
  _heightScale = scale;
  _heightShift = shift;
}

void TerrainGPU::enableShadow(bool enable) { _enableShadow = enable; }

void TerrainGPU::enableLighting(bool enable) { _enableLighting = enable; }

void TerrainGPU::_unregisterMaterialUpdates() {
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

void TerrainGPU::setMaterial(std::shared_ptr<MaterialColor> material) {
  _unregisterMaterialUpdates();
  _material = material;
  _materialType = MaterialType::COLOR;
  std::fill(_changedMaterial.begin(), _changedMaterial.end(), true);
}

void TerrainGPU::setMaterial(std::shared_ptr<MaterialPhong> material) {
  _unregisterMaterialUpdates();
  _material = material;
  _materialType = MaterialType::PHONG;
  std::fill(_changedMaterial.begin(), _changedMaterial.end(), true);
}

void TerrainGPU::setMaterial(std::shared_ptr<MaterialPBR> material) {
  _unregisterMaterialUpdates();
  _material = material;
  _materialType = MaterialType::PBR;
  std::fill(_changedMaterial.begin(), _changedMaterial.end(), true);
}
