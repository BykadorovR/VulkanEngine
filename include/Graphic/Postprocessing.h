#pragma once
#include <memory>
#include <vector>
#include "Utility/EngineState.h"

import Command;
import DescriptorBuffer;
import Graph;
import Pipeline;
import Swapchain;
import Texture;

class Postprocessing : public RenderGraph::GraphElement {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<RenderGraph::Pipeline> _computePipeline;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _textureLayout;
  std::vector<std::vector<std::shared_ptr<RenderGraph::DescriptorSet>>> _descriptorSets;
  std::vector<std::shared_ptr<RenderGraph::Texture>> _sourceTextures;
  std::vector<std::shared_ptr<RenderGraph::Texture>> _destinationTextures;
  RenderGraph::Swapchain* _swapchain;
  float _gamma = 2.2f;
  float _exposure = 1.f;

  void _initialize(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& source,
                   const std::vector<std::shared_ptr<RenderGraph::ImageView>>& destination,
                   const RenderGraph::CommandBuffer& commandBuffer);

 public:
  Postprocessing(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& source,
                 const std::vector<std::shared_ptr<RenderGraph::ImageView>>& destination,
                 const RenderGraph::CommandBuffer& commandBuffer,
                 std::shared_ptr<EngineState> engineState,
                 RenderGraph::Swapchain& swapchain);
  void reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& source,
             const std::vector<std::shared_ptr<RenderGraph::ImageView>>& destination,
             const RenderGraph::CommandBuffer& commandBuffer);
  void setExposure(float exposure);
  void setGamma(float gamma);
  float getGamma();
  float getExposure();

  void draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& swapchain) override;
};
