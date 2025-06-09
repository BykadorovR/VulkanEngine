#pragma once
#include "Vulkan/Descriptor.h"
#include "Vulkan/Pipeline.h"
#include "Utility/EngineState.h"
#include "Utility/GameState.h"
#include "Graphic/Camera.h"
#include "Primitive/Mesh.h"
#include "Primitive/Drawable.h"

class Line : public Drawable {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<GameState> _gameState;
  std::shared_ptr<Buffer> _stagingBuffer;
  std::shared_ptr<MeshDynamic3D> _mesh;
  std::vector<std::shared_ptr<Buffer>> _cameraBuffer;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSetCamera;
  std::shared_ptr<PipelineGraphic> _pipeline;
  glm::mat4 _model = glm::mat4(1.f);
  bool _changed = false;

 public:
  Line(std::shared_ptr<CommandBuffer> commandBufferTransfer,
       std::shared_ptr<GameState> gameState,
       std::shared_ptr<EngineState> engineState);
  std::shared_ptr<MeshDynamic3D> getMesh();
  void setModel(glm::mat4 model);

  void draw(std::shared_ptr<CommandBuffer> commandBuffer) override;
};