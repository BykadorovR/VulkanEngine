#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <Utility/EngineState.h>
#include "Utility/ResourceManager.h"
#include "Graphic/CameraManager.h"
#include "Graphic/LightManager.h"

import Command;

class GameState {
 private:
  std::shared_ptr<ResourceManager> _resourceManager;
  std::shared_ptr<CameraManager> _cameraManager;
  std::shared_ptr<LightManager> _lightManager;

 public:
  GameState(const RenderGraph::CommandBuffer* commandBuffer, std::shared_ptr<EngineState> engineState);
  std::shared_ptr<ResourceManager> getResourceManager();
  std::shared_ptr<CameraManager> getCameraManager();
  std::shared_ptr<LightManager> getLightManager();
};
