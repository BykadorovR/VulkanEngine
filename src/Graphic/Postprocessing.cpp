#include "Graphic/Postprocessing.h"

struct ComputePush {
  float gamma;
  float exposure;
  int enableBloom;
};

void Postprocessing::_initialize(std::vector<std::shared_ptr<ImageView>> src,
                                 std::vector<std::shared_ptr<Texture>> blur,
                                 std::vector<std::shared_ptr<ImageView>> dst) {
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    for (int j = 0; j < dst.size(); j++) {
      _descriptorSet[std::pair(i, j)] = std::make_shared<DescriptorSet>(_textureLayout, _engineState);
      std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
          {0,
           {VkDescriptorImageInfo{.imageView = src[j]->getImageView(),
                                  .imageLayout = src[j]->getImage()->getImageLayout()}}},
          {1,
           {VkDescriptorImageInfo{.imageView = blur[i]->getImageView()->getImageView(),
                                  .imageLayout = blur[i]->getImageView()->getImage()->getImageLayout()}}},
          {2,
           {VkDescriptorImageInfo{.imageView = dst[j]->getImageView(),
                                  .imageLayout = VK_IMAGE_LAYOUT_GENERAL}}}};  // TODO: fix somehow
      _descriptorSet[std::pair(i, j)]->createCustom({}, textureInfoColor);
    }
  }
}

Postprocessing::Postprocessing(std::vector<std::shared_ptr<ImageView>> src,
                               std::vector<std::shared_ptr<Texture>> blur,
                               std::vector<std::shared_ptr<ImageView>> dst,
                               std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;

  auto shader = std::make_shared<Shader>(_engineState);
  shader->add("shaders/postprocessing/postprocess_compute.spv", VK_SHADER_STAGE_COMPUTE_BIT);

  _textureLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
  std::vector<VkDescriptorSetLayoutBinding> layoutBinding = {{.binding = 0,
                                                              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                              .descriptorCount = 1,
                                                              .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                                              .pImmutableSamplers = nullptr},
                                                             {.binding = 1,
                                                              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                              .descriptorCount = 1,
                                                              .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                                              .pImmutableSamplers = nullptr},
                                                             {.binding = 2,
                                                              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                              .descriptorCount = 1,
                                                              .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                                              .pImmutableSamplers = nullptr}};
  _textureLayout->createCustom(layoutBinding);

  _initialize(src, blur, dst);

  _computePipeline = std::make_shared<PipelineCompute>(_engineState->getDevice());
  _computePipeline->createCustom(
      shader->getShaderStageInfo(VK_SHADER_STAGE_COMPUTE_BIT), {std::pair{std::string("texture"), _textureLayout}},
      std::map<std::string, VkPushConstantRange>{
          {std::string("compute"),
           VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(ComputePush)}}});
}

void Postprocessing::reset(std::vector<std::shared_ptr<ImageView>> src,
                           std::vector<std::shared_ptr<Texture>> blur,
                           std::vector<std::shared_ptr<ImageView>> dst) {
  _initialize(src, blur, dst);
}

void Postprocessing::setExposure(float exposure) { _exposure = exposure; }

void Postprocessing::setGamma(float gamma) { _gamma = gamma; }

float Postprocessing::getGamma() { return _gamma; }

float Postprocessing::getExposure() { return _exposure; }

void Postprocessing::drawCompute(int swapchainIndex, std::shared_ptr<CommandBuffer> commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, _computePipeline->getPipeline());

  if (_computePipeline->getPushConstants().find("compute") != _computePipeline->getPushConstants().end()) {
    ComputePush pushConstants{.gamma = _gamma,
                              .exposure = _exposure,
                              .enableBloom = _engineState->getSettings()->getBloomPasses()};
    auto info = _computePipeline->getPushConstants()["compute"];
    vkCmdPushConstants(commandBuffer->getCommandBuffer(), _computePipeline->getPipelineLayout(), info.stageFlags,
                       info.offset, info.size, &pushConstants);
  }

  auto pipelineLayout = _computePipeline->getDescriptorSetLayout();
  auto computeLayout = std::find_if(pipelineLayout.begin(), pipelineLayout.end(),
                                    [](std::pair<std::string, std::shared_ptr<DescriptorSetLayout>> info) {
                                      return info.first == std::string("texture");
                                    });
  if (computeLayout != pipelineLayout.end()) {
    vkCmdBindDescriptorSets(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE,
                            _computePipeline->getPipelineLayout(), 0, 1,
                            &_descriptorSet[std::pair(currentFrame, swapchainIndex)]->getDescriptorSets(), 0, nullptr);
  }

  auto [width, height] = _engineState->getSettings()->getResolution();
  vkCmdDispatch(commandBuffer->getCommandBuffer(), std::max(1, (int)std::ceil(width / 16.f)),
                std::max(1, (int)std::ceil(height / 16.f)), 1);
}