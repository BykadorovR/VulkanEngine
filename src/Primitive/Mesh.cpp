#include <cmath>
#include <numbers>
#include "Primitive/Mesh.h"

AABB::AABB() {
  _min = glm::vec3(std::numeric_limits<float>::max());
  _max = glm::vec3(-std::numeric_limits<float>::max());
}

void AABB::extend(glm::vec3 point) {
  _valid = true;
  for (int i = 0; i < _min.length(); i++) _min[i] = std::min(_min[i], point[i]);
  for (int i = 0; i < _max.length(); i++) _max[i] = std::max(_max[i], point[i]);
}

void AABB::extend(std::shared_ptr<AABB> aabb) {
  _valid = true;
  extend(aabb->getMin());
  extend(aabb->getMax());
}

bool AABB::valid() { return _valid; }

glm::vec3 AABB::getMin() { return _min; }

glm::vec3 AABB::getMax() { return _max; }

Mesh3D::Mesh3D(std::shared_ptr<EngineState> engineState) { _engineState = engineState; }

std::vector<VkVertexInputAttributeDescription> Mesh3D::getAttributeDescriptions(
    const std::vector<std::tuple<VkFormat, std::size_t>>& fields) const {
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions(fields.size());
  for (std::size_t fieldIndex = 0; fieldIndex < fields.size(); fieldIndex++) {
    attributeDescriptions[fieldIndex] = {.location = static_cast<uint32_t>(fieldIndex),
                                         .binding = 0,
                                         .format = std::get<0>(fields[fieldIndex]),
                                         .offset = static_cast<uint32_t>(std::get<1>(fields[fieldIndex]))};
  }
  return attributeDescriptions;
}

void Mesh3D::setAABBPositions(std::shared_ptr<AABB> aabb) { _aabbPositions = aabb; }

void Mesh3D::setAABBJoints(std::map<int, std::shared_ptr<AABB>> aabb) { _aabbJoints = aabb; }

void Mesh3D::setGlobalMatrix(glm::mat4 globalMatrix) { _globalMatrix = globalMatrix; }

const std::vector<uint32_t>& Mesh3D::getIndexData() {
  std::unique_lock<std::mutex> accessLock(_accessIndexMutex);
  return _indexData;
}

const std::vector<Vertex3D>& Mesh3D::getVertexData() {
  std::unique_lock<std::mutex> accessLock(_accessVertexMutex);
  return _vertexData;
}

void Mesh3D::addPrimitive(MeshPrimitive primitive) { _primitives.push_back(primitive); }

const std::vector<MeshPrimitive>& Mesh3D::getPrimitives() { return _primitives; }

std::shared_ptr<AABB> Mesh3D::getAABBPositions() { return _aabbPositions; }

glm::mat4 Mesh3D::getGlobalMatrix() { return _globalMatrix; }

std::map<int, std::shared_ptr<AABB>> Mesh3D::getAABBJoints() { return _aabbJoints; }

MeshStatic3D::MeshStatic3D(std::shared_ptr<EngineState> engineState) : Mesh3D(engineState) {
  _vertexBuffer = std::make_shared<VertexBufferStatic<Vertex3D>>(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, _engineState);
  _indexBuffer = std::make_shared<VertexBufferStatic<uint32_t>>(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, _engineState);
}

void MeshStatic3D::setVertices(std::vector<Vertex3D> vertices) {
  std::unique_lock<std::mutex> accessLock(_accessVertexMutex);
  _vertexData = vertices;
  _vertexBuffer->setData(_vertexData);
}

void MeshStatic3D::setIndexes(std::vector<uint32_t> indexes) {
  std::unique_lock<std::mutex> accessLock(_accessIndexMutex);
  _indexData = indexes;
  _indexBuffer->setData(_indexData);
}

std::shared_ptr<VertexBuffer<Vertex3D>> MeshStatic3D::getVertexBuffer() {
  std::unique_lock<std::mutex> accessLock(_accessVertexMutex);
  return _vertexBuffer;
}

std::shared_ptr<VertexBuffer<uint32_t>> MeshStatic3D::getIndexBuffer() {
  std::unique_lock<std::mutex> accessLock(_accessIndexMutex);
  return _indexBuffer;
}

