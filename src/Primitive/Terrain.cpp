#include "Primitive/Terrain.h"
#undef far
#undef near
#include "stb_image_write.h"
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <nlohmann/json.hpp>

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
  JPH::HeightFieldShapeSettings settingsTerrain(_terrainPhysic.data(),
                                                JPH::Vec3(0.f, -std::get<1>(_heightScaleOffset), 0.f),
                                                JPH::Vec3(1.f, std::get<0>(_heightScaleOffset), 1.f), w);

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
  int w = std::get<0>(_resolution);
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

void TerrainCPU::_updateColorDescriptor() {
  std::vector<VkDescriptorImageInfo> colorImageInfo(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    std::map<int, std::vector<VkDescriptorBufferInfo>> bufferInfoColor{
        {0, {{.buffer = _cameraBuffer[i]->getData(), .offset = 0, .range = _cameraBuffer[i]->getSize()}}}};
    _descriptorSetColor[i]->createCustom(bufferInfoColor, {});
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
  for (unsigned int i = 0; i < height; i++) {
    for (unsigned int j = 0; j < width; j++) {
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
  for (unsigned int i = 0; i < height - 1; i++) {  // for each row a.k.a. each strip
    for (unsigned int j = 0; j < width; j++) {     // for each column
      for (unsigned int k = 0; k < 2; k++) {       // for each side of the strip
        indices.push_back(j + width * (i + k));
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
    _cameraBuffer[i] = std::make_shared<Buffer>(
        sizeof(BufferMVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _engineState);

  // layout for Color
  {
    auto descriptorSetLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
    std::vector<VkDescriptorSetLayoutBinding> layoutColor{{.binding = 0,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                           .descriptorCount = 1,
                                                           .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                                           .pImmutableSamplers = nullptr}};
    descriptorSetLayout->createCustom(layoutColor);
    _descriptorSetLayout.push_back({"color", descriptorSetLayout});

    _descriptorSetColor.resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++)
      _descriptorSetColor[i] = std::make_shared<DescriptorSet>(descriptorSetLayout, _engineState);

    // update descriptor set in setMaterial
    _updateColorDescriptor();

    // initialize Color
    {
      auto shader = std::make_shared<Shader>(_engineState);
      shader->add("shaders/terrain/terrainCPU_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
      shader->add("shaders/terrain/terrainCPU_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
      _pipeline = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(), _engineState->getDevice());
      _pipeline->setDepthTest(true);
      _pipeline->setDepthWrite(true);
      _pipeline->setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
      _pipeline->createCustom(
          shader, _descriptorSetLayout, {}, _mesh[0]->getBindingDescription(),
          _mesh[0]->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}}),
          RenderPassScenario::GRAPHIC);

      _pipelineWireframe = std::make_shared<PipelineGraphic>(_engineState->getRenderPassManager(),
                                                             _engineState->getDevice());
      _pipelineWireframe->setDepthTest(true);
      _pipelineWireframe->setDepthWrite(true);
      _pipelineWireframe->setPolygonMode(VK_POLYGON_MODE_LINE);
      _pipelineWireframe->setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
      _pipelineWireframe->createCustom(
          shader, _descriptorSetLayout, {}, _mesh[0]->getBindingDescription(),
          _mesh[0]->Mesh3D::getAttributeDescriptions({{VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos)}}),
          RenderPassScenario::GRAPHIC);
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

void TerrainCPU::draw(std::shared_ptr<CommandBuffer> commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  auto drawTerrain = [&](std::shared_ptr<Pipeline> pipeline) {
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

    _cameraBuffer[currentFrame]->setData(&cameraUBO);

    VkBuffer vertexBuffers[] = {_mesh[currentFrame]->getVertexBuffer()->getBuffer()->getData()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBuffer(), 0, 1, vertexBuffers, offsets);
    if (_hasIndexes) {
      VkBuffer indexBuffers = _mesh[currentFrame]->getIndexBuffer()->getBuffer()->getData();
      vkCmdBindIndexBuffer(commandBuffer->getCommandBuffer(), indexBuffers, 0, VK_INDEX_TYPE_UINT32);
    }

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

    if (_hasIndexes) {
      for (unsigned int strip = 0; strip < _numStrips; ++strip) {
        vkCmdDrawIndexed(commandBuffer->getCommandBuffer(), _numVertsPerStrip, 1, strip * _numVertsPerStrip, 0, 0);
      }
    } else {
      for (int i = 0; i < _numStrips; i++)
        vkCmdDraw(commandBuffer->getCommandBuffer(), _numVertsPerStrip, 1, i * _numVertsPerStrip, 0);
    }
  };

  if (_changeMeshTriangles.size() > 0 && _changeMeshTriangles[_engineState->getFrameInFlight()]) {
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
  auto [width, height] = _heightMap->getImageView()->getImage()->getResolution();
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
  auto [width, height] = _heightMap->getImageView()->getImage()->getResolution();

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
  data[index] = result;
  data[index + 1] = result;
  data[index + 2] = result;
  data[index + 3] = result;
  _heightMapCPU->setData(data);
  _heightMapGPU->setData(_heightMapCPU->getData().get());
}

void TerrainDebug::_calculateMesh(int index) {
  auto [width, height] = _heightMap->getImageView()->getImage()->getResolution();
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
  _heightMapCPU = _gameState->getResourceManager()->loadImageCPU<uint8_t>(path + ".png");
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
  _minTesselationDistance = min;
  _maxTesselationDistance = max;
}

void TerrainDebug::setColorHeightLevels(std::array<float, 4> levels) { _heightLevels = levels; }

void TerrainDebug::setHeight(float scale, float shift) {
  _heightScale = scale;
  _heightShift = shift;
}

void TerrainDebug::setMaterial(std::shared_ptr<MaterialColor> material) {
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    if (_material) {
      _material->unregisterUpdate(_descriptorSetColor[i]);
    }
    material->registerUpdate(_descriptorSetColor[i],
                             {._frameInFlight = i, ._materialInfo = {{MaterialTexture::COLOR, 4}}});
  }
  _material = material;
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void TerrainDebug::setDrawType(DrawType drawType) { _drawType = drawType; }

void TerrainDebug::setTileRotation(int tileID, int angle) { _patchRotationsIndex[tileID] = angle; }

void TerrainDebug::setTileTexture(int tileID, int textureID) { _patchTextures[tileID] = textureID; }

DrawType TerrainDebug::getDrawType() { return _drawType; }

void TerrainDebug::patchEdge(bool enable) { _enableEdge = enable; }

void TerrainDebug::showLoD(bool enable) { _showLoD = enable; }

std::shared_ptr<ImageCPU<uint8_t>> TerrainDebug::getHeightmap() { return _heightMapCPU; }

std::optional<glm::vec3> TerrainDebug::getHitCoords() { return _hitCoords; }

void TerrainGPU::_calculateMesh(std::shared_ptr<CommandBuffer> commandBuffer) {
  auto [width, height] = _heightMap->getImageView()->getImage()->getResolution();
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
  _mesh->setVertices(vertices, commandBuffer);
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
  _minTesselationDistance = min;
  _maxTesselationDistance = max;
}

void TerrainGPU::setColorHeightLevels(std::array<float, 4> levels) { _heightLevels = levels; }

void TerrainGPU::setHeight(float scale, float shift) {
  _heightScale = scale;
  _heightShift = shift;
}

void TerrainGPU::enableShadow(bool enable) { _enableShadow = enable; }

void TerrainGPU::enableLighting(bool enable) { _enableLighting = enable; }

void TerrainGPU::setMaterial(std::shared_ptr<MaterialColor> material) {
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    if (_material) {
      _material->unregisterUpdate(_descriptorSetColor[i]);
    }
    material->registerUpdate(_descriptorSetColor[i],
                             {._frameInFlight = i, ._materialInfo = {{MaterialTexture::COLOR, 4}}});
  }
  _material = material;
  _materialType = MaterialType::COLOR;
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void TerrainGPU::setMaterial(std::shared_ptr<MaterialPhong> material) {
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    if (_material) _material->unregisterUpdate(_descriptorSetPhong[i]);
    material->registerUpdate(
        _descriptorSetPhong[i],
        {._frameInFlight = i,
         ._materialInfo = {{MaterialTexture::COLOR, 4}, {MaterialTexture::NORMAL, 5}, {MaterialTexture::SPECULAR, 6}}});
  }
  _material = material;
  _materialType = MaterialType::PHONG;
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}

void TerrainGPU::setMaterial(std::shared_ptr<MaterialPBR> material) {
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    if (_material) _material->unregisterUpdate(_descriptorSetPBR[i]);
    material->registerUpdate(_descriptorSetPBR[i], {._frameInFlight = i,
                                                    ._materialInfo = {{MaterialTexture::COLOR, 4},
                                                                      {MaterialTexture::NORMAL, 5},
                                                                      {MaterialTexture::METALLIC, 6},
                                                                      {MaterialTexture::ROUGHNESS, 7},
                                                                      {MaterialTexture::OCCLUSION, 8},
                                                                      {MaterialTexture::EMISSIVE, 9},
                                                                      {MaterialTexture::IBL_DIFFUSE, 10},
                                                                      {MaterialTexture::IBL_SPECULAR, 11},
                                                                      {MaterialTexture::BRDF_SPECULAR, 12}}});
  }
  _material = material;
  _materialType = MaterialType::PBR;
  for (int i = 0; i < _changedMaterial.size(); i++) {
    _changedMaterial[i] = true;
  }
}