#pragma once
#include "Utility/EngineState.h"
#include "Graphic/Texture.h"
#include "Vulkan/Pipeline.h"

class Postprocessing {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<PipelineCompute> _computePipeline;
  std::shared_ptr<DescriptorSetLayout> _textureLayout;
  std::map<std::pair<int, int>, std::shared_ptr<DescriptorSet>> _descriptorSet;
  float _gamma = 2.2f;
  float _exposure = 1.f;

  void _initialize(std::vector<std::shared_ptr<ImageView>> src,
                   std::vector<std::shared_ptr<Texture>> blur,
                   std::vector<std::shared_ptr<ImageView>> dst);

 public:
  Postprocessing(std::vector<std::shared_ptr<ImageView>> src,
                 std::vector<std::shared_ptr<Texture>> blur,
                 std::vector<std::shared_ptr<ImageView>> dst,
                 std::shared_ptr<EngineState> engineState);
  void setExposure(float exposure);
  void setGamma(float gamma);
  float getGamma();
  float getExposure();

  void reset(std::vector<std::shared_ptr<ImageView>> src,
             std::vector<std::shared_ptr<Texture>> blur,
             std::vector<std::shared_ptr<ImageView>> dst);

  void drawCompute(int swapchainIndex, std::shared_ptr<CommandBuffer> commandBuffer);
};