void MeshStatic3D::setColor(std::vector<glm::vec3> color) {
  std::unique_lock<std::mutex> accessLock(_accessVertexMutex);
  if (color.size() == 0) return;
  if (color.size() > 1 && color.size() != _vertexData.size())
    throw std::invalid_argument("Number of colors doesn't match number of vertices");

  glm::vec3 colorValue = color.front();
  for (int i = 0; i < _vertexData.size(); i++) {
    if (i < color.size()) colorValue = color[i];
    _vertexData[i].color = colorValue;
  }
  _vertexBuffer->setData(_vertexData);
}

void MeshStatic3D::setNormal(std::vector<glm::vec3> normal) {
  std::unique_lock<std::mutex> accessLock(_accessVertexMutex);
  if (normal.size() == 0) return;
  if (normal.size() > 1 && normal.size() != _vertexData.size())
    throw std::invalid_argument("Number of normals doesn't match number of vertices");

  glm::vec3 normalValue = normal.front();
  for (int i = 0; i < _vertexData.size(); i++) {
    if (i < normal.size()) normalValue = normal[i];
    _vertexData[i].normal = normalValue;
  }
  _vertexBuffer->setData(_vertexData);
}

void MeshStatic3D::setPosition(std::vector<glm::vec3> position) {
  if (position.size() == 0) return;
  if (position.size() > 1 && position.size() != _vertexData.size())
    throw std::invalid_argument("Number of positions doesn't match number of vertices");

  glm::vec3 positionValue = position.front();
  for (int i = 0; i < _vertexData.size(); i++) {
    if (i < position.size()) positionValue = position[i];
    _vertexData[i].pos = positionValue;
  }
  _vertexBuffer->setData(_vertexData);
}

VkVertexInputBindingDescription Mesh3D::getBindingDescription() const noexcept {
  VkVertexInputBindingDescription bindingDescription{.binding = 0,
                                                     .stride = sizeof(Vertex3D),
                                                     .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
  return bindingDescription;
}

std::vector<VkVertexInputAttributeDescription> Mesh3D::getAttributeDescriptions() const {
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions{
      {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex3D, pos)},
      {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex3D, normal)},
      {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex3D, color)},
      {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex3D, texCoord)},
      {.location = 4,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32A32_SFLOAT,
       .offset = offsetof(Vertex3D, jointIndices)},
      {.location = 5,
       .binding = 0,
       .format = VK_FORMAT_R32G32B32A32_SFLOAT,
       .offset = offsetof(Vertex3D, jointWeights)},
      {.location = 6, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex3D, tangent)}};

  return attributeDescriptions;
}

MeshDynamic3D::MeshDynamic3D(std::shared_ptr<EngineState> engineState) : Mesh3D(engineState) {
  _vertexBuffer = std::make_shared<VertexBufferDynamic<Vertex3D>>(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, _engineState);
  _indexBuffer = std::make_shared<VertexBufferDynamic<uint32_t>>(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, _engineState);
}

void MeshDynamic3D::setVertices(std::vector<Vertex3D> vertices) {
  _vertexData = vertices;
  _vertexBuffer->setData(_vertexData);
}

void MeshDynamic3D::setIndexes(std::vector<uint32_t> indexes) {
  _indexData = indexes;
  _indexBuffer->setData(_indexData);
}

std::shared_ptr<VertexBuffer<Vertex3D>> MeshDynamic3D::getVertexBuffer() { return _vertexBuffer; }

std::shared_ptr<VertexBuffer<uint32_t>> MeshDynamic3D::getIndexBuffer() { return _indexBuffer; }

void MeshDynamic3D::setColor(std::vector<glm::vec3> color) {
  if (color.size() == 0) return;
  if (color.size() > 1 && color.size() != _vertexData.size())
    throw std::invalid_argument("Number of colors doesn't match number of vertices");

  glm::vec3 colorValue = color.front();
  for (int i = 0; i < _vertexData.size(); i++) {
    if (i < color.size()) colorValue = color[i];
    _vertexData[i].color = colorValue;
  }
  _vertexBuffer->setData(_vertexData);
}

