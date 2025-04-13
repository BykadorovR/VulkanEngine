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
  if (depthReference) subpass.pDepthStencilAttachment = &depthReference.value();
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

RenderPassManager::RenderPassManager(std::shared_ptr<Settings> settings, std::shared_ptr<Device> device) {
  // initialize graphic pass
  {
    std::vector<VkAttachmentDescription> colorDescription{
        {.format = settings->getSwapchainColorFormat(),
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
         // goes to bloom (not blur) postprocessing as input image
         .finalLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.format = settings->getGraphicColorFormat(),
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         // goes to blur postprocessing stage (bloom happens after blur) and then to bloom as blured image
         .finalLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.format = settings->getDepthFormat(),
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         // comes from light baking to depth image
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         // goes to GUI for visualization
         .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}};

    std::vector<VkAttachmentReference> colorReference{{.attachment = 0, .layout = VK_IMAGE_LAYOUT_GENERAL},
                                                      {.attachment = 1, .layout = VK_IMAGE_LAYOUT_GENERAL}};
    VkAttachmentReference depthReference{.attachment = 2,
                                         // we want read depth in shader
                                         .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    _renderPasses[RenderPassScenario::GRAPHIC] = std::make_shared<RenderPass>(device);
    _renderPasses[RenderPassScenario::GRAPHIC]->initializeCustom(colorDescription, colorReference, depthReference);
  }
  // initialize GUI pass
  {
    std::vector<VkAttachmentDescription> colorDescription{{.format = settings->getSwapchainColorFormat(),
                                                           .samples = VK_SAMPLE_COUNT_1_BIT,
                                                           .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                                                           .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                                           .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                           .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                           // comes from postprocessing
                                                           .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
                                                           // goes to presentation
                                                           .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR}};
    VkAttachmentReference colorReference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_GENERAL};
    _renderPasses[RenderPassScenario::GUI] = std::make_shared<RenderPass>(device);
    _renderPasses[RenderPassScenario::GUI]->initializeCustom(colorDescription, {colorReference}, std::nullopt);
  }
  // initialize shadow pass
  {
    std::vector<VkAttachmentDescription> colorDescription{
        {.format = settings->getShadowMapFormat(),
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         // we want read depth buffer in graphic render later, so after graphic render it's read only (initially also,
         // so it's the same behaviour)
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         // graphic render will read from it, so change back to read only
         .finalLayout = VK_IMAGE_LAYOUT_GENERAL}};

    VkAttachmentReference colorReference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_GENERAL};
    _renderPasses[RenderPassScenario::SHADOW] = std::make_shared<RenderPass>(device);
    _renderPasses[RenderPassScenario::SHADOW]->initializeCustom(colorDescription, {colorReference}, std::nullopt);
  }
  // initialize IBL pass
  {
    std::vector<VkAttachmentDescription> colorDescription{{.format = settings->getGraphicColorFormat(),
                                                           .samples = VK_SAMPLE_COUNT_1_BIT,
                                                           .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                           .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                                           .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                           .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                           // comes from previous stage/initialization
                                                           .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                                           // goes to render shader as input
                                                           .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};

    // we want write to this attachments
    VkAttachmentReference colorReference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    _renderPasses[RenderPassScenario::IBL] = std::make_shared<RenderPass>(device);
    _renderPasses[RenderPassScenario::IBL]->initializeCustom(colorDescription, {colorReference}, std::nullopt);
  }
  // initialize Blur
  {
    std::vector<VkAttachmentDescription> colorDescription{
        {.format = settings->getShadowMapFormat(),
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         // we want read depth buffer in graphic render later, so after graphic render it's read only (initially also,
         // so it's the same behaviour)
         .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
         // graphic render will read from it, so change back to read only
         .finalLayout = VK_IMAGE_LAYOUT_GENERAL}};
    VkAttachmentReference colorReference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_GENERAL};

    _renderPasses[RenderPassScenario::BLUR] = std::make_shared<RenderPass>(device);
    _renderPasses[RenderPassScenario::BLUR]->initializeCustom(colorDescription, {colorReference}, std::nullopt);
  }
}

std::shared_ptr<RenderPass> RenderPassManager::getRenderPass(RenderPassScenario scenario) {
  return _renderPasses[scenario];
}

int RenderPass::getColorAttachmentNumber() { return _colorAttachmentNumber; }

VkRenderPass& RenderPass::getRenderPass() { return _renderPass; }

RenderPass::~RenderPass() { vkDestroyRenderPass(_device->getLogicalDevice(), _renderPass, nullptr); }