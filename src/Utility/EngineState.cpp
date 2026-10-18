#include <glm/glm.hpp>
#include "Utility/EngineState.h"

EngineState::EngineState(std::shared_ptr<Settings> settings) { _settings = settings; }

#ifdef __ANDROID__
void EngineState::setNativeWindow(ANativeWindow* window) { _nativeWindow = window; }
void EngineState::setAssetManager(AAssetManager* assetManager) { _assetManager = assetManager; }
AAssetManager* EngineState::getAssetManager() { return _assetManager; }
#endif

void EngineState::initialize() {
#ifdef __ANDROID__
  _window = std::make_shared<RenderGraph::Window>(_nativeWindow);
#else
  const auto [width, height] = _settings->getResolution();
  _window = std::make_shared<RenderGraph::Window>(glm::ivec2{width, height});
#endif
  _window->initialize();
  _input = std::make_shared<Input>(_window);
  _instance = std::make_shared<RenderGraph::Instance>(_settings->getName(), true);
  _surface = std::make_shared<RenderGraph::Surface>(*_window, *_instance);
  _device = std::make_shared<RenderGraph::Device>(*_instance);
  _device->setSurface(*_surface);
  RenderGraph::DeviceRequirements deviceRequirements;
  deviceRequirements.features.geometryShader = true;
  deviceRequirements.features.tessellationShader = true;
  deviceRequirements.features.samplerAnisotropy = _settings->getAnisotropicSamples() > 0;
#ifndef __ANDROID__
  deviceRequirements.features.fillModeNonSolid = true;
#endif
  deviceRequirements.features.largePoints = true;
  deviceRequirements.features.shaderStorageImageReadWithoutFormat = true;
  deviceRequirements.features.shaderStorageImageWriteWithoutFormat = true;
  _device->initialize(deviceRequirements);
  _memoryAllocator = std::make_shared<RenderGraph::MemoryAllocator>(*_device, *_instance);
  _resourceUploader = std::make_shared<RenderGraph::ResourceUploader>(*_memoryAllocator, *_device);
  const RenderGraph::DescriptorPoolSize descriptorPoolSize{
      .uniformBuffer = _settings->getPoolSizeUBO(),
      .sampler = _settings->getPoolSizeSampler(),
      .computeImage = _settings->getPoolSizeComputeImage(),
      .ssbo = _settings->getPoolSizeSSBO(),
      .descriptorSets = _settings->getPoolSizeDescriptorSets(),
  };
  _descriptorPool = std::make_shared<RenderGraph::DescriptorPool>(descriptorPoolSize, *_device);
  _filesystem = std::make_shared<Filesystem>();
  _logger = std::make_shared<Logger>();
  _debugUtils = std::make_shared<DebugUtils>(_device->getLogicalDevice());
#ifdef __ANDROID__
  _filesystem->setAssetManager(_assetManager);
#endif
}

std::shared_ptr<Settings> EngineState::getSettings() { return _settings; }

std::shared_ptr<RenderGraph::Window> EngineState::getWindow() { return _window; }

std::shared_ptr<Input> EngineState::getInput() { return _input; }

std::shared_ptr<RenderGraph::MemoryAllocator> EngineState::getMemoryAllocator() { return _memoryAllocator; }

std::shared_ptr<RenderGraph::Instance> EngineState::getInstance() { return _instance; }

std::shared_ptr<RenderGraph::Surface> EngineState::getSurface() { return _surface; }

std::shared_ptr<RenderGraph::Device> EngineState::getDevice() { return _device; }

std::shared_ptr<RenderGraph::DescriptorPool> EngineState::getDescriptorPool() { return _descriptorPool; }

std::shared_ptr<Filesystem> EngineState::getFilesystem() { return _filesystem; }

std::shared_ptr<RenderGraph::ResourceUploader> EngineState::getResourceUploader() { return _resourceUploader; }

std::vector<RenderGraph::UploadTicket>& EngineState::getUploadTickets() { return _uploadTickets; }

std::shared_ptr<Logger> EngineState::getLogger() { return _logger; }

std::shared_ptr<DebugUtils> EngineState::getDebugUtils() { return _debugUtils; }

void EngineState::setFrameInFlight(int frameInFlight) { _frameInFlight = frameInFlight; }

int EngineState::getFrameInFlight() { return _frameInFlight; }
