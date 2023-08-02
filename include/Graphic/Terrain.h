#pragma once
#include "State.h"
#include "Camera.h"

class Terrain {
 protected:
  std::shared_ptr<Camera> _camera;
  glm::mat4 _model = glm::mat4(1.f);

 public:
  void setModel(glm::mat4 model);
  void setCamera(std::shared_ptr<Camera> camera);

  virtual void draw(int currentFrame, std::shared_ptr<CommandBuffer> commandBuffer, bool normals = false) = 0;
};

class TerrainCPU : public Terrain {
 private:
  std::shared_ptr<State> _state;
  std::shared_ptr<VertexBuffer<Vertex3D>> _vertexBuffer;
  std::shared_ptr<VertexBuffer<uint32_t>> _indexBuffer;
  std::shared_ptr<UniformBuffer> _uniformBuffer;
  std::shared_ptr<DescriptorSet> _descriptorSetCamera;
  std::shared_ptr<Pipeline> _pipeline;
  std::vector<uint32_t> _indices;
  int _numStrips, _numVertsPerStrip;

 public:
  TerrainCPU(std::shared_ptr<CommandBuffer> commandBufferTransfer, std::shared_ptr<State> state);

  void draw(int currentFrame, std::shared_ptr<CommandBuffer> commandBuffer, bool normals = false) override;
};

class TerrainGPU : public Terrain {
 private:
  std::shared_ptr<State> _state;
  int _vertexNumber;
  std::shared_ptr<VertexBuffer<Vertex3D>> _vertexBuffer;
  std::shared_ptr<VertexBuffer<uint32_t>> _indexBuffer;
  std::shared_ptr<UniformBuffer> _cameraBuffer;
  std::shared_ptr<DescriptorSet> _descriptorSetCameraControl, _descriptorSetCameraEvaluation,
      _descriptorSetCameraGeometry, _descriptorSetHeight, _descriptorSetTerrainTiles;
  std::shared_ptr<Pipeline> _pipeline, _pipelineNormal;
  std::shared_ptr<Texture> _heightMap;
  std::array<std::shared_ptr<Texture>, 4> _terrainTiles;
  std::pair<int, int> _patchNumber;
  float _heightScale = 64.f;
  float _heightShift = 16.f;
  float _heightLevels[4] = {16, 128, 192, 256};
  // TODO: work very strange, don't use not equal values
  int _minTessellationLevel = 4, _maxTessellationLevel = 32;
  float _minDistance = 0.1, _maxDistance = 30;

 public:
  TerrainGPU(std::pair<int, int> patchNumber,
             std::shared_ptr<CommandBuffer> commandBufferTransfer,
             std::shared_ptr<State> state);
  void draw(int currentFrame, std::shared_ptr<CommandBuffer> commandBuffer, bool normals = false) override;
};