void MeshDynamic3D::setNormal(std::vector<glm::vec3> normal) {
  if (normal.size() == 0) return;
  if (normal.size() > 1 && normal.size() != _vertexData.size())
    throw std::invalid_argument("Number of normals doesn't match number of vertices");

  glm::vec3 normalValue = normal.front();
  for (int i = 0; i < _vertexData.size(); i++) {
    if (i < normal.size()) normalValue = normal[i];
    _vertexData[i].normal = normalValue;
  }
  _vertexBuffer->setData(_vertexData);
}

void MeshDynamic3D::setPosition(std::vector<glm::vec3> position) {
  if (position.size() == 0) return;
  if (position.size() > 1 && position.size() != _vertexData.size())
    throw std::invalid_argument("Number of positions doesn't match number of vertices");

  glm::vec3 positionValue = position.front();
  for (int i = 0; i < _vertexData.size(); i++) {
    if (i < position.size()) positionValue = position[i];
    _vertexData[i].pos = positionValue;
  }
  _vertexBuffer->setData(_vertexData);
}

MeshCube::MeshCube(std::shared_ptr<EngineState> engineState) : MeshStatic3D(engineState) {
  // normal - z, tangent - y, bitangent - x
  std::vector<Vertex3D> vertices{{// 0
                                  .pos = glm::vec3(-0.5, -0.5, 0.5),
                                  .normal = glm::vec3(0.0, -1.0, 0.0),  // down
                                  .tangent = glm::vec4(0.0, 0.0, 1.0, 1.0)},
                                 {.pos = glm::vec3(-0.5, -0.5, 0.5),
                                  .normal = glm::vec3(0.0, 0.0, 1.0),  // front
                                  .tangent = glm::vec4(0.0, 1.0, 0.0, 1.0)},
                                 {.pos = glm::vec3(-0.5, -0.5, 0.5),
                                  .normal = glm::vec3(-1.0, 0.0, 0.0),  // left
                                  .tangent = glm::vec4(0.0, 1.0, 0.0, 1.0)},
                                 // 1
                                 {.pos = glm::vec3(0.5, -0.5, 0.5),
                                  .normal = glm::vec3(0.0, -1.0, 0.0),  // down
                                  .tangent = glm::vec4(0.0, 0.0, 1.0, 1.0)},
                                 {.pos = glm::vec3(0.5, -0.5, 0.5),
                                  .normal = glm::vec3(0.0, 0.0, 1.0),  // front
                                  .tangent = glm::vec4(0.0, 1.0, 0.0, 1.0)},
                                 {.pos = glm::vec3(0.5, -0.5, 0.5),
                                  .normal = glm::vec3(1.0, 0.0, 0.0),  // right
                                  .tangent = glm::vec4(0.0, 1.0, 0.0, 1.0)},
                                 // 2
                                 {.pos = glm::vec3(-0.5, 0.5, 0.5),
                                  .normal = glm::vec3(0.0, 1.0, 0.0),  // top
                                  .tangent = glm::vec4(0.0, 0.0, -1.0, 1.0)},
                                 {.pos = glm::vec3(-0.5, 0.5, 0.5),
                                  .normal = glm::vec3(0.0, 0.0, 1.0),  // front
                                  .tangent = glm::vec4(0.0, 1.0, 0.0, 1.0)},
                                 {.pos = glm::vec3(-0.5, 0.5, 0.5),
                                  .normal = glm::vec3(-1.0, 0.0, 0.0),  // left
                                  .tangent = glm::vec4(0.0, 1.0, 0.0, 1.0)},
                                 // 3
                                 {.pos = glm::vec3(0.5, 0.5, 0.5),
                                  .normal = glm::vec3(0.0, 1.0, 0.0),  // top
                                  .tangent = glm::vec4(0.0, 0.0, -1.0, 1.0)},
                                 {.pos = glm::vec3(0.5, 0.5, 0.5),
                                  .normal = glm::vec3(0.0, 0.0, 1.0),  // front
                                  .tangent = glm::vec4(0.0, 1.0, 0.0, 1.0)},
                                 {.pos = glm::vec3(0.5, 0.5, 0.5),
                                  .normal = glm::vec3(1.0, 0.0, 0.0),  // right
                                  .tangent = glm::vec4(0.0, 1.0, 0.0, 1.0)},
                                 // 4
                                 {.pos = glm::vec3(-0.5, -0.5, -0.5),
                                  .normal = glm::vec3(0.0, -1.0, 0.0),  // down
                                  .tangent = glm::vec4(0.0, 0.0, 1.0, 1.0)},
                                 {.pos = glm::vec3(-0.5, -0.5, -0.5),
                                  .normal = glm::vec3(0.0, 0.0, -1.0),  // back
                                  .tangent = glm::vec4(0.0, -1.0, 0.0, 1.0)},
                                 {.pos = glm::vec3(-0.5, -0.5, -0.5),
                                  .normal = glm::vec3(-1.0, 0.0, 0.0),  // left
                                  .tangent = glm::vec4(0.0, 1.0, 0.0, 1.0)},
                                 // 5
                                 {.pos = glm::vec3(0.5, -0.5, -0.5),
                                  .normal = glm::vec3(0.0, -1.0, 0.0),  // down
                                  .tangent = glm::vec4(0.0, 0.0, 1.0, 1.0)},
                                 {.pos = glm::vec3(0.5, -0.5, -0.5),
                                  .normal = glm::vec3(0.0, 0.0, -1.0),  // back
                                  .tangent = glm::vec4(0.0, -1.0, 0.0, 1.0)},
                                 {.pos = glm::vec3(0.5, -0.5, -0.5),
                                  .normal = glm::vec3(1.0, 0.0, 0.0),  // right
                                  .tangent = glm::vec4(0.0, 1.0, 0.0, 1.0)},
                                 // 6
                                 {.pos = glm::vec3(-0.5, 0.5, -0.5),
                                  .normal = glm::vec3(0.0, 1.0, 0.0),  // top
                                  .tangent = glm::vec4(0.0, 0.0, -1.0, 1.0)},
                                 {.pos = glm::vec3(-0.5, 0.5, -0.5),
                                  .normal = glm::vec3(0.0, 0.0, -1.0),  // back
                                  .tangent = glm::vec4(0.0, -1.0, 0.0, 1.0)},
                                 {.pos = glm::vec3(-0.5, 0.5, -0.5),
                                  .normal = glm::vec3(-1.0, 0.0, 0.0),  // left
                                  .tangent = glm::vec4(0.0, 1.0, 0.0, 1.0)},
                                 // 7
                                 {.pos = glm::vec3(0.5, 0.5, -0.5),
                                  .normal = glm::vec3(0.0, 1.0, 0.0),  // top
                                  .tangent = glm::vec4(0.0, 0.0, -1.0, 1.0)},
                                 {.pos = glm::vec3(0.5, 0.5, -0.5),
                                  .normal = glm::vec3(0.0, 0.0, -1.0),  // back
                                  .tangent = glm::vec4(0.0, -1.0, 0.0, 1.0)},
                                 {.pos = glm::vec3(0.5, 0.5, -0.5),
                                  .normal = glm::vec3(1.0, 0.0, 0.0),  // right
                                  .tangent = glm::vec4(0.0, 1.0, 0.0, 1.0)}};

  std::vector<uint32_t> indices{                          // Bottom
                                0, 12, 15, 15, 3, 0,      // ccw if look to this face from down
                                                          //  Top
                                6, 9, 21, 21, 18, 6,      // ccw if look to this face from up
                                                          //  Left
                                2, 8, 20, 20, 14, 2,      // ccw if look to this face from left
                                                          //  Right
                                5, 17, 23, 23, 11, 5,     // ccw if look to this face from right
                                                          //  Front
                                4, 10, 7, 7, 1, 4,        // ccw if look to this face from front
                                                          //  Back
                                16, 13, 19, 19, 22, 16};  // ccw if look to this face from back
  setVertices(vertices);
  setIndexes(indices);
  setColor(std::vector<glm::vec3>(vertices.size(), glm::vec3(1.f, 1.f, 1.f)));
}

