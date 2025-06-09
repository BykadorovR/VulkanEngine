#pragma once
#include <memory>
#include <vector>
#include "Graphic/Texture.h"
#include "Primitive/Cubemap.h"
#include "Utility/Settings.h"
#include "Graphic/Camera.h"
#include "Utility/Logger.h"
#include "Vulkan/Render.h"
#include "Graphic/Blur.h"

class DirectionalShadow {
 protected:
  std::shared_ptr<EngineState> _engineState;
  std::vector<std::shared_ptr<Texture>> _shadowMapTexture;

 public:
  DirectionalShadow(std::shared_ptr<CommandBuffer> commandBufferTransfer, std::shared_ptr<EngineState> engineState);
  std::vector<std::shared_ptr<Texture>> getShadowMapTexture();
};

class PointShadow {
 protected:
  std::shared_ptr<EngineState> _engineState;
  std::vector<std::shared_ptr<Cubemap>> _shadowMapCubemap;

 public:
  PointShadow(std::shared_ptr<CommandBuffer> commandBufferTransfer, std::shared_ptr<EngineState> engineState);
  std::vector<std::shared_ptr<Cubemap>> getShadowMapCubemap();
};