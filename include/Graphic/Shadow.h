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
  std::vector<std::shared_ptr<CommandBuffer>> _commandBufferDirectional;
  std::vector<std::shared_ptr<Texture>> _shadowMapTexture;
  std::vector<std::shared_ptr<Framebuffer>> _shadowMapFramebuffer;

 public:
  DirectionalShadow(std::shared_ptr<CommandBuffer> commandBufferTransfer,
                    std::shared_ptr<RenderPass> renderPass,
                    std::shared_ptr<EngineState> engineState);
  std::vector<std::shared_ptr<Texture>> getShadowMapTexture();

  std::shared_ptr<CommandBuffer> getShadowMapCommandBuffer(int frameInFlight);
  std::vector<std::shared_ptr<Framebuffer>> getShadowMapFramebuffer();
};

class PointShadow {
 protected:
  std::shared_ptr<EngineState> _engineState;
  std::vector<std::vector<std::shared_ptr<CommandBuffer>>> _commandBufferPoint;
  std::vector<std::shared_ptr<Cubemap>> _shadowMapCubemap;
  std::vector<std::vector<std::shared_ptr<Framebuffer>>> _shadowMapFramebuffer;
  std::shared_ptr<BlurGraphic> _blur;

 public:
  PointShadow(std::shared_ptr<CommandBuffer> commandBufferTransfer,
              std::shared_ptr<RenderPass> renderPass,
              std::shared_ptr<EngineState> engineState);
  std::vector<std::shared_ptr<Cubemap>> getShadowMapCubemap();
  std::vector<std::shared_ptr<CommandBuffer>> getShadowMapCommandBuffer(int frameInFlight);
  std::vector<std::vector<std::shared_ptr<Framebuffer>>> getShadowMapFramebuffer();
};

class PointShadowBlur {
 protected:
  std::shared_ptr<EngineState> _engineState;
  std::vector<std::vector<std::shared_ptr<CommandBuffer>>> _commandBufferPoint;
  std::vector<std::shared_ptr<Cubemap>> _cubemapOut;
  std::vector<std::vector<std::vector<std::shared_ptr<Framebuffer>>>> _shadowMapFramebuffer;
  std::vector<std::shared_ptr<BlurGraphic>> _blur;

 public:
  PointShadowBlur(std::vector<std::shared_ptr<Cubemap>> cubemapIn,
                  std::shared_ptr<CommandBuffer> commandBufferTransfer,
                  std::shared_ptr<RenderPass> renderPass,
                  std::shared_ptr<EngineState> engineState);
  std::vector<std::shared_ptr<CommandBuffer>> getShadowMapBlurCommandBuffer(int frameInFlight);
  std::vector<std::vector<std::vector<std::shared_ptr<Framebuffer>>>> getShadowMapBlurFramebuffer();
  std::vector<std::shared_ptr<BlurGraphic>> getBlur();
  std::vector<std::shared_ptr<Cubemap>> getShadowMapBlurCubemapOut();
};