#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <tuple>
#include <vector>
#include "Utility/EngineState.h"
#include "Graphic/Buffer.h"

import Command;
import Texture;

class Cubemap {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<RenderGraph::ImageView> _imageView;
  // for each face, mip maps are stored
  std::vector<std::vector<std::shared_ptr<RenderGraph::ImageView>>> _imageViewSeparate;
  std::shared_ptr<RenderGraph::Texture> _texture;
  std::vector<std::vector<std::shared_ptr<RenderGraph::Texture>>> _textureSeparate;

 public:
  Cubemap(std::shared_ptr<ImageDataBuffer> data,
          VkFormat format,
          int mipMapLevels,
          VkImageAspectFlagBits colorBits,
          VkImageUsageFlags usage,
          VkFilter filter,
          std::shared_ptr<EngineState> engineState);
  Cubemap(std::tuple<int, int> resolution,
          VkFormat format,
          int mipMapLevels,
          VkImageLayout layout,
          VkImageAspectFlagBits colorBits,
          VkImageUsageFlags usage,
          VkFilter filter,
          const RenderGraph::CommandBuffer* commandBufferTransfer,
          std::shared_ptr<EngineState> engineState);
  std::shared_ptr<RenderGraph::Texture> getTexture();
  std::vector<std::vector<std::shared_ptr<RenderGraph::Texture>>> getTextureSeparate();
};
