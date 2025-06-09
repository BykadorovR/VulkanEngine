#include "Vulkan/Render.h"
#include <array>

RenderPass::RenderPass(std::shared_ptr<Device> device) { _device = device; }

void RenderPass::initializeCustom(std::vector<VkAttachmentDescription> colorDescription,
                                  std::vector<VkAttachmentReference> colorReference,
                                  std::optional<VkAttachmentReference> depthReference) {
  _colorAttachmentNumber = colorReference.size();
  VkSubpassDescription subpass{.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                               .colorAttachmentCount = static_cast<uint32_t>(colorReference.size()),
                               .pColorAttachments = colorReference.data()};
  if (depthReference) {
    _depthAttachmentNumber = 1;
    subpass.pDepthStencilAttachment = &depthReference.value();
  }
  VkRenderPassCreateInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                        .attachmentCount = static_cast<uint32_t>(colorDescription.size()),
                                        .pAttachments = colorDescription.data(),
                                        .subpassCount = 1,
                                        .pSubpasses = &subpass,
                                        .dependencyCount = 0,
                                        .pDependencies = nullptr};

  if (vkCreateRenderPass(_device->getLogicalDevice(), &renderPassInfo, nullptr, &_renderPass) != VK_SUCCESS) {
    throw std::runtime_error("failed to create render pass!");
  }
}

int RenderPass::getColorAttachmentNumber() { return _colorAttachmentNumber; }

int RenderPass::getDepthAttachmentNumber() { return _depthAttachmentNumber; }

VkRenderPass& RenderPass::getRenderPass() { return _renderPass; }

RenderPass::~RenderPass() { vkDestroyRenderPass(_device->getLogicalDevice(), _renderPass, nullptr); }

void RenderPassManager::setRenderPass(RenderPassScenario scenario, std::shared_ptr<RenderPass> renderPass) {
  _renderPasses[scenario] = renderPass;
  for (auto& callback : _pipelineCallbacks[scenario]) {
    callback(renderPass);
  }
}

std::shared_ptr<RenderPass> RenderPassManager::getRenderPass(RenderPassScenario scenario) {
  if (_renderPasses.find(scenario) != _renderPasses.end()) return _renderPasses[scenario];
  return nullptr;
}

void RenderPassManager::subscribe(RenderPassScenario scenario,
                                  std::function<void(std::shared_ptr<RenderPass> renderPass)> callback) {
  _pipelineCallbacks[scenario].push_back(callback);
}