MeshSphere::MeshSphere(std::shared_ptr<EngineState> engineState) : MeshStatic3D(engineState) {
  int radius = 1;
  int sectorCount = 20;
  int stackCount = 20;

  float x, y, z, xz;                            // vertex position
  float nx, ny, nz, lengthInv = 1.0f / radius;  // vertex normal
  float s, t;                                   // vertex texCoord

  constexpr float pi = std::numbers::pi_v<float>;
  float sectorStep = 2.0f * pi / static_cast<float>(sectorCount);
  float stackStep = pi / static_cast<float>(stackCount);
  float sectorAngle, stackAngle;

  std::vector<Vertex3D> vertices;
  for (int i = 0; i <= stackCount; ++i) {
    Vertex3D vertex;
    stackAngle = pi / 2.0f - static_cast<float>(i) * stackStep;  // starting from pi/2 to -pi/2
    xz = radius * cosf(stackAngle);                              // r * cos(u)
    y = radius * sinf(stackAngle);                               // r * sin(u)

    // add (sectorCount+1) vertices per stack
    // first and last vertices have same position and normal, but different tex coords
    for (int j = 0; j <= sectorCount; ++j) {
      sectorAngle = 2.0f * pi -
                    static_cast<float>(j) * sectorStep;  // starting from 2pi to 0, very IMPORTANT for winding order

      // vertex position (x, y, z)
      x = xz * cosf(sectorAngle);  // r * cos(u) * cos(v)
      z = xz * sinf(sectorAngle);  // r * cos(u) * sin(v)
      vertex.pos = glm::vec3(x, y, z);

      // normalized vertex normal (nx, ny, nz)
      nx = x * lengthInv;
      ny = y * lengthInv;
      nz = z * lengthInv;
      vertex.normal = glm::vec3(nx, ny, nz);

      // calculate tangent as dp/du
      // (https://computergraphics.stackexchange.com/questions/5498/compute-sphere-tangent-for-normal-mapping)
      vertex.tangent = glm::vec4(-sinf(sectorAngle), 0.f, cosf(sectorAngle), 1.f);

      // vertex tex coord (s, t) range between [0, 1]
      s = static_cast<float>(j) / sectorCount;
      t = static_cast<float>(i) / stackCount;
      vertex.texCoord = glm::vec2(s, t);
      vertices.push_back(vertex);
    }
  }

  std::vector<uint32_t> indexes;
  int k1, k2;
  for (int i = 0; i < stackCount; ++i) {
    k1 = i * (sectorCount + 1);  // beginning of current stack
    k2 = k1 + sectorCount + 1;   // beginning of next stack

    for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
      // 2 triangles per sector excluding first and last stacks
      // k1 => k2 => k1+1
      if (i != 0) {
        indexes.push_back(k1);
        indexes.push_back(k2);
        indexes.push_back(k1 + 1);
      }

      // k1+1 => k2 => k2+1
      if (i != (stackCount - 1)) {
        indexes.push_back(k1 + 1);
        indexes.push_back(k2);
        indexes.push_back(k2 + 1);
      }
    }
  }
  setVertices(vertices);
  setIndexes(indexes);
  setColor(std::vector<glm::vec3>(vertices.size(), glm::vec3(1.f, 1.f, 1.f)));
}

