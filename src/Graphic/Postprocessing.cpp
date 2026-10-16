#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include "Graphic/Postprocessing.h"

import Shader;

struct ComputePush {
  float gamma;
  float exposure;
};

void Postprocessing::_initialize(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& source,
                                 const std::vector<std::shared_ptr<RenderGraph::ImageView>>& destination,
                                 const RenderGraph::CommandBuffer& commandBuffer) {
  const auto frameCount = static_cast<std::size_t>(_engineState->getSettings()->getMaxFramesInFlight());
  if (source.size() != frameCount) {
    throw std::invalid_argument("Postprocessing source count must match the number of frames in flight");
  }
  if (destination.empty()) {
    throw std::invalid_argument("Postprocessing requires at least one destination image");
  }

  _descriptorSets.clear();
  _sourceTextures.clear();
  _destinationTextures.clear();
  _sourceTextures.reserve(source.size());
  _destinationTextures.reserve(destination.size());
  for (const auto& imageView : source) {
    _sourceTextures.push_back(std::make_shared<RenderGraph::Texture>(imageView, nullptr));
  }
  for (const auto& imageView : destination) {
    _destinationTextures.push_back(std::make_shared<RenderGraph::Texture>(imageView, nullptr));
  }

  _descriptorSets.resize(frameCount);
  for (std::size_t frame = 0; frame < frameCount; ++frame) {
    _descriptorSets[frame].resize(destination.size());
    for (std::size_t image = 0; image < destination.size(); ++image) {
      auto descriptorSet = std::make_shared<RenderGraph::DescriptorSet>(
          std::vector<RenderGraph::DescriptorSetLayout*>{_textureLayout.get()}, *_engineState->getDescriptorPool(),
          *_engineState->getDevice(), 0);
      descriptorSet->add({_sourceTextures[frame].get()});
      descriptorSet->add({_destinationTextures[image].get()});
      descriptorSet->initialize(commandBuffer);
      _descriptorSets[frame][image] = std::move(descriptorSet);
    }
  }
}

Postprocessing::Postprocessing(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& source,
                               const std::vector<std::shared_ptr<RenderGraph::ImageView>>& destination,
                               const RenderGraph::CommandBuffer& commandBuffer,
                               std::shared_ptr<EngineState> engineState,
                               RenderGraph::Swapchain& swapchain)
    : _engineState(std::move(engineState)),
      _swapchain(&swapchain) {
  auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
  shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                            "shaders/postprocessing/postprocess_compute.spv"));

  _textureLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
  _textureLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));
  _initialize(source, destination, commandBuffer);

  std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_textureLayout.get()};
  const std::unordered_map<std::string, VkPushConstantRange> pushConstants{
      {"compute", {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(ComputePush)}}};
  const auto shaderStages = shader->getShaderStageInfo();
  _computePipeline = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
  _computePipeline->createCompute(shaderStages.front(), descriptorSetLayouts, pushConstants);
}

void Postprocessing::reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& source,
                           const std::vector<std::shared_ptr<RenderGraph::ImageView>>& destination,
                           const RenderGraph::CommandBuffer& commandBuffer) {
  _initialize(source, destination, commandBuffer);
}

void Postprocessing::setExposure(float exposure) { _exposure = exposure; }

void Postprocessing::setGamma(float gamma) { _gamma = gamma; }

float Postprocessing::getGamma() { return _gamma; }

float Postprocessing::getExposure() { return _exposure; }

void Postprocessing::draw(int, const RenderGraph::CommandBuffer& commandBuffer) {
  const auto logger = _engineState->getLogger();
  logger->begin("Postprocessing compute " + std::to_string(_engineState->getFrameInFlight()), &commandBuffer);

  const int currentFrame = _engineState->getFrameInFlight();
  const int swapchainIndex = _swapchain->getSwapchainIndex();
  if (currentFrame < 0 || static_cast<std::size_t>(currentFrame) >= _descriptorSets.size()) {
    throw std::out_of_range("Postprocessing frame is out of range");
  }
  if (swapchainIndex < 0 || static_cast<std::size_t>(swapchainIndex) >= _descriptorSets[currentFrame].size()) {
    throw std::out_of_range("Postprocessing swapchain image is out of range");
  }

  vkCmdBindPipeline(commandBuffer.getCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, _computePipeline->getPipeline());

  if (_computePipeline->getPushConstants().contains("compute")) {
    const ComputePush pushConstants{.gamma = _gamma, .exposure = _exposure};
    const auto info = _computePipeline->getPushConstants().at("compute");
    vkCmdPushConstants(commandBuffer.getCommandBuffer(), _computePipeline->getPipelineLayout(), info.stageFlags,
                       info.offset, info.size, &pushConstants);
  }

  _descriptorSets[currentFrame][swapchainIndex]->bind(VK_PIPELINE_BIND_POINT_COMPUTE,
                                                      _computePipeline->getPipelineLayout(), commandBuffer);

  const auto [width, height] = _engineState->getSettings()->getResolution();
  vkCmdDispatch(commandBuffer.getCommandBuffer(), std::max(1, static_cast<int>(std::ceil(width / 16.f))),
                std::max(1, static_cast<int>(std::ceil(height / 16.f))), 1);
  logger->end(&commandBuffer);
}

void Postprocessing::update(int, const RenderGraph::CommandBuffer&) {}

void Postprocessing::reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>&) {}
