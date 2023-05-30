#include "State.h"

State::State(std::shared_ptr<Settings> settings) {
  _settings = settings;

  _window = std::make_shared<Window>(settings->getResolution());
  _input = std::make_shared<Input>(_window);
  _instance = std::make_shared<Instance>(settings->getName(), true, _window);
  _surface = std::make_shared<Surface>(_window, _instance);
  _device = std::make_shared<Device>(_surface, _instance);
  _commandPool = std::make_shared<CommandPool>(_device);
  _queue = std::make_shared<Queue>(_device);
  _commandBuffer = std::make_shared<CommandBuffer>(settings->getMaxFramesInFlight(), _commandPool, _device);
  _swapchain = std::make_shared<Swapchain>(settings->getFormat(), _window, _surface, _device);
  _descriptorPool = std::make_shared<DescriptorPool>(1000, _device);
}

std::shared_ptr<Settings> State::getSettings() { return _settings; }

std::shared_ptr<Window> State::getWindow() { return _window; }

std::shared_ptr<Input> State::getInput() { return _input; }

std::shared_ptr<Instance> State::getInstance() { return _instance; }

std::shared_ptr<Surface> State::getSurface() { return _surface; }

std::shared_ptr<Device> State::getDevice() { return _device; }

std::shared_ptr<CommandPool> State::getCommandPool() { return _commandPool; }

std::shared_ptr<Queue> State::getQueue() { return _queue; }

std::shared_ptr<CommandBuffer> State::getCommandBuffer() { return _commandBuffer; }

std::shared_ptr<Swapchain> State::getSwapchain() { return _swapchain; }

std::shared_ptr<DescriptorPool> State::getDescriptorPool() { return _descriptorPool; }