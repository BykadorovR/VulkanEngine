#include "Graphic/Postprocessing.h"

struct ComputePush {
  float gamma;
  float exposure;
};

void Postprocessing::_initialize(std::vector<std::shared_ptr<ImageView>> src) {
  _descriptorSet.resize(src.size());
  for (int i = 0; i < src.size(); i++) {
    _descriptorSet[i] = std::make_shared<DescriptorSet>(_textureLayout, _engineState);
    std::map<int, std::vector<VkDescriptorImageInfo>> textureInfoColor = {
        {0,
         {VkDescriptorImageInfo{.imageView = src[i]->getImageView(),
                                .imageLayout = src[i]->getImage()->getImageLayout()}}}};  // TODO: fix somehow
    _descriptorSet[i]->createCustom({}, textureInfoColor);
  }
}

Postprocessing::Postprocessing(std::vector<std::shared_ptr<ImageView>> src, std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;

  auto shader = std::make_shared<Shader>(_engineState);
  shader->add("shaders/postprocessing/postprocess_compute.spv", VK_SHADER_STAGE_COMPUTE_BIT);

  _textureLayout = std::make_shared<DescriptorSetLayout>(_engineState->getDevice());
  std::vector<VkDescriptorSetLayoutBinding> layoutBinding = {{.binding = 0,
                                                              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                              .descriptorCount = 1,
                                                              .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                                              .pImmutableSamplers = nullptr}};
  _textureLayout->createCustom(layoutBinding);

  _initialize(src);

  _computePipeline = std::make_shared<PipelineCompute>(_engineState->getDevice());
  _computePipeline->createCustom(
      shader, {std::pair{std::string("texture"), _textureLayout}},
      std::map<std::string, VkPushConstantRange>{
          {std::string("compute"),
           VkPushConstantRange{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(ComputePush)}}});
}
void Postprocessing::reset(std::vector<std::shared_ptr<ImageView>> src) { _initialize(src); }

void Postprocessing::setExposure(float exposure) { _exposure = exposure; }

void Postprocessing::setGamma(float gamma) { _gamma = gamma; }

float Postprocessing::getGamma() { return _gamma; }

float Postprocessing::getExposure() { return _exposure; }

void Postprocessing::drawCompute(int swapchainIndex, std::shared_ptr<CommandBuffer> commandBuffer) {
  int currentFrame = _engineState->getFrameInFlight();
  vkCmdBindPipeline(commandBuffer->getCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, _computePipeline->getPipeline());

  if (_computePipeline->getPushConstants().find("compute") != _computePipeline->getPushConstants().end()) {
    ComputePush pushConstants{.gamma = _gamma, .exposure = _exposure};
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
                            &_descriptorSet[swapchainIndex]->getDescriptorSets(), 0, nullptr);
  }

  auto [width, height] = _engineState->getSettings()->getResolution();
  vkCmdDispatch(commandBuffer->getCommandBuffer(), std::max(1, (int)std::ceil(width / 16.f)),
                std::max(1, (int)std::ceil(height / 16.f)), 1);
}