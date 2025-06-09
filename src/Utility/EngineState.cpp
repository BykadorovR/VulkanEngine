#include "Utility/EngineState.h"

EngineState::EngineState(std::shared_ptr<Settings> settings) { _settings = settings; }

#ifdef __ANDROID__
void EngineState::setNativeWindow(ANativeWindow* window) { _nativeWindow = window; }
void EngineState::setAssetManager(AAssetManager* assetManager) { _assetManager = assetManager; }
AAssetManager* EngineState::getAssetManager() { return _assetManager; }
#endif

void EngineState::initialize() {
  _window = std::make_shared<Window>(_settings->getResolution());
#ifdef __ANDROID__
  _window->setNativeWindow(_nativeWindow);
#endif
  _window->initialize();
  _input = std::make_shared<Input>(_window);
  _instance = std::make_shared<Instance>(_settings->getName(), true);
  _surface = std::make_shared<Surface>(_window, _instance);
  _device = std::make_shared<Device>(_surface, _instance);
  _memoryAllocator = std::make_shared<MemoryAllocator>(_device, _instance);
  _descriptorPool = std::make_shared<DescriptorPool>(_settings, _device);
  _filesystem = std::make_shared<Filesystem>();
  _renderPassManager = std::make_shared<RenderPassManager>();
  _logger = std::make_shared<Logger>(_device);
  _debugUtils = std::make_shared<DebugUtils>(_device);
#ifdef __ANDROID__
  _filesystem->setAssetManager(_assetManager);
#endif
}

std::shared_ptr<Settings> EngineState::getSettings() { return _settings; }

std::shared_ptr<Window> EngineState::getWindow() { return _window; }

std::shared_ptr<Input> EngineState::getInput() { return _input; }

std::shared_ptr<MemoryAllocator> EngineState::getMemoryAllocator() { return _memoryAllocator; }

std::shared_ptr<Instance> EngineState::getInstance() { return _instance; }

std::shared_ptr<Surface> EngineState::getSurface() { return _surface; }

std::shared_ptr<Device> EngineState::getDevice() { return _device; }

std::shared_ptr<DescriptorPool> EngineState::getDescriptorPool() { return _descriptorPool; }

std::shared_ptr<Filesystem> EngineState::getFilesystem() { return _filesystem; }

std::shared_ptr<RenderPassManager> EngineState::getRenderPassManager() { return _renderPassManager; }

std::shared_ptr<Logger> EngineState::getLogger() { return _logger; }

std::shared_ptr<DebugUtils> EngineState::getDebugUtils() { return _debugUtils; }

void EngineState::setFrameInFlight(int frameInFlight) { _frameInFlight = frameInFlight; }

int EngineState::getFrameInFlight() { return _frameInFlight; }