MeshCapsuleStatic::MeshCapsuleStatic(float height, float radius, std::shared_ptr<EngineState> engineState)
    : MeshStatic3D(engineState) {
  _initialize(height, radius);
}

void MeshCapsuleStatic::_initialize(float height, float radius) {
  int sectorCount = 20;
  int stackCount = 10;

  constexpr float pi = std::numbers::pi_v<float>;
  float sectorStep = 2.0f * pi / static_cast<float>(sectorCount);
  float stackStep = pi / static_cast<float>(stackCount);
  float halfHeight = height * 0.5f;

  std::vector<Vertex3D> vertices;

  // Top hemisphere
  for (int i = 0; i <= stackCount; ++i) {
    float stackAngle = pi / 2.0f - static_cast<float>(i) * stackStep / 2.0f;  // pi/2 - 0
    float xz = radius * cosf(stackAngle);                                     // r * cos(u)
    float y = radius * sinf(stackAngle) + halfHeight;                         // r * sin(u) + half of the height

    for (int j = 0; j <= sectorCount; ++j) {
      Vertex3D vertex;
      float sectorAngle = 2.0f * pi - static_cast<float>(j) * sectorStep;  // very IMPORTANT for winding order

      float x = xz * cosf(sectorAngle);  // r * cos(u) * cos(v)
      float z = xz * sinf(sectorAngle);  // r * cos(u) * sin(v)

      vertex.pos = glm::vec3(x, y, z);
      vertex.normal = glm::normalize(glm::vec3(x, y - halfHeight, z));  // normal of the top hemisphere
      vertex.texCoord = glm::vec2(static_cast<float>(j) / static_cast<float>(sectorCount),
                                  static_cast<float>(i) / static_cast<float>(stackCount));
      vertices.push_back(vertex);
    }
  }

  // Cylinder part
  for (int i = 0; i <= 1; ++i) {
    float y = i == 0 ? halfHeight : -halfHeight;  // top and bottom

    for (int j = 0; j <= sectorCount; ++j) {
      Vertex3D vertex;
      float sectorAngle = 2.0f * pi - static_cast<float>(j) * sectorStep;  // very IMPORTANT for winding order

      float x = radius * cosf(sectorAngle);
      float z = radius * sinf(sectorAngle);

      vertex.pos = glm::vec3(x, y, z);
      vertex.normal = glm::normalize(glm::vec3(x, 0.0f, z));  // cylinder normal
      vertex.texCoord = glm::vec2(static_cast<float>(j) / static_cast<float>(sectorCount), static_cast<float>(i));
      vertices.push_back(vertex);
    }
  }

  // bottom hemisphere
  for (int i = 0; i <= stackCount; ++i) {
    float stackAngle = -pi / 2.0f + static_cast<float>(i) * stackStep / 2.0f;  // -pi/2 - 0
    float xz = radius * cosf(stackAngle);                                      // r * cos(u)
    float y = radius * sinf(stackAngle) - halfHeight;                          // r * sin(u) - half of the height

    for (int j = 0; j <= sectorCount; ++j) {
      Vertex3D vertex;
      float sectorAngle = j * sectorStep;  // very IMPORTANT for winding order

      float x = xz * cosf(sectorAngle);
      float z = xz * sinf(sectorAngle);

      vertex.pos = glm::vec3(x, y, z);
      vertex.normal = glm::normalize(glm::vec3(x, y + halfHeight, z));  // normal of the bottom hemisphere
      vertex.texCoord = glm::vec2(static_cast<float>(j) / static_cast<float>(sectorCount),
                                  static_cast<float>(i) / static_cast<float>(stackCount));
      vertices.push_back(vertex);
    }
  }

  // generate indices
  std::vector<uint32_t> indices;
  // top hemisphere
  int k1, k2;
  for (int i = 0; i < stackCount; ++i) {
    k1 = i * (sectorCount + 1);
    k2 = k1 + sectorCount + 1;

    for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
      indices.push_back(k1);
      indices.push_back(k2);
      indices.push_back(k1 + 1);

      indices.push_back(k1 + 1);
      indices.push_back(k2);
      indices.push_back(k2 + 1);
    }
  }

  // cylinder
  int offset = (stackCount + 1) * (sectorCount + 1);
  for (int i = 0; i < 1; ++i) {
    k1 = offset + i * (sectorCount + 1);
    k2 = k1 + sectorCount + 1;

    for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
      indices.push_back(k1);
      indices.push_back(k2);
      indices.push_back(k1 + 1);

      indices.push_back(k1 + 1);
      indices.push_back(k2);
      indices.push_back(k2 + 1);
    }
  }

  // bottom hemisphere
  offset += 2 * (sectorCount + 1);
  for (int i = 0; i < stackCount; ++i) {
    k1 = offset + i * (sectorCount + 1);
    k2 = k1 + sectorCount + 1;

    for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
      indices.push_back(k1);
      indices.push_back(k2);
      indices.push_back(k1 + 1);

      indices.push_back(k1 + 1);
      indices.push_back(k2);
      indices.push_back(k2 + 1);
    }
  }

  setVertices(vertices);
  setIndexes(indices);
  setColor(std::vector<glm::vec3>(vertices.size(), glm::vec3(1.f, 1.f, 1.f)));
}

