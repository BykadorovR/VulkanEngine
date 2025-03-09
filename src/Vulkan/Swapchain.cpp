#include "Vulkan/Swapchain.h"

Swapchain::Swapchain(std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;

  vkb::SwapchainBuilder builder{_engineState->getDevice()->getDevice()};
#if __ANDROID__
  builder.set_composite_alpha_flags(VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);
#else
  builder.set_composite_alpha_flags(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
#endif
  builder.set_desired_format(VkSurfaceFormatKHR{.format = engineState->getSettings()->getSwapchainColorFormat(),
                                                .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
  // because we use swapchain in compute shader
  builder.add_image_usage_flags(VK_IMAGE_USAGE_STORAGE_BIT);
  if (engineState->getSettings()->getVerticalSync()) {
#if __ANDROID__
    builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);

#else
    builder.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR);
#endif
  } else {
    builder.set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR);
  }

  auto swapchainResult = builder.build();
  if (!swapchainResult) {
    throw std::runtime_error(swapchainResult.error().message());
  }
  // TODO: check extent
  _swapchain = swapchainResult.value();

  _createImageViews();
}

void Swapchain::_createImageViews() {
  std::vector<std::shared_ptr<Image>> images;
  for (auto& image : _swapchain.get_images().value()) {
    images.push_back(std::make_shared<Image>(image, std::tuple{_swapchain.extent.width, _swapchain.extent.height},
                                             _swapchain.image_format, _engineState));
  }

  _swapchainImageViews.clear();
  for (int i = 0; i < _swapchain.get_image_views().value().size(); i++) {
    _swapchainImageViews.push_back(
        std::make_shared<ImageView>(images[i], _swapchain.get_image_views().value()[i], _engineState));
  }
}

std::vector<std::shared_ptr<ImageView>> Swapchain::getImageViews() { return _swapchainImageViews; }

Swapchain::~Swapchain() { _destroy(); }

void Swapchain::_destroy() { vkb::destroy_swapchain(_swapchain); }

const vkb::Swapchain& Swapchain::getSwapchain() { return _swapchain; }

uint32_t& Swapchain::getSwapchainIndex() { return _swapchainIndex; }

void Swapchain::reset() {
  if (vkDeviceWaitIdle(_engineState->getDevice()->getDevice().device) != VK_SUCCESS)
    throw std::runtime_error("failed to create reset swap chain!");

  vkb::SwapchainBuilder builder{_engineState->getDevice()->getDevice()};
  builder.set_old_swapchain(_swapchain);
#if __ANDROID__
  builder.set_composite_alpha_flags(VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);
#else
  builder.set_composite_alpha_flags(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
#endif
  builder.set_desired_format(VkSurfaceFormatKHR{.format = _engineState->getSettings()->getSwapchainColorFormat(),
                                                .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
  // because we use swapchain in compute shader
  builder.add_image_usage_flags(VK_IMAGE_USAGE_STORAGE_BIT);
  auto swapchainResult = builder.build();
  if (!swapchainResult) {
    // If it failed to create a swapchain, the old swapchain handle is invalid.
    _swapchain.swapchain = VK_NULL_HANDLE;
  }
  // Even though we recycled the previous swapchain, we need to free its resources.
  _destroy();
  // Get the new swapchain and place it in our variable
  _swapchain = swapchainResult.value();

  _createImageViews();
}