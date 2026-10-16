#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>
#include "Graphic/Buffer.h"
#include "Utility/EngineState.h"

struct MeshPrimitive {
  int firstIndex;
  int indexCount;
  int materialIndex;
};

struct Vertex2D {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec3 color;
  glm::vec2 texCoord;
  glm::vec4 tangent;
};

struct Vertex3D {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec3 color;
  glm::vec2 texCoord;
  glm::vec4 jointIndices;
  glm::vec4 jointWeights;
  glm::vec4 tangent;
};

class AABB {
 private:
  glm::vec3 _min;
  glm::vec3 _max;
  bool _valid = false;

 public:
  AABB();
  void extend(glm::vec3 point);
  void extend(std::shared_ptr<AABB> aabb);
  bool valid();
  glm::vec3 getMin();
  glm::vec3 getMax();
};

class Mesh3D {
 protected:
  std::mutex _accessVertexMutex, _accessIndexMutex;
  std::shared_ptr<EngineState> _engineState;
  std::vector<uint32_t> _indexData;
  std::vector<Vertex3D> _vertexData;
  std::vector<MeshPrimitive> _primitives;
  std::shared_ptr<AABB> _aabbPositions;
  std::map<int, std::shared_ptr<AABB>> _aabbJoints;
  // special Node matrix for mesh node
  glm::mat4 _globalMatrix;

 public:
  Mesh3D(std::shared_ptr<EngineState> engineState);

  void setAABBPositions(std::shared_ptr<AABB> aabb);
  void setAABBJoints(std::map<int, std::shared_ptr<AABB>> aabb);
  void setGlobalMatrix(glm::mat4 globalMatrix);
  void addPrimitive(MeshPrimitive primitive);

  const std::vector<uint32_t>& getIndexData();
  const std::vector<Vertex3D>& getVertexData();
  const std::vector<MeshPrimitive>& getPrimitives();
  std::shared_ptr<AABB> getAABBPositions();
  glm::mat4 getGlobalMatrix();
  std::map<int, std::shared_ptr<AABB>> getAABBJoints();
  // custom fields
  std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions(
      const std::vector<std::tuple<VkFormat, std::size_t>>& fields) const;
  VkVertexInputBindingDescription getBindingDescription() const noexcept;
  // all fields
  std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() const;
  virtual std::shared_ptr<VertexBuffer<Vertex3D>> getVertexBuffer() = 0;
  virtual std::shared_ptr<VertexBuffer<uint32_t>> getIndexBuffer() = 0;
};

class MeshDynamic3D : public Mesh3D {
 private:
  std::shared_ptr<VertexBufferDynamic<Vertex3D>> _vertexBuffer;
  std::shared_ptr<VertexBufferDynamic<uint32_t>> _indexBuffer;

 public:
  MeshDynamic3D(std::shared_ptr<EngineState> engineState);

  void setVertices(std::vector<Vertex3D> vertices);
  void setIndexes(std::vector<uint32_t> indexes);
  void setColor(std::vector<glm::vec3> color);
  void setNormal(std::vector<glm::vec3> normal);
  void setPosition(std::vector<glm::vec3> position);

  std::shared_ptr<VertexBuffer<Vertex3D>> getVertexBuffer() override;
  std::shared_ptr<VertexBuffer<uint32_t>> getIndexBuffer() override;
};

class MeshStatic3D : public Mesh3D {
 private:
  std::shared_ptr<VertexBufferStatic<Vertex3D>> _vertexBuffer;
  std::shared_ptr<VertexBufferStatic<uint32_t>> _indexBuffer;

 public:
  MeshStatic3D(std::shared_ptr<EngineState> engineState);

  void setVertices(std::vector<Vertex3D> vertices);
  void setIndexes(std::vector<uint32_t> indexes);
  void setColor(std::vector<glm::vec3> color);
  void setNormal(std::vector<glm::vec3> normal);
  void setPosition(std::vector<glm::vec3> position);

  std::shared_ptr<VertexBuffer<Vertex3D>> getVertexBuffer() override;
  std::shared_ptr<VertexBuffer<uint32_t>> getIndexBuffer() override;
};

class MeshCube : public MeshStatic3D {
 public:
  explicit MeshCube(std::shared_ptr<EngineState> engineState);
};

class MeshSphere : public MeshStatic3D {
 public:
  explicit MeshSphere(std::shared_ptr<EngineState> engineState);
};

class MeshCapsuleStatic : public MeshStatic3D {
 private:
  void _initialize(float height, float radius);

 public:
  MeshCapsuleStatic(float height, float radius, std::shared_ptr<EngineState> engineState);
  void reset(float height, float radius);
};

class MeshCapsuleDynamic : public MeshDynamic3D {
 private:
  void _initialize(float height, float radius);

 public:
  MeshCapsuleDynamic(float height, float radius, std::shared_ptr<EngineState> engineState);
  void reset(float height, float radius);
};

class Mesh2D {
 protected:
  std::mutex _accessVertexMutex, _accessIndexMutex;
  std::shared_ptr<EngineState> _engineState;
  std::vector<uint32_t> _indexData;
  std::vector<Vertex2D> _vertexData;

 public:
  Mesh2D(std::shared_ptr<EngineState> engineState);

  // custom fields
  std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions(
      const std::vector<std::tuple<VkFormat, std::size_t>>& fields) const;
  VkVertexInputBindingDescription getBindingDescription() const noexcept;
  std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() const;
  const std::vector<uint32_t>& getIndexData();
  const std::vector<Vertex2D>& getVertexData();
  virtual std::shared_ptr<VertexBuffer<Vertex2D>> getVertexBuffer() = 0;
  virtual std::shared_ptr<VertexBuffer<uint32_t>> getIndexBuffer() = 0;
};

class MeshStatic2D : public Mesh2D {
 private:
  std::shared_ptr<VertexBufferStatic<Vertex2D>> _vertexBuffer;
  std::shared_ptr<VertexBufferStatic<uint32_t>> _indexBuffer;

 public:
  MeshStatic2D(std::shared_ptr<EngineState> engineState);

  void setVertices(std::vector<Vertex2D> vertices);
  void setIndexes(std::vector<uint32_t> indexes);
  void setColor(glm::vec3 color);
  void setNormal(glm::vec3 normal);

  std::shared_ptr<VertexBuffer<Vertex2D>> getVertexBuffer() override;
  std::shared_ptr<VertexBuffer<uint32_t>> getIndexBuffer() override;
};