void MeshCapsuleStatic::reset(float height, float radius) { _initialize(height, radius); }

MeshCapsuleDynamic::MeshCapsuleDynamic(float height, float radius, std::shared_ptr<EngineState> engineState)
    : MeshDynamic3D(engineState) {
  _initialize(height, radius);
}

void MeshCapsuleDynamic::_initialize(float height, float radius) {
  int sectorCount = 20;
  int stackCount = 10;

  constexpr float pi = std::numbers::pi_v<float>;
  float sectorStep = 2.0f * pi / static_cast<float>(sectorCount);
  float stackStep = pi / static_cast<float>(stackCount);
  float halfHeight = height * 0.5f;

  std::vector<Vertex3D> vertices;

  // Top hemisphere
  for (int i = 0; i <= stackCount; ++i) {
    float stackAngle = pi / 2.0f - static_cast<float>(i) * stackStep / 2.0f;  // pi/2 - 0
    float xz = radius * cosf(stackAngle);                                     // r * cos(u)
    float y = radius * sinf(stackAngle) + halfHeight;                         // r * sin(u) + half of the height

    for (int j = 0; j <= sectorCount; ++j) {
      Vertex3D vertex;
      float sectorAngle = 2.0f * pi - static_cast<float>(j) * sectorStep;  // very IMPORTANT for winding order

      float x = xz * cosf(sectorAngle);  // r * cos(u) * cos(v)
      float z = xz * sinf(sectorAngle);  // r * cos(u) * sin(v)

      vertex.pos = glm::vec3(x, y, z);
      vertex.normal = glm::normalize(glm::vec3(x, y - halfHeight, z));  // normal of the top hemisphere
      vertex.texCoord = glm::vec2(static_cast<float>(j) / static_cast<float>(sectorCount),
                                  static_cast<float>(i) / static_cast<float>(stackCount));
      vertices.push_back(vertex);
    }
  }

  // Cylinder part
  for (int i = 0; i <= 1; ++i) {
    float y = i == 0 ? halfHeight : -halfHeight;  // top and bottom

    for (int j = 0; j <= sectorCount; ++j) {
      Vertex3D vertex;
      float sectorAngle = 2.0f * pi - static_cast<float>(j) * sectorStep;  // very IMPORTANT for winding order

      float x = radius * cosf(sectorAngle);
      float z = radius * sinf(sectorAngle);

      vertex.pos = glm::vec3(x, y, z);
      vertex.normal = glm::normalize(glm::vec3(x, 0.0f, z));  // cylinder normal
      vertex.texCoord = glm::vec2(static_cast<float>(j) / static_cast<float>(sectorCount), static_cast<float>(i));
      vertices.push_back(vertex);
    }
  }

  // bottom hemisphere
  for (int i = 0; i <= stackCount; ++i) {
    float stackAngle = -pi / 2.0f + static_cast<float>(i) * stackStep / 2.0f;  // -pi/2 - 0
    float xz = radius * cosf(stackAngle);                                      // r * cos(u)
    float y = radius * sinf(stackAngle) - halfHeight;                          // r * sin(u) - half of the height

    for (int j = 0; j <= sectorCount; ++j) {
      Vertex3D vertex;
      float sectorAngle = j * sectorStep;  // very IMPORTANT for winding order

      float x = xz * cosf(sectorAngle);
      float z = xz * sinf(sectorAngle);

      vertex.pos = glm::vec3(x, y, z);
      vertex.normal = glm::normalize(glm::vec3(x, y + halfHeight, z));  // normal of the bottom hemisphere
      vertex.texCoord = glm::vec2(static_cast<float>(j) / static_cast<float>(sectorCount),
                                  static_cast<float>(i) / static_cast<float>(stackCount));
      vertices.push_back(vertex);
    }
  }

  // generate indices
  std::vector<uint32_t> indices;
  // top hemisphere
  int k1, k2;
  for (int i = 0; i < stackCount; ++i) {
    k1 = i * (sectorCount + 1);
    k2 = k1 + sectorCount + 1;

    for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
      indices.push_back(k1);
      indices.push_back(k2);
      indices.push_back(k1 + 1);

      indices.push_back(k1 + 1);
      indices.push_back(k2);
      indices.push_back(k2 + 1);
    }
  }

  // cylinder
  int offset = (stackCount + 1) * (sectorCount + 1);
  for (int i = 0; i < 1; ++i) {
    k1 = offset + i * (sectorCount + 1);
    k2 = k1 + sectorCount + 1;

    for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
      indices.push_back(k1);
      indices.push_back(k2);
      indices.push_back(k1 + 1);

      indices.push_back(k1 + 1);
      indices.push_back(k2);
      indices.push_back(k2 + 1);
    }
  }

  // bottom hemisphere
  offset += 2 * (sectorCount + 1);
  for (int i = 0; i < stackCount; ++i) {
    k1 = offset + i * (sectorCount + 1);
    k2 = k1 + sectorCount + 1;

    for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
      indices.push_back(k1);
      indices.push_back(k2);
      indices.push_back(k1 + 1);

      indices.push_back(k1 + 1);
      indices.push_back(k2);
      indices.push_back(k2 + 1);
    }
  }

  setVertices(vertices);
  setIndexes(indices);
  setColor(std::vector<glm::vec3>(vertices.size(), glm::vec3(1.f, 1.f, 1.f)));
}

