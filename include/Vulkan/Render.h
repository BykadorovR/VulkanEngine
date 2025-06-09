#pragma once
#include <Vulkan/Device.h>
#include <Utility/Settings.h>
#include <functional>

enum class RenderPassScenario { GRAPHIC, GUI, SHADOW, IBL, BLUR };

class RenderPass {
 private:
  std::shared_ptr<Device> _device;
  std::shared_ptr<Settings> _settings;
  VkRenderPass _renderPass;
  int _colorAttachmentNumber = 0;
  int _depthAttachmentNumber = 0;

 public:
  RenderPass(std::shared_ptr<Device> device);
  void initializeCustom(std::vector<VkAttachmentDescription> colorDescription,
                        std::vector<VkAttachmentReference> colorReference,
                        std::optional<VkAttachmentReference> depthReference);
  VkRenderPass& getRenderPass();
  int getColorAttachmentNumber();
  int getDepthAttachmentNumber();
  ~RenderPass();
};

class RenderPassManager {
 private:
  std::map<RenderPassScenario, std::shared_ptr<RenderPass>> _renderPasses;
  std::map<RenderPassScenario, std::vector<std::function<void(std::shared_ptr<RenderPass> renderPass)>>>
      _pipelineCallbacks;

 public:
  void setRenderPass(RenderPassScenario scenario, std::shared_ptr<RenderPass> renderPass);
  std::shared_ptr<RenderPass> getRenderPass(RenderPassScenario scenario);
  void subscribe(RenderPassScenario scenario, std::function<void(std::shared_ptr<RenderPass> renderPass)> callback);
};