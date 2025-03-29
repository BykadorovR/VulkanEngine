#include "Engine/Core.h"
#include "Primitive/TerrainInterpolation.h"
#include "Primitive/TerrainComposition.h"
#include <typeinfo>

#ifdef __ANDROID__
void Core::setAssetManager(AAssetManager* assetManager) { _assetManager = assetManager; }
void Core::setNativeWindow(ANativeWindow* window) { _nativeWindow = window; }
#endif

Core::Core(std::shared_ptr<Settings> settings) {
  _engineState = std::make_shared<EngineState>(settings);
#ifdef __ANDROID__
  _engineState->setNativeWindow(_nativeWindow);
  _engineState->setAssetManager(_assetManager);
#endif
  _engineState->initialize();
  _pool = std::make_shared<BS::thread_pool>(settings->getThreadsInPool());
  _swapchain = std::make_shared<Swapchain>(_engineState);
  _renderGraph = std::make_shared<RenderGraph>(_swapchain, _pool, _engineState);
  _timer = std::make_shared<Timer>(_engineState);
  _timerFPSReal = std::make_shared<TimerFPS>();
  _timerFPSLimited = std::make_shared<TimerFPS>();

  _engineState->getDebugUtils()->setName("Queue graphic", VkObjectType::VK_OBJECT_TYPE_QUEUE,
                                         _engineState->getDevice()->getQueue(vkb::QueueType::graphics));
  _engineState->getDebugUtils()->setName("Queue present", VkObjectType::VK_OBJECT_TYPE_QUEUE,
                                         _engineState->getDevice()->getQueue(vkb::QueueType::present));
  _engineState->getDebugUtils()->setName("Queue compute", VkObjectType::VK_OBJECT_TYPE_QUEUE,
                                         _engineState->getDevice()->getQueue(vkb::QueueType::compute));
  {
    _commandPoolApplication = std::make_shared<CommandPool>(vkb::QueueType::graphics, _engineState->getDevice());
    _commandBufferApplication.resize(settings->getMaxFramesInFlight());
    for (int i = 0; i < settings->getMaxFramesInFlight(); i++) {
      _commandBufferApplication[i] = std::make_shared<CommandBuffer>(_commandPoolApplication,
                                                                     _engineState->getDevice());
      _engineState->getDebugUtils()->setName("Command buffer for appplication",
                                             VkObjectType::VK_OBJECT_TYPE_COMMAND_BUFFER,
                                             _commandBufferApplication[i]->getCommandBuffer());
    }
  }

  _renderPassGraphic = _engineState->getRenderPassManager()->getRenderPass(RenderPassScenario::GRAPHIC);
  _renderPassShadowMap = _engineState->getRenderPassManager()->getRenderPass(RenderPassScenario::SHADOW);
  _renderPassDebug = _engineState->getRenderPassManager()->getRenderPass(RenderPassScenario::GUI);
  _renderPassBlur = _engineState->getRenderPassManager()->getRenderPass(RenderPassScenario::BLUR);
}

void Core::_initializeTextures() {
  auto settings = _engineState->getSettings();
  _textureRender.resize(settings->getMaxFramesInFlight());
  _textureBlurIn.resize(settings->getMaxFramesInFlight());
  _textureBlurOut.resize(settings->getMaxFramesInFlight());
  int frameInFlight = _engineState->getFrameInFlight();
  for (int i = 0; i < settings->getMaxFramesInFlight(); i++) {
    auto graphicImage = std::make_shared<Image>(settings->getResolution(), 1, 1, settings->getGraphicColorFormat(),
                                                VK_IMAGE_TILING_OPTIMAL,
                                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _engineState);
    graphicImage->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                               _commandBufferApplication[frameInFlight]);
    auto graphicImageView = std::make_shared<ImageView>(graphicImage, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1,
                                                        VK_IMAGE_ASPECT_COLOR_BIT, _engineState);
    _textureRender[i] = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1, VK_FILTER_LINEAR,
                                                  graphicImageView, _engineState);
    {
      auto blurImage = std::make_shared<Image>(settings->getResolution(), 1, 1, settings->getGraphicColorFormat(),
                                               VK_IMAGE_TILING_OPTIMAL,

                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _engineState);
      blurImage->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                              _commandBufferApplication[frameInFlight]);
      auto blurImageView = std::make_shared<ImageView>(blurImage, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1,
                                                       VK_IMAGE_ASPECT_COLOR_BIT, _engineState);
      _textureBlurIn[i] = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1, VK_FILTER_LINEAR,
                                                    blurImageView, _engineState);
    }
    {
      auto blurImage = std::make_shared<Image>(settings->getResolution(), 1, 1, settings->getGraphicColorFormat(),
                                               VK_IMAGE_TILING_OPTIMAL,
                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _engineState);
      blurImage->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                              _commandBufferApplication[frameInFlight]);
      auto blurImageView = std::make_shared<ImageView>(blurImage, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1,
                                                       VK_IMAGE_ASPECT_COLOR_BIT, _engineState);
      _textureBlurOut[i] = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1, VK_FILTER_LINEAR,
                                                     blurImageView, _engineState);
    }
  }

  auto depthAttachment = std::make_shared<Image>(settings->getResolution(), 1, 1, settings->getDepthFormat(),
                                                 VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _engineState);
  _depthAttachmentImageView = std::make_shared<ImageView>(depthAttachment, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1,
                                                          VK_IMAGE_ASPECT_DEPTH_BIT, _engineState);
}

void Core::_initializeFramebuffer() {
  _frameBufferGraphic.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _frameBufferGraphic[i] = std::make_shared<Framebuffer>(
        std::vector{_textureRender[i]->getImageView(), _textureBlurIn[i]->getImageView(), _depthAttachmentImageView},
        _textureRender[i]->getImageView()->getImage()->getResolution(), _renderPassGraphic, _engineState->getDevice());
  }

  _frameBufferDebug.resize(_swapchain->getImageViews().size());
  for (int i = 0; i < _frameBufferDebug.size(); i++) {
    _frameBufferDebug[i] = std::make_shared<Framebuffer>(std::vector{_swapchain->getImageViews()[i]},
                                                         _swapchain->getImageViews()[i]->getImage()->getResolution(),
                                                         _renderPassDebug, _engineState->getDevice());
  }
}

void Core::initialize() {
  try {
    int currentFrame = _engineState->getFrameInFlight();
    _commandBufferApplication[currentFrame]->beginCommands();
    // start transfer command buffer
    _initializeTextures();
    _initializeFramebuffer();

    // but we expect it to be in VK_IMAGE_LAYOUT_PRESENT_SRC_KHR as start value
    for (auto& imageView : _swapchain->getImageViews())
      imageView->getImage()->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                          VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, _commandBufferApplication[currentFrame]);

    _gameState = std::make_shared<GameState>(_commandBufferApplication[currentFrame], _engineState);
  } catch (std::exception e) {
    std::cerr << e.what() << std::endl;
  }
}