void MeshCapsuleDynamic::reset(float height, float radius) { _initialize(height, radius); }

Mesh2D::Mesh2D(std::shared_ptr<EngineState> engineState) { _engineState = engineState; }

std::vector<VkVertexInputAttributeDescription> Mesh2D::getAttributeDescriptions(
    const std::vector<std::tuple<VkFormat, std::size_t>>& fields) const {
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions(fields.size());
  for (std::size_t fieldIndex = 0; fieldIndex < fields.size(); fieldIndex++) {
    attributeDescriptions[fieldIndex] = {.location = static_cast<uint32_t>(fieldIndex),
                                         .binding = 0,
                                         .format = std::get<0>(fields[fieldIndex]),
                                         .offset = static_cast<uint32_t>(std::get<1>(fields[fieldIndex]))};
  }
  return attributeDescriptions;
}

const std::vector<uint32_t>& Mesh2D::getIndexData() {
  std::unique_lock<std::mutex> accessLock(_accessIndexMutex);
  return _indexData;
}

const std::vector<Vertex2D>& Mesh2D::getVertexData() {
  std::unique_lock<std::mutex> accessLock(_accessVertexMutex);
  return _vertexData;
}

MeshStatic2D::MeshStatic2D(std::shared_ptr<EngineState> engineState) : Mesh2D(engineState) {
  _vertexBuffer = std::make_shared<VertexBufferStatic<Vertex2D>>(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, _engineState);
  _indexBuffer = std::make_shared<VertexBufferStatic<uint32_t>>(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, _engineState);
}

