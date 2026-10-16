#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include "Primitive/Drawable.h"
#include "Utility/EngineState.h"
#include "Utility/GameState.h"
#include "Graphic/Camera.h"
#include "Primitive/Mesh.h"

import Buffer;
import Command;
import DescriptorBuffer;
import Pipeline;

class Line : public Drawable {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<GameState> _gameState;
  std::shared_ptr<MeshDynamic3D> _mesh;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _cameraBuffer;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _descriptorSetLayoutCamera;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSet>> _descriptorSetCamera;
  std::shared_ptr<RenderGraph::Pipeline> _pipeline;
  glm::mat4 _model = glm::mat4(1.f);

 public:
  Line(const RenderGraph::CommandBuffer* commandBufferTransfer,
       std::shared_ptr<GameState> gameState,
       std::shared_ptr<EngineState> engineState);
  std::shared_ptr<MeshDynamic3D> getMesh();
  void setModel(glm::mat4 model);

  void draw(const RenderGraph::CommandBuffer* commandBuffer) override;
};
