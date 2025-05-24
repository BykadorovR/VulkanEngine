#pragma once
#include "Utility/EngineState.h"
#include "Graphic/Texture.h"
#include "Vulkan/Descriptor.h"
#include "Vulkan/Pipeline.h"
#include "Primitive/Mesh.h"
#include "Primitive/Cubemap.h"

class BlurComputeSeparate {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<PipelineCompute> _pipeline;
  std::vector<std::shared_ptr<DescriptorSet>> _descriptorSetWeights, _descriptorSet;
  std::shared_ptr<DescriptorSetLayout> _textureLayout;
  std::vector<std::shared_ptr<Buffer>> _blurWeightsSSBO;
  std::vector<std::shared_ptr<Texture>> _textureSrc;
  std::vector<std::shared_ptr<Texture>> _textureDst;
  std::vector<float> _blurWeights;
  std::vector<bool> _changed;
  int _kernelSize = 15;
  float _sigma = _kernelSize / 3;

  void _setWeights(int currentFrame);
  void _updateWeights();
  void _updateDescriptors(int currentFrame);
  void _initialize(std::vector<std::shared_ptr<Texture>> src, std::vector<std::shared_ptr<Texture>> dst);
  bool _horizontal;

 public:
  BlurComputeSeparate(bool horizontal,
                      std::vector<std::shared_ptr<Texture>> src,
                      std::vector<std::shared_ptr<Texture>> dst,
                      std::shared_ptr<EngineState> engineState);
  void draw(std::shared_ptr<CommandBuffer> commandBuffer);
  std::vector<std::shared_ptr<Texture>> getTextureSrc();
  std::vector<std::shared_ptr<Texture>> getTextureDst();
  bool getHorizontal();
  ~BlurComputeSeparate() = default;
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