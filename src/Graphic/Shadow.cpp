#include <utility>
#include "Graphic/Shadow.h"

DirectionalShadowResources::DirectionalShadowResources(const RenderGraph::CommandBuffer* commandBufferTransfer,
                                                       std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  // create shadow map texture
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    const auto [width, height] = engineState->getSettings()->getShadowMapResolution();
    auto image = std::make_unique<RenderGraph::Image>(*_engineState->getMemoryAllocator());
    image->createImage(_engineState->getSettings()->getShadowMapFormat(), {width, height}, 1, 1,
                       VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    image->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, *commandBufferTransfer);
    auto imageView = std::make_shared<RenderGraph::ImageView>(std::move(image), *_engineState->getDevice());
    imageView->createImageView(VK_IMAGE_VIEW_TYPE_2D, 0, 0);
    // android doesn't support linear + d32 texture
    auto filter = VK_FILTER_NEAREST;
    if (_engineState->getDevice()->isFormatFeatureSupported(_engineState->getSettings()->getShadowMapFormat(),
                                                            VK_IMAGE_TILING_OPTIMAL,
                                                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
      filter = VK_FILTER_LINEAR;
    }

    auto sampler = std::make_shared<RenderGraph::Sampler>(*_engineState->getDevice());
    sampler->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1,
                           _engineState->getSettings()->getAnisotropicSamples(), filter);
    _shadowMapTexture.push_back(std::make_shared<RenderGraph::Texture>(std::move(imageView), std::move(sampler)));
  }
}

std::vector<std::shared_ptr<RenderGraph::Texture>> DirectionalShadowResources::getShadowMapTexture() {
  return _shadowMapTexture;
}

PointShadowResources::PointShadowResources(const RenderGraph::CommandBuffer* commandBufferTransfer,
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

std::vector<std::shared_ptr<Cubemap>> PointShadowResources::getShadowMapCubemap() { return _shadowMapCubemap; }

DirectionalShadowRenderer::DirectionalShadowRenderer(
    int shadowIndex,
    const std::vector<std::shared_ptr<Shadowable>>& shadowables,
    std::shared_ptr<EngineState> engineState)
    : _shadowables(&shadowables),
      _engineState(std::move(engineState)),
      _shadowIndex(shadowIndex) {}

void DirectionalShadowRenderer::draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  const auto logger = _engineState->getLogger();
  logger->begin("Directional shadow " + std::to_string(currentFrame), &commandBuffer);
  const auto [width, height] = _engineState->getSettings()->getShadowMapResolution();
  const VkClearAttachment clearAttachment{
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .colorAttachment = 0,
      .clearValue = {.color = {{1.f, 1.f, 1.f, 1.f}}},
  };
  const VkClearRect clearRect{
      .rect = {.offset = {0, 0},
               .extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}},
      .baseArrayLayer = 0,
      .layerCount = 1,
  };
  vkCmdClearAttachments(commandBuffer.getCommandBuffer(), 1, &clearAttachment, 1, &clearRect);
  vkCmdSetDepthBias(commandBuffer.getCommandBuffer(), _engineState->getSettings()->getDepthBiasConstant(), 0.f,
                    _engineState->getSettings()->getDepthBiasSlope());
  for (const auto& shadowable : *_shadowables) {
    logger->begin(shadowable->getName() + " to directional shadow " + std::to_string(currentFrame), &commandBuffer);
    shadowable->drawShadow(LightType::DIRECTIONAL, _shadowIndex, 0, &commandBuffer);
    logger->end(&commandBuffer);
  }
  logger->end(&commandBuffer);
}

void DirectionalShadowRenderer::update(int, const RenderGraph::CommandBuffer&) {}

void DirectionalShadowRenderer::reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>&) {}

PointShadowRenderer::PointShadowRenderer(std::shared_ptr<PointShadowResources> pointShadowResources,
                                         int shadowIndex,
                                         int face,
                                         const std::vector<std::shared_ptr<Shadowable>>& shadowables,
                                         std::shared_ptr<EngineState> engineState)
    : _pointShadowResources(std::move(pointShadowResources)),
      _shadowables(&shadowables),
      _engineState(std::move(engineState)),
      _shadowIndex(shadowIndex),
      _face(face) {}

void PointShadowRenderer::draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  const auto logger = _engineState->getLogger();
  logger->begin("Point shadow " + std::to_string(currentFrame), &commandBuffer);
  const auto [width, height] = _engineState->getSettings()->getShadowMapResolution();
  const VkClearAttachment clearAttachment{
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .colorAttachment = 0,
      .clearValue = {.color = {{1.f, 1.f, 1.f, 1.f}}},
  };
  const VkClearRect clearRect{
      .rect = {.offset = {0, 0},
               .extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}},
      .baseArrayLayer = 0,
      .layerCount = 1,
  };
  vkCmdClearAttachments(commandBuffer.getCommandBuffer(), 1, &clearAttachment, 1, &clearRect);
  vkCmdSetDepthBias(commandBuffer.getCommandBuffer(), _engineState->getSettings()->getDepthBiasConstant(), 0.f,
                    _engineState->getSettings()->getDepthBiasSlope());
  for (const auto& shadowable : *_shadowables) {
    logger->begin(shadowable->getName() + " to point shadow " + std::to_string(currentFrame), &commandBuffer);
    shadowable->drawShadow(LightType::POINT, _shadowIndex, _face, &commandBuffer);
    logger->end(&commandBuffer);
  }
  logger->end(&commandBuffer);
}

void PointShadowRenderer::update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) {
  const auto& image =
      _pointShadowResources->getShadowMapCubemap().at(currentFrame)->getTexture()->getImageView().getImage();
  const VkImageMemoryBarrier2 barrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image.getImage(),
      .subresourceRange =
          {
              .aspectMask = image.getAspectMask(),
              .baseMipLevel = 0,
              .levelCount = static_cast<uint32_t>(image.getMipMapNumber()),
              .baseArrayLayer = static_cast<uint32_t>(_face),
              .layerCount = 1,
          },
  };
  const VkDependencyInfo dependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(commandBuffer.getCommandBuffer(), &dependencyInfo);
}

void PointShadowRenderer::reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>&) {}
