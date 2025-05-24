#include "Graphic/Shadow.h"

DirectionalShadow::DirectionalShadow(std::shared_ptr<CommandBuffer> commandBufferTransfer,
                                     std::shared_ptr<RenderPass> renderPass,
                                     std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  // create shadow map texture
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    std::shared_ptr<Image> image = std::make_shared<Image>(
        engineState->getSettings()->getShadowMapResolution(), 1, 1, _engineState->getSettings()->getShadowMapFormat(),
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _engineState);
    image->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                        commandBufferTransfer);
    auto imageView = std::make_shared<ImageView>(image, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1, VK_IMAGE_ASPECT_COLOR_BIT,
                                                 _engineState);
    // android doesn't support linear + d32 texture
    auto filter = VK_FILTER_NEAREST;
    if (_engineState->getDevice()->isFormatFeatureSupported(_engineState->getSettings()->getShadowMapFormat(),
                                                            VK_IMAGE_TILING_OPTIMAL,
                                                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
      filter = VK_FILTER_LINEAR;
    }

    _shadowMapTexture.push_back(
        std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1, filter, imageView, _engineState));
  }
  // create command buffer for rendering to shadow map
  auto commandPool = std::make_shared<CommandPool>(vkb::QueueType::graphics, _engineState->getDevice());
  _commandBufferDirectional.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _commandBufferDirectional[i] = std::make_shared<CommandBuffer>(commandPool, _engineState->getDevice());
    _engineState->getDebugUtils()->setName("Command buffer directional ", VkObjectType::VK_OBJECT_TYPE_COMMAND_BUFFER,
                                           _commandBufferDirectional[i]->getCommandBuffer());
  }
  // create framebuffer to render shadow map to
  _shadowMapFramebuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _shadowMapFramebuffer[i] = std::make_shared<Framebuffer>(
        std::vector{_shadowMapTexture[i]->getImageView()},
        _shadowMapTexture[i]->getImageView()->getImage()->getResolution(), renderPass, _engineState->getDevice());
  }
}
std::shared_ptr<CommandBuffer> DirectionalShadow::getShadowMapCommandBuffer(int frameInFlight) {
  return _commandBufferDirectional[frameInFlight];
}

std::vector<std::shared_ptr<Texture>> DirectionalShadow::getShadowMapTexture() { return _shadowMapTexture; }

std::vector<std::shared_ptr<Framebuffer>> DirectionalShadow::getShadowMapFramebuffer() { return _shadowMapFramebuffer; }

PointShadow::PointShadow(std::shared_ptr<CommandBuffer> commandBufferTransfer,
                         std::shared_ptr<RenderPass> renderPass,
                         std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  // create shadow map cubemap texture
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    auto filter = VK_FILTER_NEAREST;
    if (_engineState->getDevice()->isFormatFeatureSupported(_engineState->getSettings()->getShadowMapFormat(),
                                                            VK_IMAGE_TILING_OPTIMAL,
                                                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
      filter = VK_FILTER_LINEAR;
    }
    _shadowMapCubemap.push_back(std::make_shared<Cubemap>(
        engineState->getSettings()->getShadowMapResolution(), _engineState->getSettings()->getShadowMapFormat(), 1,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, filter, commandBufferTransfer, _engineState));
  }
  // should be unique command pool for every command buffer to work in parallel.
  // the same with logger, it's binded to command buffer (//TODO: maybe fix somehow)
  _commandBufferPoint.resize(engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _commandBufferPoint[i].resize(6);
    for (int j = 0; j < 6; j++) {
      auto commandPool = std::make_shared<CommandPool>(vkb::QueueType::graphics, _engineState->getDevice());
      _commandBufferPoint[i][j] = std::make_shared<CommandBuffer>(commandPool, _engineState->getDevice());
      _engineState->getDebugUtils()->setName("Command buffer point " + std::to_string(j),
                                             VkObjectType::VK_OBJECT_TYPE_COMMAND_BUFFER,
                                             _commandBufferPoint[i][j]->getCommandBuffer());
    }
  }
  // create framebuffer to render shadow map to
  _shadowMapFramebuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    std::vector<std::shared_ptr<Framebuffer>> pointLightFaces(6);
    for (int j = 0; j < 6; j++) {
      auto imageView = _shadowMapCubemap[i]->getTextureSeparate()[j][0]->getImageView();
      pointLightFaces[j] = std::make_shared<Framebuffer>(std::vector{imageView}, imageView->getImage()->getResolution(),
                                                         renderPass, _engineState->getDevice());
    }
    _shadowMapFramebuffer[i] = pointLightFaces;
  }
}

std::vector<std::shared_ptr<CommandBuffer>> PointShadow::getShadowMapCommandBuffer(int frameInFlight) {
  return _commandBufferPoint[frameInFlight];
}

std::vector<std::shared_ptr<Cubemap>> PointShadow::getShadowMapCubemap() { return _shadowMapCubemap; }

std::vector<std::vector<std::shared_ptr<Framebuffer>>> PointShadow::getShadowMapFramebuffer() {
  return _shadowMapFramebuffer;
}