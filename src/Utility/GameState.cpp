#include <chrono>
#include <optional>
#include "Utility/GameState.h"

GameState::GameState(const RenderGraph::CommandBuffer* commandBuffer, std::shared_ptr<EngineState> engineState) {
  _resourceManager = std::make_shared<ResourceManager>(engineState);
#ifdef __ANDROID__
  _resourceManager->setAssetManager(engineState->getAssetManager());
#endif
  _resourceManager->initialize();
  _lightManager = std::make_shared<LightManager>(_resourceManager, engineState, *commandBuffer);
  _cameraManager = std::make_shared<CameraManager>();
}

std::shared_ptr<ResourceManager> GameState::getResourceManager() { return _resourceManager; }

std::shared_ptr<CameraManager> GameState::getCameraManager() { return _cameraManager; }

std::shared_ptr<LightManager> GameState::getLightManager() { return _lightManager; }
