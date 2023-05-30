#pragma once
#include "Window.h"
#include "Instance.h"
#include "Surface.h"
#include "Device.h"
#include "Swapchain.h"
#include "Render.h"
#include "Buffer.h"
#include "Shader.h"
#include "Pipeline.h"
#include "Command.h"
#include "Queue.h"
#include "Sync.h"
#include "Command.h"
#include "Settings.h"
#include "Input.h"

class State {
 private:
  std::shared_ptr<Settings> _settings;
  std::shared_ptr<Window> _window;
  std::shared_ptr<Input> _input;
  std::shared_ptr<Instance> _instance;
  std::shared_ptr<Surface> _surface;
  std::shared_ptr<Device> _device;
  std::shared_ptr<CommandPool> _commandPool;
  std::shared_ptr<Queue> _queue;
  std::shared_ptr<CommandBuffer> _commandBuffer;
  std::shared_ptr<Swapchain> _swapchain;
  std::shared_ptr<DescriptorPool> _descriptorPool;

 public:
  State(std::shared_ptr<Settings> settings);
  std::shared_ptr<Settings> getSettings();
  std::shared_ptr<Window> getWindow();
  std::shared_ptr<Input> getInput();
  std::shared_ptr<Instance> getInstance();
  std::shared_ptr<Surface> getSurface();
  std::shared_ptr<Device> getDevice();
  std::shared_ptr<CommandPool> getCommandPool();
  std::shared_ptr<Queue> getQueue();
  std::shared_ptr<CommandBuffer> getCommandBuffer();
  std::shared_ptr<Swapchain> getSwapchain();
  std::shared_ptr<DescriptorPool> getDescriptorPool();
};