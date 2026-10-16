#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include "Primitive/Drawable.h"
#include "Primitive/Cubemap.h"
#include "Utility/Settings.h"
#include "Graphic/Camera.h"
#include "Utility/Logger.h"
#include "Graphic/Blur.h"

import Command;
import Graph;
import Texture;

class DirectionalShadowResources {
 protected:
  std::shared_ptr<EngineState> _engineState;
  std::vector<std::shared_ptr<RenderGraph::Texture>> _shadowMapTexture;

 public:
  DirectionalShadowResources(const RenderGraph::CommandBuffer* commandBufferTransfer,
                             std::shared_ptr<EngineState> engineState);
  std::vector<std::shared_ptr<RenderGraph::Texture>> getShadowMapTexture();
};

class PointShadowResources {
 protected:
  std::shared_ptr<EngineState> _engineState;
  std::vector<std::shared_ptr<Cubemap>> _shadowMapCubemap;

 public:
  PointShadowResources(const RenderGraph::CommandBuffer* commandBufferTransfer,
                       std::shared_ptr<EngineState> engineState);
  std::vector<std::shared_ptr<Cubemap>> getShadowMapCubemap();
};

class DirectionalShadowRenderer final : public RenderGraph::GraphElement {
 private:
  const std::vector<std::shared_ptr<Shadowable>>* _shadowables;
  std::shared_ptr<EngineState> _engineState;
  int _shadowIndex;

 public:
  DirectionalShadowRenderer(int shadowIndex,
                            const std::vector<std::shared_ptr<Shadowable>>& shadowables,
                            std::shared_ptr<EngineState> engineState);
  void draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& swapchain) override;
};

class PointShadowRenderer final : public RenderGraph::GraphElement {
 private:
  std::shared_ptr<PointShadowResources> _pointShadowResources;
  const std::vector<std::shared_ptr<Shadowable>>* _shadowables;
  std::shared_ptr<EngineState> _engineState;
  int _shadowIndex;
  int _face;

 public:
  PointShadowRenderer(std::shared_ptr<PointShadowResources> pointShadowResources,
                      int shadowIndex,
                      int face,
                      const std::vector<std::shared_ptr<Shadowable>>& shadowables,
                      std::shared_ptr<EngineState> engineState);
  void draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& swapchain) override;
};