void Core::_computeParticles(int index, std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto logger = _engineState->getLogger();
  //_commandBufferParticleSystem[frameInFlight]
  commandBuffer->beginCommands();

  // any read from SSBO should wait for write to SSBO
  // First dispatch writes to a storage buffer, second dispatch reads from that storage buffer.
  VkMemoryBarrier memoryBarrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
  vkCmdPipelineBarrier(commandBuffer->getCommandBuffer(),
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // srcStageMask
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
                       0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

  logger->begin(
      "Particle system compute " + std::to_string(index) + ", timer: " + std::to_string(_timer->getFrameCounter()),
      commandBuffer);
  _particleSystems[index]->drawCompute(commandBuffer);
  _particleSystems[index]->updateTimer(_timer->getElapsedCurrent());

  logger->end(commandBuffer);
  commandBuffer->endCommands();
}

void Core::_drawShadowMapDirectional(int index, std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto shadow = _gameState->getLightManager()->getDirectionalShadows()[index];
  // auto commandBuffer = shadow->getShadowMapCommandBuffer(frameInFlight);
  auto logger = _engineState->getLogger();
  // record command buffer
  commandBuffer->beginCommands();
  logger->begin("Directional to depth buffer " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  //
  auto [widthFramebuffer, heightFramebuffer] = shadow->getShadowMapFramebuffer()[frameInFlight]->getResolution();
  VkClearValue clearColor{.color = {1.f, 1.f, 1.f, 1.f}};
  VkRenderPassBeginInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                       .renderPass = _renderPassShadowMap->getRenderPass(),
                                       .framebuffer = shadow->getShadowMapFramebuffer()[frameInFlight]->getBuffer(),
                                       .renderArea = {.offset = {0, 0},
                                                      .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                                                 .height = static_cast<uint32_t>(heightFramebuffer)}},
                                       .clearValueCount = 1,
                                       .pClearValues = &clearColor};

  // TODO: only one depth texture?
  vkCmdBeginRenderPass(commandBuffer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  // Set depth bias (aka "Polygon offset")
  // Required to avoid shadow mapping artifacts
  vkCmdSetDepthBias(commandBuffer->getCommandBuffer(), _engineState->getSettings()->getDepthBiasConstant(), 0.0f,
                    _engineState->getSettings()->getDepthBiasSlope());

  // draw scene here
  auto globalFrame = _timer->getFrameCounter();
  for (auto shadowable : _shadowables) {
    logger->begin(shadowable->getName() + " to directional depth buffer " + std::to_string(globalFrame), commandBuffer);
    shadowable->drawShadow(LightType::DIRECTIONAL, index, 0, commandBuffer);
    logger->end(commandBuffer);
  }
  vkCmdEndRenderPass(commandBuffer->getCommandBuffer());
  logger->end(commandBuffer);

  // record command buffer
  commandBuffer->endCommands();
}

void Core::_drawShadowMapPoint(int index, int face, std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto shadow = _gameState->getLightManager()->getPointShadows()[index];

  // auto commandBuffer = shadow->getShadowMapCommandBuffer(frameInFlight)[face];
  auto logger = _engineState->getLogger();

  // record command buffer
  commandBuffer->beginCommands();
  logger->begin("Point to depth buffer " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  auto [widthFramebuffer, heightFramebuffer] = shadow->getShadowMapFramebuffer()[frameInFlight][face]->getResolution();
  VkClearValue clearDepth{.color = {1.f, 1.f, 1.f, 1.f}};
  VkRenderPassBeginInfo renderPassInfo{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = _renderPassShadowMap->getRenderPass(),
      .framebuffer = shadow->getShadowMapFramebuffer()[frameInFlight][face]->getBuffer(),
      .renderArea = {.offset = {0, 0},
                     .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                .height = static_cast<uint32_t>(heightFramebuffer)}},
      .clearValueCount = 1,
      .pClearValues = &clearDepth};

  // TODO: only one depth texture?
  vkCmdBeginRenderPass(commandBuffer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  // Set depth bias (aka "Polygon offset")
  // Required to avoid shadow mapping artifacts
  vkCmdSetDepthBias(commandBuffer->getCommandBuffer(), _engineState->getSettings()->getDepthBiasConstant(), 0.0f,
                    _engineState->getSettings()->getDepthBiasSlope());

  // draw scene here
  auto globalFrame = _timer->getFrameCounter();
  float aspect = std::get<0>(_engineState->getSettings()->getResolution()) /
                 std::get<1>(_engineState->getSettings()->getResolution());

  // draw scene here
  for (auto shadowable : _shadowables) {
    logger->begin(shadowable->getName() + " to point depth buffer " + std::to_string(globalFrame), commandBuffer);
    shadowable->drawShadow(LightType::POINT, index, face, commandBuffer);
    logger->end(commandBuffer);
  }
  vkCmdEndRenderPass(commandBuffer->getCommandBuffer());
  logger->end(commandBuffer);

  // record command buffer
  commandBuffer->endCommands();
}

void Core::_drawShadowMapPointBlur(std::shared_ptr<PointShadow> pointShadow,
                                   int face,
                                   std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  // auto commandBufferBlur = _blurGraphicPoint[pointShadow]->getShadowMapBlurCommandBuffer(frameInFlight)[face];
  auto logger = _engineState->getLogger();

  commandBuffer->beginCommands();
  logger->begin("Blur point " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  auto [widthFramebuffer, heightFramebuffer] =
      _blurGraphicPoint[pointShadow]->getShadowMapBlurFramebuffer()[0][frameInFlight][face]->getResolution();
  VkClearValue clearDepth{.color = {0.f, 0.f, 0.f, 1.f}};
  VkRenderPassBeginInfo renderPassInfo{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = _renderPassBlur->getRenderPass(),
      .framebuffer = _blurGraphicPoint[pointShadow]->getShadowMapBlurFramebuffer()[0][frameInFlight][face]->getBuffer(),
      .renderArea = {.offset = {0, 0},
                     .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                .height = static_cast<uint32_t>(heightFramebuffer)}},
      .clearValueCount = 1,
      .pClearValues = &clearDepth};
  vkCmdBeginRenderPass(commandBuffer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  logger->begin("Horizontal " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  _blurGraphicPoint[pointShadow]->getBlur()[face]->draw(true, commandBuffer);
  logger->end(commandBuffer);
  vkCmdEndRenderPass(commandBuffer->getCommandBuffer());
  // wait blur image to be ready
  {
    VkImageMemoryBarrier imageMemoryBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                                            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                            .image = _blurGraphicPoint[pointShadow]
                                                         ->getShadowMapBlurCubemapOut()[frameInFlight]
                                                         ->getTextureSeparate()[face][0]
                                                         ->getImageView()
                                                         ->getImage()
                                                         ->getImage(),
                                            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(commandBuffer->getCommandBuffer(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
  }

  renderPassInfo = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = _renderPassBlur->getRenderPass(),
      .framebuffer = _blurGraphicPoint[pointShadow]->getShadowMapBlurFramebuffer()[1][frameInFlight][face]->getBuffer(),
      .renderArea = {.offset = {0, 0},
                     .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                .height = static_cast<uint32_t>(heightFramebuffer)}},
      .clearValueCount = 1,
      .pClearValues = &clearDepth};
  vkCmdBeginRenderPass(commandBuffer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  logger->begin("Vertical " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  // Vertical blur
  _blurGraphicPoint[pointShadow]->getBlur()[face]->draw(false, commandBuffer);
  logger->end(commandBuffer);
  vkCmdEndRenderPass(commandBuffer->getCommandBuffer());

  // wait blur image to be ready
  // blurTextureIn stores result after blur (vertical part, out - in)
  {
    auto pointShadows = _gameState->getLightManager()->getPointShadows();
    int indexShadow = std::distance(pointShadows.begin(), find(pointShadows.begin(), pointShadows.end(), pointShadow));
    VkImageMemoryBarrier imageMemoryBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                                            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                            .image = _gameState->getLightManager()
                                                         ->getPointShadows()[indexShadow]
                                                         ->getShadowMapCubemap()[frameInFlight]
                                                         ->getTextureSeparate()[face][0]
                                                         ->getImageView()
                                                         ->getImage()
                                                         ->getImage(),
                                            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(commandBuffer->getCommandBuffer(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
  }
  logger->end(commandBuffer);
  commandBuffer->endCommands();
}

void Core::_drawShadowMapDirectionalBlur(std::shared_ptr<DirectionalShadow> directionalShadow,
                                         std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  // auto commandBufferBlur = _blurGraphicDirectional[directionalShadow]->getShadowMapBlurCommandBuffer(frameInFlight);
  auto logger = _engineState->getLogger();

  commandBuffer->beginCommands();
  logger->begin("Blur directional " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  auto [widthFramebuffer, heightFramebuffer] =
      _blurGraphicDirectional[directionalShadow]->getShadowMapBlurFramebuffer()[0][frameInFlight]->getResolution();
  VkClearValue clearDepth{.color = {0.f, 0.f, 0.f, 1.f}};
  VkRenderPassBeginInfo renderPassInfo{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = _renderPassBlur->getRenderPass(),
      .framebuffer =
          _blurGraphicDirectional[directionalShadow]->getShadowMapBlurFramebuffer()[0][frameInFlight]->getBuffer(),
      .renderArea = {.offset = {0, 0},
                     .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                .height = static_cast<uint32_t>(heightFramebuffer)}},
      .clearValueCount = 1,
      .pClearValues = &clearDepth};
  vkCmdBeginRenderPass(commandBuffer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  logger->begin("Horizontal " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  _blurGraphicDirectional[directionalShadow]->getBlur()->draw(true, commandBuffer);
  logger->end(commandBuffer);
  vkCmdEndRenderPass(commandBuffer->getCommandBuffer());
  // wait blur image to be ready
  {
    VkImageMemoryBarrier imageMemoryBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                                            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                            .image = _blurGraphicDirectional[directionalShadow]
                                                         ->getShadowMapBlurTextureOut()[frameInFlight]
                                                         ->getImageView()
                                                         ->getImage()
                                                         ->getImage(),
                                            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(commandBuffer->getCommandBuffer(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
  }

  renderPassInfo = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = _renderPassBlur->getRenderPass(),
      .framebuffer =
          _blurGraphicDirectional[directionalShadow]->getShadowMapBlurFramebuffer()[1][frameInFlight]->getBuffer(),
      .renderArea = {.offset = {0, 0},
                     .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                .height = static_cast<uint32_t>(heightFramebuffer)}},
      .clearValueCount = 1,
      .pClearValues = &clearDepth};
  vkCmdBeginRenderPass(commandBuffer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  logger->begin("Vertical " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  // Vertical blur
  _blurGraphicDirectional[directionalShadow]->getBlur()->draw(false, commandBuffer);
  logger->end(commandBuffer);
  vkCmdEndRenderPass(commandBuffer->getCommandBuffer());

  // wait blur image to be ready
  // blurTextureIn stores result after blur (vertical part, out - in)
  {
    auto directionalShadows = _gameState->getLightManager()->getDirectionalShadows();
    int indexShadow = std::distance(directionalShadows.begin(),
                                    find(directionalShadows.begin(), directionalShadows.end(), directionalShadow));
    VkImageMemoryBarrier imageMemoryBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                                            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                            .image = _gameState->getLightManager()
                                                         ->getDirectionalShadows()[indexShadow]
                                                         ->getShadowMapTexture()[frameInFlight]
                                                         ->getImageView()
                                                         ->getImage()
                                                         ->getImage(),
                                            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(commandBuffer->getCommandBuffer(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
  }
  logger->end(commandBuffer);
  commandBuffer->endCommands();
}

void Core::_computeBloom(std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto logger = _engineState->getLogger();

  commandBuffer->beginCommands();
  int bloomPasses = _engineState->getSettings()->getBloomPasses();
  // blur cycle:
  // in - out - horizontal
  // out - in - vertical
  for (int i = 0; i < bloomPasses; i++) {
    logger->begin("Blur horizontal compute " + std::to_string(_timer->getFrameCounter()), commandBuffer);
    _blurCompute->draw(true, commandBuffer);
    logger->end(commandBuffer);

    // sync between horizontal and vertical
    // wait dst (textureOut) to be written
    {
      VkImageMemoryBarrier colorBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                                        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                        .image = _textureBlurOut[frameInFlight]->getImageView()->getImage()->getImage(),
                                        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                             .baseMipLevel = 0,
                                                             .levelCount = 1,
                                                             .baseArrayLayer = 0,
                                                             .layerCount = 1}};
      vkCmdPipelineBarrier(commandBuffer->getCommandBuffer(),
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // srcStageMask
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
                           0, 0, nullptr, 0, nullptr,
                           1,             // imageMemoryBarrierCount
                           &colorBarrier  // pImageMemoryBarriers
      );
    }

    logger->begin("Blur vertical compute " + std::to_string(_timer->getFrameCounter()), commandBuffer);
    _blurCompute->draw(false, commandBuffer);
    logger->end(commandBuffer);

    // wait blur image to be ready
    // blurTextureIn stores result after blur (vertical part, out - in)
    {
      VkImageMemoryBarrier colorBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                                        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                        .image = _textureBlurIn[frameInFlight]->getImageView()->getImage()->getImage(),
                                        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                             .baseMipLevel = 0,
                                                             .levelCount = 1,
                                                             .baseArrayLayer = 0,
                                                             .layerCount = 1}};
      vkCmdPipelineBarrier(commandBuffer->getCommandBuffer(),
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // srcStageMask
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
                           0, 0, nullptr, 0, nullptr,
                           1,             // imageMemoryBarrierCount
                           &colorBarrier  // pImageMemoryBarriers
      );
    }
  }

  commandBuffer->endCommands();
}

void Core::_computePostprocessing(std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto logger = _engineState->getLogger();
  auto swapchainImageIndex = _swapchain->getSwapchainIndex();

  commandBuffer->beginCommands();
  // wait dst image to be ready
  {
    VkImageMemoryBarrier colorBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                      .image = _swapchain->getImageViews()[swapchainImageIndex]->getImage()->getImage(),
                                      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                           .baseMipLevel = 0,
                                                           .levelCount = 1,
                                                           .baseArrayLayer = 0,
                                                           .layerCount = 1}};
    vkCmdPipelineBarrier(commandBuffer->getCommandBuffer(),
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,     // srcStageMask
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
                         0, 0, nullptr, 0, nullptr,
                         1,             // imageMemoryBarrierCount
                         &colorBarrier  // pImageMemoryBarriers
    );
  }

  logger->begin("Postprocessing compute " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  _postprocessing->drawCompute(frameInFlight, swapchainImageIndex, commandBuffer);
  logger->end(commandBuffer);
  commandBuffer->endCommands();
}

void Core::_debugVisualizations(std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto logger = _engineState->getLogger();
  auto swapchainImageIndex = _swapchain->getSwapchainIndex();

  commandBuffer->beginCommands();

  auto [widthFramebuffer, heightFramebuffer] = _frameBufferDebug[swapchainImageIndex]->getResolution();
  VkClearValue clearColor{.color = _engineState->getSettings()->getClearColor()};
  VkRenderPassBeginInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                       .renderPass = _renderPassDebug->getRenderPass(),
                                       .framebuffer = _frameBufferDebug[swapchainImageIndex]->getBuffer(),
                                       .renderArea = {.offset = {0, 0},
                                                      .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                                                 .height = static_cast<uint32_t>(heightFramebuffer)}},
                                       .clearValueCount = 1,
                                       .pClearValues = &clearColor};
  // TODO: only one depth texture?
  vkCmdBeginRenderPass(commandBuffer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  logger->begin("Render GUI " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  _gui->updateBuffers();
  _gui->drawFrame(commandBuffer);
  logger->end(commandBuffer);
  vkCmdEndRenderPass(commandBuffer->getCommandBuffer());
  commandBuffer->endCommands();
}

void Core::_renderGraphic(std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto logger = _engineState->getLogger();

  // record command buffer
  commandBuffer->beginCommands();
  /////////////////////////////////////////////////////////////////////////////////////////
  // depth to screne barrier
  /////////////////////////////////////////////////////////////////////////////////////////
  // Image memory barrier to make sure that writes are finished before sampling from the texture
  int directionalNum = _gameState->getLightManager()->getDirectionalLights().size();
  int pointNum = _gameState->getLightManager()->getPointLights().size();
  std::vector<VkImageMemoryBarrier> imageMemoryBarrier;
  for (int i = 0; i < directionalNum; i++) {
    if (_gameState->getLightManager()->getDirectionalShadows()[i]) {
      VkImageMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                   .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                   // We won't be changing the layout of the image
                                   .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                                   .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .image = _gameState->getLightManager()
                                                ->getDirectionalShadows()[i]
                                                ->getShadowMapTexture()[frameInFlight]
                                                ->getImageView()
                                                ->getImage()
                                                ->getImage(),
                                   .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

      imageMemoryBarrier.push_back(barrier);
    }
  }

  for (int i = 0; i < pointNum; i++) {
    if (_gameState->getLightManager()->getPointShadows()[i]) {
      VkImageMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                   .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                   .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                                   .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .image = _gameState->getLightManager()
                                                ->getPointShadows()[i]
                                                ->getShadowMapCubemap()[frameInFlight]
                                                ->getTexture()
                                                ->getImageView()
                                                ->getImage()
                                                ->getImage(),
                                   .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

      imageMemoryBarrier.push_back(barrier);
    }
  }
  vkCmdPipelineBarrier(commandBuffer->getCommandBuffer(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, imageMemoryBarrier.size(),
                       imageMemoryBarrier.data());

  /////////////////////////////////////////////////////////////////////////////////////////
  // render graphic
  /////////////////////////////////////////////////////////////////////////////////////////
  auto [widthFramebuffer, heightFramebuffer] = _frameBufferGraphic[frameInFlight]->getResolution();
  std::vector<VkClearValue> clearColor{{.color = _engineState->getSettings()->getClearColor()},
                                       {.color = _engineState->getSettings()->getClearColor()},
                                       {.color = {1.0f, 0}}};
  VkRenderPassBeginInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                       .renderPass = _renderPassGraphic->getRenderPass(),
                                       .framebuffer = _frameBufferGraphic[frameInFlight]->getBuffer(),
                                       .renderArea = {.offset = {0, 0},
                                                      .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                                                 .height = static_cast<uint32_t>(heightFramebuffer)}},
                                       .clearValueCount = 3,
                                       .pClearValues = clearColor.data()};

  auto globalFrame = _timer->getFrameCounter();
  // TODO: only one depth texture?
  vkCmdBeginRenderPass(commandBuffer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  logger->begin("Render light " + std::to_string(globalFrame), commandBuffer);
  _gameState->getLightManager()->draw(frameInFlight);
  logger->end(commandBuffer);

  // draw scene here
  for (auto& animation : _animations) {
    logger->begin("Update animation buffers " + std::to_string(globalFrame));
    if (_futureAnimationUpdate[animation].valid()) {
      _futureAnimationUpdate[animation].get();
    }
    animation->updateBuffers(_engineState->getFrameInFlight());
    logger->end();
  }

  // should be draw first
  if (_skybox) {
    logger->begin("Render skybox " + std::to_string(globalFrame), commandBuffer);
    _skybox->draw(commandBuffer);
    logger->end(commandBuffer);
  }

  for (auto& drawable : _drawables[AlphaType::OPAQUE]) {
    // TODO: add getName() to drawable?
    std::string drawableName = typeid(drawable.get()).name();
    logger->begin("Render " + drawable->getName() + " " + std::to_string(globalFrame), commandBuffer);
    drawable->draw(commandBuffer);
    logger->end(commandBuffer);
  }

  std::sort(_drawables[AlphaType::TRANSPARENT].begin(), _drawables[AlphaType::TRANSPARENT].end(),
            [camera = _gameState->getCameraManager()->getCurrentCamera()](std::shared_ptr<Drawable> left,
                                                                          std::shared_ptr<Drawable> right) {
              return glm::distance(glm::vec3(left->getModel()[3]), camera->getEye()) >
                     glm::distance(glm::vec3(right->getModel()[3]), camera->getEye());
            });
  for (auto& drawable : _drawables[AlphaType::TRANSPARENT]) {
    logger->begin("Render " + drawable->getName() + " " + std::to_string(globalFrame), commandBuffer);
    drawable->draw(commandBuffer);
    logger->end(commandBuffer);
  }

  // submit model3D update
  for (auto& animation : _animations) {
    _futureAnimationUpdate[animation] = _pool->submit([&, logger, frame = globalFrame]() {
      logger->begin("Calculate animation joints " + std::to_string(frame));
      // we want update model for next frame, current frame we can't touch and update because it will be used on GPU
      animation->calculateJoints(_timer->getElapsedCurrent());
      logger->end();
    });
  }

  vkCmdEndRenderPass(commandBuffer->getCommandBuffer());
  commandBuffer->endCommands();
}

VkResult Core::_getImageIndex() {
  auto frameInFlight = _engineState->getFrameInFlight();
  std::vector<VkFence> waitFences = {_renderGraph->getFenceInFlight()[frameInFlight]->getFence()};
  auto result = vkWaitForFences(_engineState->getDevice()->getLogicalDevice(), waitFences.size(), waitFences.data(),
                                VK_TRUE, UINT64_MAX);
  if (result != VK_SUCCESS) throw std::runtime_error("Can't wait for fence");

  // RETURNS ONLY INDEX, NOT IMAGE
  // semaphore to signal, once image is available
  result = vkAcquireNextImageKHR(_engineState->getDevice()->getLogicalDevice(), _swapchain->getSwapchain(), UINT64_MAX,
                                 _renderGraph->getSemaphoreImageAvailable()[frameInFlight]->getSemaphore(),
                                 VK_NULL_HANDLE, &_swapchain->getSwapchainIndex());

  if (result == VK_ERROR_OUT_OF_DATE_KHR || _engineState->getWindow()->getResized()) {
    _engineState->getWindow()->setResized(false);
    _reset();
    return VK_ERROR_OUT_OF_DATE_KHR;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  // Only reset the fence if we are submitting work
  result = vkResetFences(_engineState->getDevice()->getLogicalDevice(), waitFences.size(), waitFences.data());
  if (result != VK_SUCCESS) throw std::runtime_error("Can't reset fence");

  return result;
}

void Core::_reset() {
  auto frameInFlight = _engineState->getFrameInFlight();
  _swapchain->reset();
  _engineState->getSettings()->setResolution(
      {_swapchain->getSwapchain().extent.width, _swapchain->getSwapchain().extent.height});
  _textureRender.clear();
  _textureBlurIn.clear();
  _textureBlurOut.clear();

  _commandBufferApplication[frameInFlight]->beginCommands();

  _initializeTextures();
  for (auto& imageView : _swapchain->getImageViews()) imageView->getImage()->overrideLayout(VK_IMAGE_LAYOUT_GENERAL);
  _postprocessing->reset(_textureRender, _textureBlurIn, _swapchain->getImageViews());
  for (auto& imageView : _swapchain->getImageViews())
    imageView->getImage()->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                        VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, _commandBufferApplication[frameInFlight]);
  if (_gui) _gui->reset();
  if (_blurCompute) _blurCompute->reset(_textureBlurIn, _textureBlurOut);

  _initializeFramebuffer();

  _callbackReset(_swapchain->getSwapchain().extent.width, _swapchain->getSwapchain().extent.height);
}

void Core::_clearUnusedData() {
  int currentFrame = _engineState->getFrameInFlight();
  _unusedShadowable[currentFrame].clear();
  _unusedDrawable[currentFrame].clear();
}

void Core::_displayFrame() {
  auto frameInFlight = _engineState->getFrameInFlight();

  std::vector<VkSemaphore> waitSemaphoresPresent = {
      _renderGraph->getSemaphoreRenderFinished()[frameInFlight]->getSemaphore()};
  VkSwapchainKHR swapChains[] = {_swapchain->getSwapchain()};
  VkPresentInfoKHR presentInfo{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                               .waitSemaphoreCount = static_cast<uint32_t>(waitSemaphoresPresent.size()),
                               .pWaitSemaphores = waitSemaphoresPresent.data(),
                               .swapchainCount = 1,
                               .pSwapchains = swapChains,
                               .pImageIndices = &_swapchain->getSwapchainIndex()};

  // TODO: change to own present queue
  auto result = vkQueuePresentKHR(_engineState->getDevice()->getQueue(vkb::QueueType::present), &presentInfo);

  // getResized() can be valid only here, we can get inconsistencies in semaphores if VK_ERROR_OUT_OF_DATE_KHR is not
  // reported by Vulkan
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    _engineState->getWindow()->setResized(true);
  } else if (result != VK_SUCCESS) {
    throw std::runtime_error("failed to present swap chain image!");
  }
}

void Core::draw() {
  try {
#ifdef __ANDROID__
    {
#else
    while (!glfwWindowShouldClose((GLFWwindow*)(_engineState->getWindow()->getWindow()))) {
      glfwPollEvents();
#endif
      _timer->tick();
      _timerFPSReal->tick();
      _timerFPSLimited->tick();
      _engineState->setFrameInFlight(_timer->getFrameCounter() % _engineState->getSettings()->getMaxFramesInFlight());
      auto frameInFlight = _engineState->getFrameInFlight();
      std::vector<VkFence> waitFences = {_renderGraph->getFenceInFlight()[frameInFlight]->getFence()};
      auto result = vkWaitForFences(_engineState->getDevice()->getLogicalDevice(), waitFences.size(), waitFences.data(),
                                    VK_TRUE, UINT64_MAX);
      if (result != VK_SUCCESS) throw std::runtime_error("Can't wait for fence");

      result = VK_ERROR_OUT_OF_DATE_KHR;
      while (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // RETURNS ONLY INDEX, NOT IMAGE
        // semaphore to signal, once image is available
        result = vkAcquireNextImageKHR(_engineState->getDevice()->getLogicalDevice(), _swapchain->getSwapchain(),
                                       UINT64_MAX,
                                       _renderGraph->getSemaphoreImageAvailable()[frameInFlight]->getSemaphore(),
                                       VK_NULL_HANDLE, &_swapchain->getSwapchainIndex());

        if (result == VK_ERROR_OUT_OF_DATE_KHR || _engineState->getWindow()->getResized()) {
          result = VK_ERROR_OUT_OF_DATE_KHR;
          _engineState->getWindow()->setResized(false);
          _reset();
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
          throw std::runtime_error("failed to acquire swap chain image!");
        }
      }
      // Only reset the fence if we are submitting work
      result = vkResetFences(_engineState->getDevice()->getLogicalDevice(), waitFences.size(), waitFences.data());
      if (result != VK_SUCCESS) throw std::runtime_error("Can't reset fence");

      // clear removed entities: drawables and shadowables
      _clearUnusedData();
      // application update, can be anything

      if (_recalculateRenderGraph) {
        // application
        {
          auto applicationPass = _renderGraph->getPassApplication();
          applicationPass->setCommandBuffers(_commandBufferApplication);
          applicationPass->addRenderExecution([this](std::shared_ptr<CommandBuffer> commandBuffer) {
            if (commandBuffer->getActive() == false) commandBuffer->beginCommands();
            _callbackUpdate(commandBuffer);
            commandBuffer->endCommands();
          });
        }
        {
          // particle system
          for (int i = 0; i < _particleSystems.size(); i++) {
            auto computeParticlesPass = _renderGraph->getPass("Compute particles " + std::to_string(i),
                                                              GraphPassStage::COMPUTE);
            computeParticlesPass->addRenderExecution(
                std::bind(&Core::_computeParticles, this, i, std::placeholders::_1));
            std::vector<std::shared_ptr<Buffer>> inputBuffer(_engineState->getSettings()->getMaxFramesInFlight());
            std::vector<std::shared_ptr<Buffer>> outputBuffer(_engineState->getSettings()->getMaxFramesInFlight());
            for (int frame = 0; frame < _engineState->getSettings()->getMaxFramesInFlight(); frame++) {
              inputBuffer[frame] =
                  _particleSystems[i]
                      ->getParticlesBuffer()[abs(frame - 1) % _engineState->getSettings()->getMaxFramesInFlight()];
              outputBuffer[frame] = _particleSystems[i]->getParticlesBuffer()[frame];
            }
            computeParticlesPass->addStorageInput("Particles " + std::to_string(i), inputBuffer);
            computeParticlesPass->addStorageOutput("Particles " + std::to_string(i), outputBuffer);
          }
        }
        {
          // directional shadow
          auto directionalShadows = _gameState->getLightManager()->getDirectionalShadows();
          for (int i = 0; i < directionalShadows.size(); i++) {
            auto directionalShadowPass = _renderGraph->getPass("Directional shadow " + std::to_string(i),
                                                               GraphPassStage::GRAPHIC);
            directionalShadowPass->addRenderExecution(
                std::bind(&Core::_drawShadowMapDirectional, this, i, std::placeholders::_1));

            std::vector<std::shared_ptr<Image>> imagesShadow;
            for (auto& texture : directionalShadows[i]->getShadowMapTexture()) {
              imagesShadow.push_back(texture->getImageView()->getImage());
            }
            directionalShadowPass->addColorTarget("Directional shadow " + std::to_string(i), imagesShadow);

            // directional shadow blur
            if (_blurGraphicDirectional.find(directionalShadows[i]) != _blurGraphicDirectional.end()) {
              auto directionalShadowBlurPass = _renderGraph->getPass("Directional shadow blur " + std::to_string(i),
                                                                     GraphPassStage::GRAPHIC);
              directionalShadowBlurPass->addRenderExecution(
                  std::bind(&Core::_drawShadowMapDirectionalBlur, this, directionalShadows[i], std::placeholders::_1));
              std::vector<std::shared_ptr<Image>> imagesBlur;
              for (auto& texture : _blurGraphicDirectional[directionalShadows[i]]->getShadowMapBlurTextureOut()) {
                imagesBlur.push_back(texture->getImageView()->getImage());
              }
              directionalShadowBlurPass->addColorTarget("Directional shadow blur " + std::to_string(i), imagesBlur);
              directionalShadowBlurPass->addTextureInput("Directional shadow " + std::to_string(i), imagesShadow);
            }
          }
        }
        {
          // point shadow
          auto pointShadows = _gameState->getLightManager()->getPointShadows();
          for (int i = 0; i < pointShadows.size(); i++) {
            auto pointShadowPass = _renderGraph->getPass("Point shadow " + std::to_string(i), GraphPassStage::GRAPHIC);
            for (int face = 0; face < 6; face++) {
              pointShadowPass->addRenderExecution(
                  std::bind(&Core::_drawShadowMapPoint, this, i, face, std::placeholders::_1));
            }
            std::vector<std::shared_ptr<Image>> imagesShadow;
            for (auto& cubemap : pointShadows[i]->getShadowMapCubemap()) {
              imagesShadow.push_back(cubemap->getTexture()->getImageView()->getImage());
            }
            pointShadowPass->addColorTarget("Point shadow " + std::to_string(i), imagesShadow);

            // point shadow blur
            if (_blurGraphicPoint.find(pointShadows[i]) != _blurGraphicPoint.end()) {
              auto pointShadowBlurPass = _renderGraph->getPass("Point shadow blur " + std::to_string(i),
                                                               GraphPassStage::GRAPHIC);
              for (int face = 0; face < 6; face++) {
                pointShadowPass->addRenderExecution(
                    std::bind(&Core::_drawShadowMapPointBlur, this, pointShadows[i], face, std::placeholders::_1));
              }
              std::vector<std::shared_ptr<Image>> imagesBlur;
              for (auto& cubemap : _blurGraphicPoint[pointShadows[i]]->getShadowMapBlurCubemapOut()) {
                imagesBlur.push_back(cubemap->getTexture()->getImageView()->getImage());
              }
              pointShadowBlurPass->addColorTarget("Point shadow blur " + std::to_string(i), imagesBlur);
              pointShadowBlurPass->addTextureInput("Point shadow " + std::to_string(i), imagesShadow);
            }
          }
        }
        {
          // bloom blur
          if (_blurCompute) {
            auto blurPass = _renderGraph->getPass("Bloom blur", GraphPassStage::COMPUTE);
            blurPass->addRenderExecution(std::bind(&Core::_computeBloom, this, std::placeholders::_1));
            std::vector<std::shared_ptr<Image>> imagesBlurInput;
            for (auto& texture : _textureBlurIn) imagesBlurInput.push_back(texture->getImageView()->getImage());
            blurPass->addTextureInput("Bloom blur input", imagesBlurInput);
            blurPass->addColorTarget("Bloom blur output", imagesBlurInput);
          }
        }
        {
          // postprocessing
          if (_postprocessing) {
            auto postprocessingPass = _renderGraph->getPass("Postprocessing", GraphPassStage::COMPUTE);
            postprocessingPass->addRenderExecution(
                std::bind(&Core::_computePostprocessing, this, std::placeholders::_1));
            std::vector<std::shared_ptr<Image>> imagesInput;
            for (auto& texture : _textureRender) imagesInput.push_back(texture->getImageView()->getImage());
            postprocessingPass->addTextureInput("Primitives", imagesInput);
            if (_blurCompute) {
              postprocessingPass->addTextureInput(
                  "Bloom blur output",
                  _renderGraph->getPass("Bloom blur", GraphPassStage::COMPUTE)->getColorTargets()["Bloom blur output"]);
            }
            std::vector<std::shared_ptr<Image>> imagesOutput;
            for (auto& imageViews : _swapchain->getImageViews()) imagesOutput.push_back(imageViews->getImage());
            postprocessingPass->addColorTarget("Postprocessing", imagesOutput);
          }
        }
        {
          // render
          auto renderPass = _renderGraph->getPass("Render", GraphPassStage::GRAPHIC);
          renderPass->addRenderExecution(std::bind(&Core::_renderGraphic, this, std::placeholders::_1));
          for (int i = 0; i < _particleSystems.size(); i++) {
            renderPass->addVertexBufferInput(
                "Particles " + std::to_string(i),
                _renderGraph->getPass("Compute particles " + std::to_string(i), GraphPassStage::COMPUTE)
                    ->getStorageOutputs()["Particles " + std::to_string(i)]);
          }
          auto directionalShadows = _gameState->getLightManager()->getDirectionalShadows();
          for (int i = 0; i < directionalShadows.size(); i++) {
            if (_blurGraphicDirectional.find(directionalShadows[i]) != _blurGraphicDirectional.end()) {
              renderPass->addTextureInput(
                  "Directional shadow blur " + std::to_string(i),
                  _renderGraph->getPass("Directional shadow blur " + std::to_string(i), GraphPassStage::GRAPHIC)
                      ->getColorTargets()["Directional shadow blur " + std::to_string(i)]);
            } else {
              renderPass->addTextureInput(
                  "Directional shadow " + std::to_string(i),
                  _renderGraph->getPass("Directional shadow " + std::to_string(i), GraphPassStage::GRAPHIC)
                      ->getColorTargets()["Directional shadow " + std::to_string(i)]);
            }
          }
          auto pointShadows = _gameState->getLightManager()->getPointShadows();
          for (int i = 0; i < pointShadows.size(); i++) {
            if (_blurGraphicPoint.find(pointShadows[i]) != _blurGraphicPoint.end()) {
              renderPass->addTextureInput(
                  "Point shadow blur " + std::to_string(i),
                  _renderGraph->getPass("Point shadow blur " + std::to_string(i), GraphPassStage::GRAPHIC)
                      ->getColorTargets()["Point shadow blur " + std::to_string(i)]);
            } else {
              renderPass->addTextureInput(
                  "Point shadow " + std::to_string(i),
                  _renderGraph->getPass("Point shadow " + std::to_string(i), GraphPassStage::GRAPHIC)
                      ->getColorTargets()["Point shadow " + std::to_string(i)]);
            }
          }

          std::vector<std::shared_ptr<Image>> imagePrimitives;
          for (auto& texture : _textureRender) imagePrimitives.push_back(texture->getImageView()->getImage());
          std::vector<std::shared_ptr<Image>> imageBlur;
          for (auto& texture : _textureBlurIn) imageBlur.push_back(texture->getImageView()->getImage());
          renderPass->addColorTarget("Primitives", imagePrimitives);
          renderPass->addColorTarget("Bloom blur input", imageBlur);
          renderPass->setDepthTarget("Depth", _depthAttachmentImageView->getImage());
        }
        {
          // gui
          if (_gui) {
            auto guiPass = _renderGraph->getPass("GUI", GraphPassStage::GRAPHIC);
            guiPass->setEnd(true);
            guiPass->addRenderExecution(std::bind(&Core::_debugVisualizations, this, std::placeholders::_1));
            std::vector<std::shared_ptr<Image>> images;
            for (auto& imageView : _swapchain->getImageViews()) images.push_back(imageView->getImage());
            guiPass->addColorTarget("GUI", images);
            if (_postprocessing) {
              guiPass->addTextureInput("Postprocessing",
                                       _renderGraph->getPass("Postprocessing", GraphPassStage::COMPUTE)
                                           ->getColorTargets()["Postprocessing"]);
            } else {
              guiPass->addTextureInput(
                  "Primitives",
                  _renderGraph->getPass("Render", GraphPassStage::GRAPHIC)->getColorTargets()["Primitives"]);
            }
          }
        }
        _renderGraph->calculate();
        _renderGraph->print();
        _recalculateRenderGraph = false;
      }
      // render scene
      _gameState->getCameraManager()->update();

      // first update materials
      for (auto& e : _materials) {
        e->update(frameInFlight);
      }

      _renderGraph->render();

      _timerFPSReal->tock();
      // if GPU frames are limited by driver it will happen during display
      _displayFrame();

      _timer->sleep(_engineState->getSettings()->getDesiredFPS());
      _timer->tock();
      _timerFPSLimited->tock();
    }
#ifndef __ANDROID__
    vkDeviceWaitIdle(_engineState->getDevice()->getLogicalDevice());
#endif
  } catch (std::exception e) {
    std::cerr << e.what() << std::endl;
  }
}

void Core::registerUpdate(std::function<void(std::shared_ptr<CommandBuffer>)> update) { _callbackUpdate = update; }

void Core::registerReset(std::function<void(int, int)> reset) { _callbackReset = reset; }

void Core::setCamera(std::shared_ptr<Camera> camera) { _gameState->getCameraManager()->setCurrentCamera(camera); }

void Core::addDrawable(std::shared_ptr<Drawable> drawable, AlphaType type) {
  if (drawable == nullptr) return;

  auto position = std::find(_drawables[type].begin(), _drawables[type].end(), drawable);
  // add only if doesn't exist already
  if (position == _drawables[type].end()) _drawables[type].push_back(drawable);
}

void Core::addShadowable(std::shared_ptr<Shadowable> shadowable) { _shadowables.push_back(shadowable); }

void Core::addSkybox(std::shared_ptr<Skybox> skybox) { _skybox = skybox; }

void Core::removeDrawable(std::shared_ptr<Drawable> drawable) {
  if (drawable == nullptr) return;

  for (auto& [_, drawableVector] : _drawables) {
    auto position = std::find(drawableVector.begin(), drawableVector.end(), drawable);
    // we can remove this object only after current frame on GPU ends processing
    if (position != drawableVector.end()) {
      _unusedDrawable[(_engineState->getFrameInFlight() + 1) % _engineState->getSettings()->getMaxFramesInFlight()]
          .push_back(*position);
      drawableVector.erase(position);
      break;
    }
  }
}

void Core::removeShadowable(std::shared_ptr<Shadowable> shadowable) {
  if (shadowable == nullptr) return;

  auto position = std::find(_shadowables.begin(), _shadowables.end(), shadowable);
  // we can remove this object only after current frame on GPU ends processing
  if (position != _shadowables.end()) {
    _unusedShadowable[(_engineState->getFrameInFlight() + 1) % _engineState->getSettings()->getMaxFramesInFlight()]
        .push_back(*position);
    _shadowables.erase(position);
  }
}

std::shared_ptr<ImageCPU<uint8_t>> Core::loadImageCPU(std::string path) {
  return _gameState->getResourceManager()->loadImageCPU<uint8_t>(path);
}

std::shared_ptr<BufferImage> Core::loadImageGPU(std::shared_ptr<ImageCPU<uint8_t>> imageCPU) {
  return _gameState->getResourceManager()->loadImageGPU<uint8_t>({imageCPU});
}

std::shared_ptr<Texture> Core::createTexture(std::string path, VkFormat format, int mipMapLevels) {
  auto imageCPU = loadImageCPU(path);
  auto texture = std::make_shared<Texture>(loadImageGPU(imageCPU), format, VK_SAMPLER_ADDRESS_MODE_REPEAT, mipMapLevels,
                                           VK_FILTER_LINEAR,
                                           _commandBufferApplication[_engineState->getFrameInFlight()], _engineState);
  return texture;
}

std::shared_ptr<Cubemap> Core::createCubemap(std::vector<std::string> paths, VkFormat format, int mipMapLevels) {
  std::vector<std::shared_ptr<ImageCPU<uint8_t>>> images;
  for (auto path : paths) {
    images.push_back(loadImageCPU(path));
  }
  return std::make_shared<Cubemap>(
      _gameState->getResourceManager()->loadImageGPU<uint8_t>(images), format, mipMapLevels, VK_IMAGE_ASPECT_COLOR_BIT,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FILTER_LINEAR,
      _commandBufferApplication[_engineState->getFrameInFlight()], _engineState);
}

std::shared_ptr<ModelGLTF> Core::createModelGLTF(std::string path) {
  int frameInFlight = _engineState->getFrameInFlight();
  auto model = _gameState->getResourceManager()->loadModel(path, _commandBufferApplication[frameInFlight]);
  _materials.insert(model->getMaterialsColor().begin(), model->getMaterialsColor().end());
  _materials.insert(model->getMaterialsPhong().begin(), model->getMaterialsPhong().end());
  _materials.insert(model->getMaterialsPBR().begin(), model->getMaterialsPBR().end());
  return model;
}

std::shared_ptr<Animation> Core::createAnimation(std::shared_ptr<ModelGLTF> modelGLTF) {
  auto animation = std::make_shared<Animation>(modelGLTF->getNodes(), modelGLTF->getSkins(), modelGLTF->getAnimations(),
                                               _engineState);
  _animations.push_back(animation);
  return animation;
}

std::shared_ptr<Equirectangular> Core::createEquirectangular(std::string path) {
  return std::make_shared<Equirectangular>(_gameState->getResourceManager()->loadImageCPU<float>(path),
                                           _commandBufferApplication[_engineState->getFrameInFlight()], _engineState);
}

std::shared_ptr<MaterialColor> Core::createMaterialColor(MaterialTarget target) {
  auto material = std::make_shared<MaterialColor>(target, _commandBufferApplication[_engineState->getFrameInFlight()],
                                                  _engineState);
  _materials.insert(material);
  return material;
}

std::shared_ptr<MaterialPhong> Core::createMaterialPhong(MaterialTarget target) {
  auto material = std::make_shared<MaterialPhong>(target, _commandBufferApplication[_engineState->getFrameInFlight()],
                                                  _engineState);
  _materials.insert(material);
  return material;
}

std::shared_ptr<MaterialPBR> Core::createMaterialPBR(MaterialTarget target) {
  auto material = std::make_shared<MaterialPBR>(target, _commandBufferApplication[_engineState->getFrameInFlight()],
                                                _engineState);
  _materials.insert(material);
  return material;
}

std::shared_ptr<Shape3D> Core::createShape3D(ShapeType shapeType,
                                             std::shared_ptr<Mesh3D> mesh,
                                             VkCullModeFlagBits cullMode) {
  return std::make_shared<Shape3D>(
      shapeType, mesh, cullMode, _commandBufferApplication[_engineState->getFrameInFlight()], _gameState, _engineState);
}

std::shared_ptr<Model3D> Core::createModel3D(std::shared_ptr<ModelGLTF> modelGLTF) {
  return std::make_shared<Model3D>(modelGLTF->getNodes(), modelGLTF->getMeshes(),
                                   _commandBufferApplication[_engineState->getFrameInFlight()], _gameState,
                                   _engineState);
}

std::shared_ptr<Sprite> Core::createSprite() {
  return std::make_shared<Sprite>(_commandBufferApplication[_engineState->getFrameInFlight()], _gameState,
                                  _engineState);
}

std::shared_ptr<TerrainGPU> Core::createTerrainInterpolation(std::shared_ptr<ImageCPU<uint8_t>> heightmap) {
  return std::make_shared<TerrainInterpolation>(heightmap, _gameState, _engineState);
}

std::shared_ptr<TerrainGPU> Core::createTerrainComposition(std::shared_ptr<ImageCPU<uint8_t>> heightmap) {
  return std::make_shared<TerrainComposition>(heightmap, _gameState, _engineState);
}

std::shared_ptr<TerrainCPU> Core::createTerrainCPU(std::shared_ptr<ImageCPU<uint8_t>> heightmap) {
  return std::make_shared<TerrainCPU>(heightmap, _gameState, _engineState);
}

std::shared_ptr<TerrainCPU> Core::createTerrainCPU(std::vector<float> heights, std::tuple<int, int> resolution) {
  return std::make_shared<TerrainCPU>(heights, resolution, _gameState, _engineState);
}

std::shared_ptr<Line> Core::createLine() {
  return std::make_shared<Line>(_commandBufferApplication[_engineState->getFrameInFlight()], _gameState, _engineState);
}

std::shared_ptr<IBL> Core::createIBL() {
  return std::make_shared<IBL>(_commandBufferApplication[_engineState->getFrameInFlight()], _gameState, _engineState);
}

std::shared_ptr<ParticleSystem> Core::createParticleSystem(std::vector<Particle> particles,
                                                           std::shared_ptr<Texture> particleTexture) {
  auto particleSystem = std::make_shared<ParticleSystem>(particles, particleTexture,
                                                         _commandBufferApplication[_engineState->getFrameInFlight()],
                                                         _gameState, _engineState);
  addDrawable(particleSystem);
  _particleSystems.push_back(particleSystem);

  return particleSystem;
}

std::shared_ptr<Skybox> Core::createSkybox() {
  return std::make_shared<Skybox>(_commandBufferApplication[_engineState->getFrameInFlight()], _gameState,
                                  _engineState);
}

std::shared_ptr<PointShadow> Core::createPointShadow(std::shared_ptr<PointLight> pointLight, bool blur) {
  auto shadow = _gameState->getLightManager()->createPointShadow(
      pointLight, _renderPassShadowMap, _commandBufferApplication[_engineState->getFrameInFlight()]);

  if (blur) {
    _blurGraphicPoint[shadow] = std::make_shared<PointShadowBlur>(
        shadow->getShadowMapCubemap(), _commandBufferApplication[_engineState->getFrameInFlight()],
        _renderPassShadowMap, _engineState);
  }

  return shadow;
}

std::shared_ptr<PhysicsManager> Core::createPhysicsManager() {
  return std::make_shared<PhysicsManager>(_pool, _engineState);
}

std::shared_ptr<GUI> Core::createGUI() {
  _gui = std::make_shared<GUI>(_engineState);
  _gui->initialize(_commandBufferApplication[_engineState->getFrameInFlight()]);
  _engineState->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriberExclusive>(_gui));

  return _gui;
}

std::shared_ptr<BlurCompute> Core::createBloomBlur() {
  _blurCompute = std::make_shared<BlurCompute>(_textureBlurIn, _textureBlurOut, _engineState);

  return _blurCompute;
}

std::shared_ptr<Postprocessing> Core::createPostprocessing() {
  _postprocessing = std::make_shared<Postprocessing>(_textureRender, _textureBlurIn, _swapchain->getImageViews(),
                                                     _engineState);
  return _postprocessing;
}

std::shared_ptr<DirectionalShadow> Core::createDirectionalShadow(std::shared_ptr<DirectionalLight> directionalLight,
                                                                 bool blur) {
  auto shadow = _gameState->getLightManager()->createDirectionalShadow(
      directionalLight, _renderPassShadowMap, _commandBufferApplication[_engineState->getFrameInFlight()]);

  if (blur) {
    _blurGraphicDirectional[shadow] = std::make_shared<DirectionalShadowBlur>(
        shadow->getShadowMapTexture(), _commandBufferApplication[_engineState->getFrameInFlight()],
        _renderPassShadowMap, _engineState);
  }

  return shadow;
}

std::shared_ptr<PointLight> Core::createPointLight() {
  auto light = _gameState->getLightManager()->createPointLight();
  return light;
}

std::shared_ptr<DirectionalLight> Core::createDirectionalLight() {
  auto light = _gameState->getLightManager()->createDirectionalLight();
  return light;
}

std::shared_ptr<AmbientLight> Core::createAmbientLight() { return _gameState->getLightManager()->createAmbientLight(); }

std::shared_ptr<CommandBuffer> Core::getCommandBufferApplication() {
  return _commandBufferApplication[_engineState->getFrameInFlight()];
}

std::shared_ptr<ResourceManager> Core::getResourceManager() { return _gameState->getResourceManager(); }

const std::vector<std::shared_ptr<Drawable>>& Core::getDrawables(AlphaType type) { return _drawables[type]; }

std::vector<std::shared_ptr<PointLight>> Core::getPointLights() {
  return _gameState->getLightManager()->getPointLights();
}

std::vector<std::shared_ptr<DirectionalLight>> Core::getDirectionalLights() {
  return _gameState->getLightManager()->getDirectionalLights();
}

std::vector<std::shared_ptr<PointShadow>> Core::getPointShadows() {
  return _gameState->getLightManager()->getPointShadows();
}

std::vector<std::shared_ptr<DirectionalShadow>> Core::getDirectionalShadows() {
  return _gameState->getLightManager()->getDirectionalShadows();
}

std::shared_ptr<Postprocessing> Core::getPostprocessing() { return _postprocessing; }

std::shared_ptr<EngineState> Core::getEngineState() { return _engineState; }

std::shared_ptr<GameState> Core::getGameState() { return _gameState; }

std::shared_ptr<Camera> Core::getCamera() { return _gameState->getCameraManager()->getCurrentCamera(); }

std::tuple<int, int> Core::getFPS() { return {_timerFPSLimited->getFPS(), _timerFPSReal->getFPS()}; }