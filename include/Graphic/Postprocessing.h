#pragma once
#include "State.h"
#include "Texture.h"
#include "Pipeline.h"

class Postprocessing {
 private:
  std::shared_ptr<State> _state;
  std::shared_ptr<Pipeline> _computePipeline;
  std::shared_ptr<DescriptorSetLayout> _textureLayout;
  std::map<std::pair<int, int>, std::shared_ptr<DescriptorSet>> _descriptorSet;
  float _gamma = 2.2f;
  float _exposure = 1.f;

  void _initialize(std::vector<std::shared_ptr<Texture>> src,
                   std::vector<std::shared_ptr<Texture>> blur,
                   std::vector<std::shared_ptr<ImageView>> dst);

 public:
  Postprocessing(std::vector<std::shared_ptr<Texture>> src,
                 std::vector<std::shared_ptr<Texture>> blur,
                 std::vector<std::shared_ptr<ImageView>> dst,
                 std::shared_ptr<State> state);
  void setExposure(float exposure);
  void setGamma(float gamma);
  float getGamma();
  float getExposure();

  void reset(std::vector<std::shared_ptr<Texture>> src,
             std::vector<std::shared_ptr<Texture>> blur,
             std::vector<std::shared_ptr<ImageView>> dst);

  void drawCompute(int currentFrame, int swapchainIndex, std::shared_ptr<CommandBuffer> commandBuffer);
};