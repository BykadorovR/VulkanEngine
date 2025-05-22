#pragma once
#include "Utility/EngineState.h"
#include "Graphic/Texture.h"
#include "Vulkan/Descriptor.h"
#include "Vulkan/Pipeline.h"
#include "Primitive/Mesh.h"
#include "Primitive/Cubemap.h"

class Blur {
 protected:
  std::shared_ptr<EngineState> _engineState;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSetVertical, _descriptorSetHorizontal;
  std::vector<std::shared_ptr<Buffer>> _blurWeightsSSBO;
  std::vector<float> _blurWeights;
  std::vector<bool> _changed;
  int _kernelSize = 3;
  float _sigma = _kernelSize / 3;

  void _updateWeights();
  virtual void _setWeights(int currentFrame) = 0;
  virtual void _updateDescriptors(int currentFrame) = 0;
  virtual void _initialize(std::vector<std::shared_ptr<Texture>> src, std::vector<std::shared_ptr<Texture>> dst) = 0;

 public:
  void reset(std::vector<std::shared_ptr<Texture>> src, std::vector<std::shared_ptr<Texture>> dst);
  int getKernelSize();
  int getSigma();
  void setKernelSize(int kernelSize);
  void setSigma(int sigma);
  virtual void draw(bool horizontal, std::shared_ptr<CommandBuffer> commandBuffer) = 0;
  virtual ~Blur() = default;
};

class BlurCompute : public Blur {
 private:
  std::shared_ptr<PipelineCompute> _pipelineVertical, _pipelineHorizontal;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSetWeights;
  std::shared_ptr<DescriptorSetLayout> _textureLayout;
  void _setWeights(int currentFrame) override;
  void _updateDescriptors(int currentFrame) override;
  void _initialize(std::vector<std::shared_ptr<Texture>> src, std::vector<std::shared_ptr<Texture>> dst) override;

 public:
  BlurCompute(std::vector<std::shared_ptr<Texture>> src,
              std::vector<std::shared_ptr<Texture>> dst,
              std::shared_ptr<EngineState> engineState);
  void draw(bool horizontal, std::shared_ptr<CommandBuffer> commandBuffer) override;
  ~BlurCompute() override = default;
};

class BlurGraphic : public Blur {
 private:
  std::shared_ptr<PipelineGraphic> _pipelineVertical, _pipelineHorizontal;
  std::shared_ptr<DescriptorSetLayout> _layoutBlur;
  std::shared_ptr<RenderPass> _renderPass;
  std::shared_ptr<MeshStatic2D> _mesh;
  std::tuple<int, int> _resolution;
  void _setWeights(int currentFrame) override;
  void _updateDescriptors(int currentFrame) override;
  void _initialize(std::vector<std::shared_ptr<Texture>> src, std::vector<std::shared_ptr<Texture>> dst) override;

 public:
  BlurGraphic(std::vector<std::shared_ptr<Texture>> src,
              std::vector<std::shared_ptr<Texture>> dst,
              std::shared_ptr<CommandBuffer> commandBufferTransfer,
              std::shared_ptr<EngineState> engineState);
  void draw(bool horizontal, std::shared_ptr<CommandBuffer> commandBuffer) override;
  ~BlurGraphic() override = default;
};

class BlurGraphicSeparate {
 private:
  std::shared_ptr<PipelineGraphic> _pipeline;
  std::shared_ptr<EngineState> _engineState;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSet;
  std::shared_ptr<DescriptorSetLayout> _layoutBlur;
  std::shared_ptr<RenderPass> _renderPass;
  std::shared_ptr<MeshStatic2D> _mesh;
  std::tuple<int, int> _resolution;
  std::vector<std::shared_ptr<Buffer>> _blurWeightsSSBO;
  std::vector<float> _blurWeights;
  std::vector<bool> _changed;
  bool _horizontal;
  int _kernelSize = 3;
  float _sigma = _kernelSize / 3;
  std::vector<std::shared_ptr<Texture>> _textureSrc, _textureDst;

  void _setWeights(int currentFrame);
  void _updateWeights();
  void _updateDescriptors(int currentFrame);
  void _initialize(std::vector<std::shared_ptr<Texture>> src);

 public:
  BlurGraphicSeparate(bool horizontal,
                      std::vector<std::shared_ptr<Texture>> src,
                      std::vector<std::shared_ptr<Texture>> dst,
                      std::shared_ptr<CommandBuffer> commandBufferTransfer,
                      std::shared_ptr<EngineState> engineState);
  void draw(std::shared_ptr<CommandBuffer> commandBuffer);
  std::vector<std::shared_ptr<Texture>> getTextureSrc();
  std::vector<std::shared_ptr<Texture>> getTextureDst();
  bool getHorizontal();
  ~BlurGraphicSeparate() = default;
};