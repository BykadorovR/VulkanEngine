#pragma once
#include "Vulkan/Device.h"
#include "Vulkan/Image.h"

class Swapchain {
 private:
  std::shared_ptr<EngineState> _engineState;
  vkb::Swapchain _swapchain;
  std::vector<std::shared_ptr<ImageView>> _swapchainImageViews;
  uint32_t _swapchainIndex;

  void _createImageViews();
  void _destroy();

 public:
  Swapchain(std::shared_ptr<EngineState> engineState);
  void reset();
  const vkb::Swapchain& getSwapchain();
  uint32_t& getSwapchainIndex();
  std::vector<std::shared_ptr<ImageView>> getImageViews();
  ~Swapchain();
};