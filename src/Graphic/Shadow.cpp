#include "Graphic/Shadow.h"

DirectionalShadow::DirectionalShadow(std::shared_ptr<CommandBuffer> commandBufferTransfer,
                                     std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  // create shadow map texture
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    std::shared_ptr<Image> image = std::make_shared<Image>(
        engineState->getSettings()->getShadowMapResolution(), 1, 1, _engineState->getSettings()->getShadowMapFormat(),
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, _engineState);
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
}

std::vector<std::shared_ptr<Texture>> DirectionalShadow::getShadowMapTexture() { return _shadowMapTexture; }

PointShadow::PointShadow(std::shared_ptr<CommandBuffer> commandBufferTransfer,
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
}

std::vector<std::shared_ptr<Cubemap>> PointShadow::getShadowMapCubemap() { return _shadowMapCubemap; }