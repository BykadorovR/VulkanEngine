#pragma once
#include "Utility/EngineState.h"
#include "Graphic/Texture.h"
#include "Vulkan/Pipeline.h"

class Postprocessing {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<PipelineCompute> _computePipeline;
  std::shared_ptr<DescriptorSetLayout> _textureLayout;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSet;
  float _gamma = 2.2f;
  float _exposure = 1.f;

  void _initialize(std::vector<std::shared_ptr<ImageView>> src);

 public:
  Postprocessing(std::vector<std::shared_ptr<ImageView>> src, std::shared_ptr<EngineState> engineState);
  void reset(std::vector<std::shared_ptr<ImageView>> src);
  void setExposure(float exposure);
  void setGamma(float gamma);
  float getGamma();
  float getExposure();

  void drawCompute(int swapchainIndex, std::shared_ptr<CommandBuffer> commandBuffer);
};