void MeshStatic2D::setVertices(std::vector<Vertex2D> vertices) {
  std::unique_lock<std::mutex> accessLock(_accessVertexMutex);
  _vertexData = vertices;
  _vertexBuffer->setData(vertices);
}

void MeshStatic2D::setIndexes(std::vector<uint32_t> indexes) {
  std::unique_lock<std::mutex> accessLock(_accessIndexMutex);
  _indexData = indexes;
  _indexBuffer->setData(indexes);
}

void MeshStatic2D::setColor(glm::vec3 color) {
  std::unique_lock<std::mutex> accessLock(_accessVertexMutex);
  for (auto& vertex : _vertexData) {
    vertex.color = color;
  }
  _vertexBuffer->setData(_vertexData);
}

void MeshStatic2D::setNormal(glm::vec3 normal) {
  std::unique_lock<std::mutex> accessLock(_accessVertexMutex);
  for (auto& vertex : _vertexData) {
    vertex.normal = normal;
  }
  _vertexBuffer->setData(_vertexData);
}

std::shared_ptr<VertexBuffer<Vertex2D>> MeshStatic2D::getVertexBuffer() {
  std::unique_lock<std::mutex> accessLock(_accessVertexMutex);
  return _vertexBuffer;
}

std::shared_ptr<VertexBuffer<uint32_t>> MeshStatic2D::getIndexBuffer() {
  std::unique_lock<std::mutex> accessLock(_accessIndexMutex);
  return _indexBuffer;
}

VkVertexInputBindingDescription Mesh2D::getBindingDescription() const noexcept {
  VkVertexInputBindingDescription bindingDescription{.binding = 0,
                                                     .stride = sizeof(Vertex2D),
                                                     .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};

  return bindingDescription;
}

std::vector<VkVertexInputAttributeDescription> Mesh2D::getAttributeDescriptions() const {
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions{
      {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex2D, pos)},
      {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex2D, normal)},
      {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex2D, color)},
      {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex2D, texCoord)},
      {.location = 4, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex2D, tangent)}};

  return attributeDescriptions;
}
