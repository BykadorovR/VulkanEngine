#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <tuple>
#include <vector>
#include "Utility/EngineState.h"
#include "Primitive/Mesh.h"
#include "Primitive/Cubemap.h"

import Buffer;
import Command;
import DescriptorBuffer;
import Graph;
import Pipeline;
import Texture;

class BlurSeparate : public RenderGraph::GraphElement {
 protected:
  std::shared_ptr<EngineState> _engineState;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSet>> _descriptorSet;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _descriptorSetLayout;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _blurWeightsSSBO;
  std::vector<std::shared_ptr<RenderGraph::Texture>> _textureSrc;
  std::vector<std::shared_ptr<RenderGraph::Texture>> _textureDst;
  bool _horizontal;
  std::vector<float> _blurWeights;
  std::vector<bool> _changed;
  int _kernelSize = 15;
  float _sigma = _kernelSize / 3.0f;
  int _layer;

  void _updateWeights();
  void _updateDescriptors(int currentFrame);

 public:
  BlurSeparate(bool horizontal,
               std::vector<std::shared_ptr<RenderGraph::Texture>> src,
               std::vector<std::shared_ptr<RenderGraph::Texture>> dst,
               std::shared_ptr<EngineState> engineState,
               int layer = -1);
  void update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& swapchain) override;
  std::vector<std::shared_ptr<RenderGraph::Texture>> getTextureSrc();
  std::vector<std::shared_ptr<RenderGraph::Texture>> getTextureDst();
  bool getHorizontal();
  virtual ~BlurSeparate() = default;
};

class BlurComputeSeparate : public BlurSeparate {
 private:
  std::shared_ptr<RenderGraph::Pipeline> _pipeline;

  void _initialize(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer);

 public:
  BlurComputeSeparate(bool horizontal,
                      std::vector<std::shared_ptr<RenderGraph::Texture>> src,
                      std::vector<std::shared_ptr<RenderGraph::Texture>> dst,
                      std::shared_ptr<EngineState> engineState,
                      int layer = -1);
  void draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  ~BlurComputeSeparate() = default;
};

class BlurGraphicSeparate : public BlurSeparate {
 private:
  std::shared_ptr<RenderGraph::Pipeline> _pipeline;
  std::shared_ptr<MeshStatic2D> _mesh;
  std::tuple<int, int> _resolution;

  void _initialize(std::vector<std::shared_ptr<RenderGraph::Texture>> src,
                   const RenderGraph::CommandBuffer& commandBuffer);

 public:
  BlurGraphicSeparate(bool horizontal,
                      std::vector<std::shared_ptr<RenderGraph::Texture>> src,
                      std::vector<std::shared_ptr<RenderGraph::Texture>> dst,
                      const RenderGraph::CommandBuffer* commandBufferTransfer,
                      std::shared_ptr<EngineState> engineState,
                      int layer = -1);
  void draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  ~BlurGraphicSeparate() = default;
};
