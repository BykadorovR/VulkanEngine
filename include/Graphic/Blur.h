#pragma once
#include "Utility/EngineState.h"
#include "Graphic/Texture.h"
#include "Vulkan/Descriptor.h"
#include "Vulkan/Pipeline.h"
#include "Primitive/Mesh.h"
#include "Primitive/Cubemap.h"

class BlurSeparate {
 protected:
  std::shared_ptr<EngineState> _engineState;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSet;
  std::shared_ptr<DescriptorSetLayout> _descriptorSetLayout;
  std::vector<std::shared_ptr<Buffer>> _blurWeightsSSBO;
  std::vector<std::shared_ptr<Texture>> _textureSrc;
  std::vector<std::shared_ptr<Texture>> _textureDst;
  bool _horizontal;
  std::vector<float> _blurWeights;
  std::vector<bool> _changed;
  int _kernelSize = 15;
  float _sigma = _kernelSize / 3;

  void _updateWeights();
  void _updateDescriptors(int currentFrame);

 public:
  BlurSeparate(bool horizontal,
               std::vector<std::shared_ptr<Texture>> src,
               std::vector<std::shared_ptr<Texture>> dst,
               std::shared_ptr<EngineState> engineState);
  virtual void draw(std::shared_ptr<CommandBuffer> commandBuffer) = 0;
  std::vector<std::shared_ptr<Texture>> getTextureSrc();
  std::vector<std::shared_ptr<Texture>> getTextureDst();
  bool getHorizontal();
  virtual ~BlurSeparate() = default;
};

class BlurComputeSeparate : public BlurSeparate {
 private:
  std::shared_ptr<PipelineCompute> _pipeline;

  void _initialize(std::vector<std::shared_ptr<Texture>> src, std::vector<std::shared_ptr<Texture>> dst);

 public:
  BlurComputeSeparate(bool horizontal,
                      std::vector<std::shared_ptr<Texture>> src,
                      std::vector<std::shared_ptr<Texture>> dst,
                      std::shared_ptr<EngineState> engineState);
  void draw(std::shared_ptr<CommandBuffer> commandBuffer) override;
  ~BlurComputeSeparate() = default;
};

class BlurGraphicSeparate : public BlurSeparate {
 private:
  std::shared_ptr<PipelineGraphic> _pipeline;
  std::shared_ptr<MeshStatic2D> _mesh;
  std::tuple<int, int> _resolution;

  void _initialize(std::vector<std::shared_ptr<Texture>> src);

 public:
  BlurGraphicSeparate(bool horizontal,
                      std::vector<std::shared_ptr<Texture>> src,
                      std::vector<std::shared_ptr<Texture>> dst,
                      std::shared_ptr<CommandBuffer> commandBufferTransfer,
                      std::shared_ptr<EngineState> engineState);
  void draw(std::shared_ptr<CommandBuffer> commandBuffer) override;
  ~BlurGraphicSeparate() = default;
};