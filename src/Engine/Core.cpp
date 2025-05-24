#include "Engine/Core.h"
#include "Primitive/TerrainInterpolation.h"
#include "Primitive/TerrainComposition.h"
#include <typeinfo>

#ifdef __ANDROID__
void Core::setAssetManager(AAssetManager* assetManager) { _assetManager = assetManager; }
void Core::setNativeWindow(ANativeWindow* window) { _nativeWindow = window; }
#endif

Core::Core(std::shared_ptr<Settings> settings) { _engineState = std::make_shared<EngineState>(settings); }

void Core::_initializeTextures() {
  auto settings = _engineState->getSettings();
  _textureBlurIn.resize(settings->getMaxFramesInFlight());
  _textureBlurOut.resize(settings->getMaxFramesInFlight());
  int frameInFlight = _engineState->getFrameInFlight();
  for (int i = 0; i < settings->getMaxFramesInFlight(); i++) {
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
  for (int f = 0; f < _engineState->getSettings()->getMaxFramesInFlight(); f++) {
    for (int s = 0; s < _swapchain->getImageViews().size(); s++) {
      _frameBufferGraphic[{f, s}] = std::make_shared<Framebuffer>(
          std::vector{_swapchain->getImageViews()[s], _textureBlurIn[f]->getImageView(), _depthAttachmentImageView},
          _swapchain->getImageViews()[s]->getImage()->getResolution(), _renderPassGraphic, _engineState->getDevice());
    }
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
#ifdef __ANDROID__
    _engineState->setNativeWindow(_nativeWindow);
    _engineState->setAssetManager(_assetManager);
#endif
    _engineState->initialize();
    auto settings = _engineState->getSettings();
    _swapchain = std::make_shared<Swapchain>(_engineState);
    if (_swapchain->getImageViews().size() < _engineState->getSettings()->getMaxFramesInFlight())
      throw std::invalid_argument(
          "Frame in flights number " + std::to_string(_engineState->getSettings()->getMaxFramesInFlight()) +
          " can't be greater than swapchain size " + std::to_string(_swapchain->getImageViews().size()));

    _pool = std::make_shared<BS::thread_pool>(settings->getThreadsInPool());
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
    int currentFrame = _engineState->getFrameInFlight();
    _commandBufferApplication[currentFrame]->beginCommands();
    // start transfer command buffer
    _initializeTextures();
    _initializeFramebuffer();

    // change real layout to SRC_KHR but we expect it to be in VK_IMAGE_LAYOUT_GENERAL as start value
    for (auto& imageView : _swapchain->getImageViews()) {
      imageView->getImage()->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                          VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, _commandBufferApplication[currentFrame]);
      imageView->getImage()->overrideLayout(VK_IMAGE_LAYOUT_GENERAL);
    }

    _gameState = std::make_shared<GameState>(_commandBufferApplication[currentFrame], _engineState);
  } catch (std::exception e) {
    std::cerr << e.what() << std::endl;
  }
}

void Core::_computeParticles(int index, std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto logger = _engineState->getLogger();

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
}

void Core::_drawShadowMapDirectional(int index,
                                     std::vector<std::shared_ptr<Framebuffer>> framebuffers,
                                     std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto frameBuffer = framebuffers[0];

  auto shadow = _gameState->getLightManager()->getDirectionalShadows()[index];
  // auto commandBuffer = shadow->getShadowMapCommandBuffer(frameInFlight);
  auto logger = _engineState->getLogger();
  // record command buffer
  logger->begin("Directional to depth buffer " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  //
  auto [widthFramebuffer, heightFramebuffer] = shadow->getShadowMapFramebuffer()[frameInFlight]->getResolution();
  VkClearValue clearColor{.color = {1.f, 1.f, 1.f, 1.f}};
  VkRenderPassBeginInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                       .renderPass = frameBuffer->getRenderPass()->getRenderPass(),
                                       .framebuffer = frameBuffer->getBuffer(),
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
}

void Core::_drawShadowMapPoint(int index,
                               int face,
                               std::vector<std::shared_ptr<Framebuffer>> framebuffers,
                               std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto shadow = _gameState->getLightManager()->getPointShadows()[index];
  auto frameBuffer = framebuffers[face];

  // auto commandBuffer = shadow->getShadowMapCommandBuffer(frameInFlight)[face];
  auto logger = _engineState->getLogger();

  // record command buffer
  logger->begin("Point to depth buffer " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  auto [widthFramebuffer, heightFramebuffer] = frameBuffer->getResolution();
  VkClearValue clearDepth{.color = {1.f, 1.f, 1.f, 1.f}};
  VkRenderPassBeginInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                       .renderPass = frameBuffer->getRenderPass()->getRenderPass(),
                                       .framebuffer = frameBuffer->getBuffer(),
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
}

void Core::_drawShadowMapPointSeparableBlur(std::vector<std::shared_ptr<BlurGraphicSeparate>> blur,
                                            int face,
                                            std::vector<std::shared_ptr<Framebuffer>> framebuffers,
                                            std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto logger = _engineState->getLogger();
  auto framebuffer = framebuffers[face];

  if (blur[face]->getHorizontal())
    logger->begin("Blur point horizontal " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  else
    logger->begin("Blur point vertical " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  auto [widthFramebuffer, heightFramebuffer] = framebuffer->getResolution();
  VkClearValue clearDepth{.color = {0.f, 0.f, 0.f, 1.f}};
  VkRenderPassBeginInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                       .renderPass = framebuffer->getRenderPass()->getRenderPass(),
                                       .framebuffer = framebuffer->getBuffer(),
                                       .renderArea = {.offset = {0, 0},
                                                      .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                                                 .height = static_cast<uint32_t>(heightFramebuffer)}},
                                       .clearValueCount = 1,
                                       .pClearValues = &clearDepth};
  vkCmdBeginRenderPass(commandBuffer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  blur[face]->draw(commandBuffer);
  logger->end(commandBuffer);
  vkCmdEndRenderPass(commandBuffer->getCommandBuffer());
}

void Core::_drawShadowMapDirectionalSeparableBlur(std::shared_ptr<BlurGraphicSeparate> blur,
                                                  std::vector<std::shared_ptr<Framebuffer>> framebuffers,
                                                  std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto logger = _engineState->getLogger();
  auto framebuffer = framebuffers[0];

  if (blur->getHorizontal())
    logger->begin("Blur directional horizontal " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  else
    logger->begin("Blur directional vertical " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  auto [widthFramebuffer, heightFramebuffer] = framebuffer->getResolution();
  VkClearValue clearDepth{.color = {0.f, 0.f, 0.f, 1.f}};
  VkRenderPassBeginInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                       .renderPass = framebuffer->getRenderPass()->getRenderPass(),
                                       .framebuffer = framebuffer->getBuffer(),
                                       .renderArea = {.offset = {0, 0},
                                                      .extent = {.width = static_cast<uint32_t>(widthFramebuffer),
                                                                 .height = static_cast<uint32_t>(heightFramebuffer)}},
                                       .clearValueCount = 1,
                                       .pClearValues = &clearDepth};
  vkCmdBeginRenderPass(commandBuffer->getCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  blur->draw(commandBuffer);
  logger->end(commandBuffer);
  vkCmdEndRenderPass(commandBuffer->getCommandBuffer());
}

void Core::_computeBloom(std::shared_ptr<BlurComputeSeparate> blur, std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto logger = _engineState->getLogger();

  if (blur->getHorizontal())
    logger->begin("Blur directional horizontal " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  else
    logger->begin("Blur directional vertical " + std::to_string(_timer->getFrameCounter()), commandBuffer);

  blur->draw(commandBuffer);
  logger->end(commandBuffer);
}

void Core::_computePostprocessing(std::shared_ptr<CommandBuffer> commandBuffer) {
  auto logger = _engineState->getLogger();
  auto swapchainImageIndex = _swapchain->getSwapchainIndex();

  logger->begin("Postprocessing compute " + std::to_string(_timer->getFrameCounter()), commandBuffer);
  _postprocessing->drawCompute(swapchainImageIndex, commandBuffer);
  logger->end(commandBuffer);
}

void Core::_debugVisualizations(std::vector<std::shared_ptr<Framebuffer>> framebuffers,
                                std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto logger = _engineState->getLogger();
  auto frameBuffer = framebuffers[0];

  auto [widthFramebuffer, heightFramebuffer] = frameBuffer->getResolution();
  VkClearValue clearColor{.color = _engineState->getSettings()->getClearColor()};
  VkRenderPassBeginInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                       .renderPass = frameBuffer->getRenderPass()->getRenderPass(),
                                       .framebuffer = frameBuffer->getBuffer(),
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
}

void Core::_renderGraphic(std::vector<std::shared_ptr<Framebuffer>> framebuffers,
                          std::shared_ptr<CommandBuffer> commandBuffer) {
  auto frameInFlight = _engineState->getFrameInFlight();
  auto logger = _engineState->getLogger();
  auto frameBuffer = framebuffers[0];

  /////////////////////////////////////////////////////////////////////////////////////////
  // render graphic
  /////////////////////////////////////////////////////////////////////////////////////////
  auto [widthFramebuffer, heightFramebuffer] = frameBuffer->getResolution();
  std::vector<VkClearValue> clearColor{{.color = _engineState->getSettings()->getClearColor()},
                                       {.color = _engineState->getSettings()->getClearColor()},
                                       {.color = {1.0f, 0}}};
  VkRenderPassBeginInfo renderPassInfo{.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                       .renderPass = frameBuffer->getRenderPass()->getRenderPass(),
                                       .framebuffer = frameBuffer->getBuffer(),
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
}

void Core::_reset() {
  auto frameInFlight = _engineState->getFrameInFlight();
  _swapchain->reset();
  _engineState->getSettings()->setResolution(
      {_swapchain->getSwapchain().extent.width, _swapchain->getSwapchain().extent.height});
  _textureBlurIn.clear();
  _textureBlurOut.clear();

  _commandBufferApplication[frameInFlight]->beginCommands();

  _initializeTextures();
  for (auto& imageView : _swapchain->getImageViews()) imageView->getImage()->overrideLayout(VK_IMAGE_LAYOUT_GENERAL);
  _postprocessing->reset(_swapchain->getImageViews(), _textureBlurIn, _swapchain->getImageViews());
  for (auto& imageView : _swapchain->getImageViews())
    imageView->getImage()->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                        VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, _commandBufferApplication[frameInFlight]);
  if (_gui) _gui->reset();

  _initializeFramebuffer();

  _renderGraph->reset();

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
          applicationPass->addComputeExecution(
              [this](std::shared_ptr<CommandBuffer> commandBuffer) { _callbackUpdate(commandBuffer); });
        }
        {
          // particle system
          for (int i = 0; i < _particleSystems.size(); i++) {
            auto computeParticlesPass = _renderGraph->getPass("Compute particles " + std::to_string(i),
                                                              GraphPassStage::COMPUTE);
            computeParticlesPass->addComputeExecution(
                std::bind(&Core::_computeParticles, this, i, std::placeholders::_1));
            std::vector<std::shared_ptr<Buffer>> inputBuffer(_engineState->getSettings()->getMaxFramesInFlight());
            std::vector<std::shared_ptr<Buffer>> outputBuffer(_engineState->getSettings()->getMaxFramesInFlight());
            for (int frame = 0; frame < _engineState->getSettings()->getMaxFramesInFlight(); frame++) {
              inputBuffer[frame] =
                  _particleSystems[i]
                      ->getParticlesBuffer()[abs(frame - 1) % _engineState->getSettings()->getMaxFramesInFlight()];
              outputBuffer[frame] = _particleSystems[i]->getParticlesBuffer()[frame];
            }
            _renderGraph->getGraphStorage()->add("Particles " + std::to_string(i), inputBuffer);
            _renderGraph->getGraphStorage()->add("Particles " + std::to_string(i), outputBuffer);
            computeParticlesPass->addStorageInput("Particles " + std::to_string(i));
            computeParticlesPass->addStorageOutput("Particles " + std::to_string(i));
          }
        }
        {
          // directional shadow
          auto directionalShadows = _gameState->getLightManager()->getDirectionalShadows();
          for (int i = 0; i < directionalShadows.size(); i++) {
            // if directional light but not shadow
            if (directionalShadows[i] == nullptr) continue;
            auto directionalShadowPass = _renderGraph->getPass("Directional shadow " + std::to_string(i),
                                                               GraphPassStage::GRAPHIC);
            directionalShadowPass->addRenderExecution(
                std::bind(&Core::_drawShadowMapDirectional, this, i, std::placeholders::_1, std::placeholders::_2));

            std::vector<std::shared_ptr<Image>> imagesShadow;
            for (auto& texture : directionalShadows[i]->getShadowMapTexture()) {
              imagesShadow.push_back(texture->getImageView()->getImage());
            }
            _renderGraph->getGraphStorage()->add("Directional shadow " + std::to_string(i),
                                                 std::make_shared<ImageHolderFlight>(imagesShadow, _engineState));
            directionalShadowPass->addColorTarget("Directional shadow " + std::to_string(i));

            // directional shadow blur
            if (_blurSeparateGraphicDirectional.find(directionalShadows[i]) != _blurSeparateGraphicDirectional.end()) {
              auto blur = _blurSeparateGraphicDirectional[directionalShadows[i]];
              for (int s = 0; s < blur.size(); s++) {
                {
                  // horizontal
                  auto directionalShadowBlurHorizontalPass = _renderGraph->getPass(
                      "Directional shadow " + std::to_string(i) + " blur horizontal " + std::to_string(s),
                      GraphPassStage::GRAPHIC);

                  directionalShadowBlurHorizontalPass->addRenderExecution(
                      std::bind(&Core::_drawShadowMapDirectionalSeparableBlur, this, blur[s].first,
                                std::placeholders::_1, std::placeholders::_2));
                  // extract images from textures
                  std::vector<std::shared_ptr<Image>> imagesSrc;
                  for (auto& texture : blur[s].first->getTextureSrc()) {
                    imagesSrc.push_back(texture->getImageView()->getImage());
                  }
                  std::vector<std::shared_ptr<Image>> imagesDst;
                  for (auto& texture : blur[s].first->getTextureDst()) {
                    imagesDst.push_back(texture->getImageView()->getImage());
                  }

                  std::string blurInputName = "Directional shadow " + std::to_string(i) + " blur vertical output " +
                                              std::to_string(s - 1);
                  if (s == 0) {
                    blurInputName = "Directional shadow " + std::to_string(i);
                  }
                  _renderGraph->getGraphStorage()->add(blurInputName,
                                                       std::make_shared<ImageHolderFlight>(imagesSrc, _engineState));
                  _renderGraph->getGraphStorage()->add(
                      "Directional shadow " + std::to_string(i) + " blur horizontal output " + std::to_string(s),
                      std::make_shared<ImageHolderFlight>(imagesDst, _engineState));

                  directionalShadowBlurHorizontalPass->addTextureInput(blurInputName);
                  directionalShadowBlurHorizontalPass->addColorTarget("Directional shadow " + std::to_string(i) +
                                                                      " blur horizontal output " + std::to_string(s));
                }
                {
                  // vertical
                  auto directionalShadowBlurVerticalPass = _renderGraph->getPass(
                      "Directional shadow " + std::to_string(i) + " blur vertical " + std::to_string(s),
                      GraphPassStage::GRAPHIC);
                  directionalShadowBlurVerticalPass->addRenderExecution(
                      std::bind(&Core::_drawShadowMapDirectionalSeparableBlur, this, blur[s].second,
                                std::placeholders::_1, std::placeholders::_2));
                  std::vector<std::shared_ptr<Image>> imagesDst;
                  for (auto& texture : blur[s].second->getTextureDst()) {
                    imagesDst.push_back(texture->getImageView()->getImage());
                  }
                  _renderGraph->getGraphStorage()->add(
                      "Directional shadow " + std::to_string(i) + " blur vertical output " + std::to_string(s),
                      std::make_shared<ImageHolderFlight>(imagesDst, _engineState));

                  directionalShadowBlurVerticalPass->addTextureInput("Directional shadow " + std::to_string(i) +
                                                                     " blur horizontal output " + std::to_string(s));
                  directionalShadowBlurVerticalPass->addColorTarget("Directional shadow " + std::to_string(i) +
                                                                    " blur vertical output " + std::to_string(s));
                }
              }
            }
          }
        }
        {
          // point shadow
          auto pointShadows = _gameState->getLightManager()->getPointShadows();
          for (int i = 0; i < pointShadows.size(); i++) {
            // if point light but not shadow
            if (pointShadows[i] == nullptr) continue;
            auto pointShadowPass = _renderGraph->getPass("Point shadow " + std::to_string(i), GraphPassStage::GRAPHIC);
            for (int face = 0; face < 6; face++) {
              pointShadowPass->addRenderExecution(
                  std::bind(&Core::_drawShadowMapPoint, this, i, face, std::placeholders::_1, std::placeholders::_2));
            }
            std::vector<std::shared_ptr<Image>> imagesShadow;
            for (auto& cubemap : pointShadows[i]->getShadowMapCubemap()) {
              imagesShadow.push_back(cubemap->getTexture()->getImageView()->getImage());
            }
            _renderGraph->getGraphStorage()->add("Point shadow " + std::to_string(i),
                                                 std::make_shared<ImageHolderFlight>(imagesShadow, _engineState));
            pointShadowPass->addColorTarget("Point shadow " + std::to_string(i));

            // point shadow blur
            if (_blurSeparateGraphicPoint.find(pointShadows[i]) != _blurSeparateGraphicPoint.end()) {
              auto blur = _blurSeparateGraphicPoint[pointShadows[i]];
              auto auxilary = _blurSeparateGraphicPointTextures[pointShadows[i]];
              for (int s = 0; s < blur.size(); s++) {
                {
                  // horizontal
                  auto pointShadowBlurHorizontalPass = _renderGraph->getPass(
                      "Point shadow " + std::to_string(i) + " blur horizontal " + std::to_string(s),
                      GraphPassStage::GRAPHIC);

                  for (int face = 0; face < 6; face++) {
                    pointShadowBlurHorizontalPass->addRenderExecution(
                        std::bind(&Core::_drawShadowMapPointSeparableBlur, this, blur[s].first, face,
                                  std::placeholders::_1, std::placeholders::_2));
                  }
                  // extract images from textures
                  std::vector<std::shared_ptr<Image>> imagesSrc;
                  for (auto& texture : auxilary[s].first) {
                    imagesSrc.push_back(texture->getImageView()->getImage());
                  }
                  std::vector<std::shared_ptr<Image>> imagesDst;
                  for (auto& texture : auxilary[s].second) {
                    imagesDst.push_back(texture->getImageView()->getImage());
                  }

                  std::string blurInputName = "Point shadow " + std::to_string(i) + " blur vertical output " +
                                              std::to_string(s - 1);
                  if (s == 0) {
                    blurInputName = "Point shadow " + std::to_string(i);
                  }
                  _renderGraph->getGraphStorage()->add(blurInputName,
                                                       std::make_shared<ImageHolderFlight>(imagesSrc, _engineState));
                  _renderGraph->getGraphStorage()->add(
                      "Point shadow " + std::to_string(i) + " blur horizontal output " + std::to_string(s),
                      std::make_shared<ImageHolderFlight>(imagesDst, _engineState));

                  pointShadowBlurHorizontalPass->addTextureInput(blurInputName);
                  pointShadowBlurHorizontalPass->addColorTarget("Point shadow " + std::to_string(i) +
                                                                " blur horizontal output " + std::to_string(s));
                }
                {
                  // vertical
                  auto pointShadowBlurHorizontalPass = _renderGraph->getPass(
                      "Point shadow " + std::to_string(i) + " blur vertical " + std::to_string(s),
                      GraphPassStage::GRAPHIC);
                  for (int face = 0; face < 6; face++) {
                    pointShadowBlurHorizontalPass->addRenderExecution(
                        std::bind(&Core::_drawShadowMapPointSeparableBlur, this, blur[s].second, face,
                                  std::placeholders::_1, std::placeholders::_2));
                  }
                  // extract images from textures
                  std::vector<std::shared_ptr<Image>> imagesDst;
                  for (auto& texture : auxilary[s].first) {
                    imagesDst.push_back(texture->getImageView()->getImage());
                  }
                  _renderGraph->getGraphStorage()->add(
                      "Point shadow " + std::to_string(i) + " blur vertical output " + std::to_string(s),
                      std::make_shared<ImageHolderFlight>(imagesDst, _engineState));

                  pointShadowBlurHorizontalPass->addTextureInput("Point shadow " + std::to_string(i) +
                                                                 " blur horizontal output " + std::to_string(s));
                  pointShadowBlurHorizontalPass->addColorTarget("Point shadow " + std::to_string(i) +
                                                                " blur vertical output " + std::to_string(s));
                }
              }
            }
          }
        }
        {
          // render
          auto renderPass = _renderGraph->getPass("Render", GraphPassStage::GRAPHIC);
          renderPass->addRenderExecution(
              std::bind(&Core::_renderGraphic, this, std::placeholders::_1, std::placeholders::_2));
          for (int i = 0; i < _particleSystems.size(); i++) {
            renderPass->addVertexBufferInput("Particles " + std::to_string(i));
          }
          auto directionalShadows = _gameState->getLightManager()->getDirectionalShadows();
          for (int i = 0; i < directionalShadows.size(); i++) {
            if (directionalShadows[i] == nullptr) continue;
            if (_blurSeparateGraphicDirectional.find(directionalShadows[i]) != _blurSeparateGraphicDirectional.end()) {
              std::string nameShadow = "Directional shadow " + std::to_string(i) + " blur vertical output " +
                                       std::to_string(_blurSeparateGraphicDirectional[directionalShadows[i]].size() -
                                                      1);
              renderPass->addTextureInput(nameShadow);
            } else {
              renderPass->addTextureInput("Directional shadow " + std::to_string(i));
            }
          }
          auto pointShadows = _gameState->getLightManager()->getPointShadows();
          for (int i = 0; i < pointShadows.size(); i++) {
            if (pointShadows[i] == nullptr) continue;
            if (_blurSeparateGraphicPoint.find(pointShadows[i]) != _blurSeparateGraphicPoint.end()) {
              renderPass->addTextureInput("Point shadow " + std::to_string(i) + " blur vertical output " +
                                          std::to_string(_blurSeparateGraphicPoint[pointShadows[i]].size() - 1));
            } else {
              renderPass->addTextureInput("Point shadow " + std::to_string(i));
            }
          }

          std::vector<std::shared_ptr<Image>> imagePrimitives;
          for (auto& imageViews : _swapchain->getImageViews()) imagePrimitives.push_back(imageViews->getImage());
          std::vector<std::shared_ptr<Image>> imageBlur;
          for (auto& texture : _textureBlurIn) imageBlur.push_back(texture->getImageView()->getImage());
          _renderGraph->getGraphStorage()->add("Swapchain",
                                               std::make_shared<ImageHolderSwapchain>(imagePrimitives, _swapchain));
          _renderGraph->getGraphStorage()->add("Bloom blur input",
                                               std::make_shared<ImageHolderFlight>(imageBlur, _engineState));
          _renderGraph->getGraphStorage()->add("Depth", _depthAttachmentImageView->getImage());
          renderPass->addColorTarget("Swapchain");
          renderPass->addColorTarget("Bloom blur input");
          renderPass->setDepthTarget("Depth");
        }
        {
          // bloom blur
          if (_blurBloom.size() > 0) {
            std::vector<std::shared_ptr<Image>> imagesBlurInput;
            for (auto& texture : _textureBlurIn) imagesBlurInput.push_back(texture->getImageView()->getImage());

            std::vector<std::shared_ptr<Image>> imagesBlurOutput;
            for (auto& texture : _textureBlurOut) imagesBlurOutput.push_back(texture->getImageView()->getImage());

            for (int i = 0; i < _blurBloom.size(); i++) {
              // horizontal
              auto blurPassHorizontal = _renderGraph->getPass("Bloom blur horizontal " + std::to_string(i),
                                                              GraphPassStage::COMPUTE);
              blurPassHorizontal->addComputeExecution(
                  std::bind(&Core::_computeBloom, this, _blurBloom[i].first, std::placeholders::_1));

              std::string inputHorizontal = "Bloom blur input";
              if (i > 0) {
                inputHorizontal = "Bloom blur vertical output " + std::to_string(i - 1);
              }
              blurPassHorizontal->addTextureInput(inputHorizontal);

              _renderGraph->getGraphStorage()->add("Bloom blur horizontal output " + std::to_string(i),
                                                   std::make_shared<ImageHolderFlight>(imagesBlurOutput, _engineState));
              blurPassHorizontal->addColorTarget("Bloom blur horizontal output " + std::to_string(i));

              // vertical
              auto blurPassVertical = _renderGraph->getPass("Bloom blur vertical " + std::to_string(i),
                                                            GraphPassStage::COMPUTE);
              blurPassVertical->addComputeExecution(
                  std::bind(&Core::_computeBloom, this, _blurBloom[i].second, std::placeholders::_1));
              blurPassVertical->addTextureInput("Bloom blur horizontal output " + std::to_string(i));
              _renderGraph->getGraphStorage()->add("Bloom blur vertical output " + std::to_string(i),
                                                   std::make_shared<ImageHolderFlight>(imagesBlurInput, _engineState));
              blurPassVertical->addColorTarget("Bloom blur vertical output " + std::to_string(i));
            }
          }
        }
        {
          // postprocessing
          if (_postprocessing) {
            auto postprocessingPass = _renderGraph->getPass("Postprocessing", GraphPassStage::COMPUTE);
            postprocessingPass->addComputeExecution(
                std::bind(&Core::_computePostprocessing, this, std::placeholders::_1));
            postprocessingPass->addTextureInput("Swapchain");
            if (_blurBloom.size() > 0) {
              postprocessingPass->addTextureInput("Bloom blur vertical output " +
                                                  std::to_string(_blurBloom.size() - 1));
            }
            postprocessingPass->addColorTarget("Swapchain");
          }
        }
        {
          // gui
          if (_gui) {
            auto guiPass = _renderGraph->getPass("GUI", GraphPassStage::GRAPHIC);
            guiPass->setEnd(true);
            guiPass->addRenderExecution(
                std::bind(&Core::_debugVisualizations, this, std::placeholders::_1, std::placeholders::_2));
            std::vector<std::shared_ptr<Image>> images;
            for (auto& imageView : _swapchain->getImageViews()) images.push_back(imageView->getImage());
            _renderGraph->getGraphStorage()->add("Swapchain",
                                                 std::make_shared<ImageHolderSwapchain>(images, _swapchain));
            guiPass->addColorTarget("Swapchain");
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

std::shared_ptr<PhysicsManager> Core::createPhysicsManager() {
  return std::make_shared<PhysicsManager>(_pool, _engineState);
}

std::shared_ptr<GUI> Core::createGUI() {
  _gui = std::make_shared<GUI>(_engineState);
  _gui->initialize(_commandBufferApplication[_engineState->getFrameInFlight()]);
  _engineState->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriberExclusive>(_gui));

  return _gui;
}

void Core::createBloomBlur() {
  _blurBloom.clear();
  for (int pass = 0; pass < _engineState->getSettings()->getBloomPasses(); pass++) {
    auto horizontal = std::make_shared<BlurComputeSeparate>(true, _textureBlurIn, _textureBlurOut, _engineState);
    auto vertical = std::make_shared<BlurComputeSeparate>(false, _textureBlurOut, _textureBlurIn, _engineState);
    _blurBloom.push_back({horizontal, vertical});
  }
}

std::shared_ptr<Postprocessing> Core::createPostprocessing() {
  _postprocessing = std::make_shared<Postprocessing>(_swapchain->getImageViews(), _textureBlurIn,
                                                     _swapchain->getImageViews(), _engineState);
  return _postprocessing;
}

std::shared_ptr<PointShadow> Core::createPointShadow(std::shared_ptr<PointLight> pointLight, bool blur) {
  auto shadow = _gameState->getLightManager()->createPointShadow(
      pointLight, _renderPassShadowMap, _commandBufferApplication[_engineState->getFrameInFlight()]);

  if (blur) {
    auto resolution = shadow->getShadowMapCubemap()[0]->getTexture()->getImageView()->getImage()->getResolution();
    std::vector<std::shared_ptr<Cubemap>> cubemapDst(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      auto filter = VK_FILTER_NEAREST;
      if (_engineState->getDevice()->isFormatFeatureSupported(_engineState->getSettings()->getShadowMapFormat(),
                                                              VK_IMAGE_TILING_OPTIMAL,
                                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        filter = VK_FILTER_LINEAR;
      }
      auto cubemapBlurOutFaces = std::make_shared<Cubemap>(
          resolution, _engineState->getSettings()->getShadowMapFormat(), 1, VK_IMAGE_LAYOUT_GENERAL,
          VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, filter,
          _commandBufferApplication[_engineState->getFrameInFlight()], _engineState);
      cubemapDst[i] = cubemapBlurOutFaces;
    }

    std::vector<std::shared_ptr<BlurGraphicSeparate>> blurHorizontals(6);
    std::vector<std::shared_ptr<BlurGraphicSeparate>> blurVerticals(6);
    for (int j = 0; j < 6; j++) {
      std::vector<std::shared_ptr<Texture>> texturesIn, texturesOut;
      for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
        texturesIn.push_back(shadow->getShadowMapCubemap()[i]->getTextureSeparate()[j][0]);
        texturesOut.push_back(cubemapDst[i]->getTextureSeparate()[j][0]);
      }
      auto blurHorizontal = std::make_shared<BlurGraphicSeparate>(
          true, texturesIn, texturesOut, _commandBufferApplication[_engineState->getFrameInFlight()], _engineState);
      auto blurVertical = std::make_shared<BlurGraphicSeparate>(
          false, blurHorizontal->getTextureDst(), blurHorizontal->getTextureSrc(),
          _commandBufferApplication[_engineState->getFrameInFlight()], _engineState);

      blurHorizontals[j] = blurHorizontal;
      blurVerticals[j] = blurVertical;
    }
    _blurSeparateGraphicPoint[shadow] = {{blurHorizontals, blurVerticals}};

    // fill auxilary buffer for render graph
    std::vector<std::shared_ptr<Texture>> auxilarySrc, auxilaryDst;
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      auxilarySrc.push_back(shadow->getShadowMapCubemap()[i]->getTexture());
      auxilaryDst.push_back(cubemapDst[i]->getTexture());
    }
    _blurSeparateGraphicPointTextures[shadow] = {{auxilarySrc, auxilaryDst}};
  }

  return shadow;
}

std::shared_ptr<DirectionalShadow> Core::createDirectionalShadow(std::shared_ptr<DirectionalLight> directionalLight,
                                                                 bool blur) {
  auto shadow = _gameState->getLightManager()->createDirectionalShadow(
      directionalLight, _renderPassShadowMap, _commandBufferApplication[_engineState->getFrameInFlight()]);

  if (blur) {
    auto resolution = shadow->getShadowMapTexture()[0]->getImageView()->getImage()->getResolution();
    std::vector<std::shared_ptr<Texture>> textureDst(_engineState->getSettings()->getMaxFramesInFlight());
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      auto blurImage = std::make_shared<Image>(resolution, 1, 1, _engineState->getSettings()->getShadowMapFormat(),
                                               VK_IMAGE_TILING_OPTIMAL,
                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _engineState);
      blurImage->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                              _commandBufferApplication[_engineState->getFrameInFlight()]);
      auto blurImageView = std::make_shared<ImageView>(blurImage, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1,
                                                       VK_IMAGE_ASPECT_COLOR_BIT, _engineState);
      auto filter = VK_FILTER_NEAREST;
      if (_engineState->getDevice()->isFormatFeatureSupported(_engineState->getSettings()->getShadowMapFormat(),
                                                              VK_IMAGE_TILING_OPTIMAL,
                                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        filter = VK_FILTER_LINEAR;
      }
      textureDst[i] = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1, filter, blurImageView,
                                                _engineState);
    }

    auto blurHorizontal = std::make_shared<BlurGraphicSeparate>(
        true, shadow->getShadowMapTexture(), textureDst, _commandBufferApplication[_engineState->getFrameInFlight()],
        _engineState);
    auto blurVertical = std::make_shared<BlurGraphicSeparate>(
        false, blurHorizontal->getTextureDst(), shadow->getShadowMapTexture(),
        _commandBufferApplication[_engineState->getFrameInFlight()], _engineState);

    // if dst texture is changed from shadow->getShadowMapTexture() then need to change it in shadow as well + in render
    // graph...
    _blurSeparateGraphicDirectional[shadow] = {{blurHorizontal, blurVertical}};
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

std::shared_ptr<GUI> Core::getGUI() { return _gui; }

std::shared_ptr<EngineState> Core::getEngineState() { return _engineState; }

std::shared_ptr<GameState> Core::getGameState() { return _gameState; }

std::shared_ptr<Camera> Core::getCamera() { return _gameState->getCameraManager()->getCurrentCamera(); }

std::tuple<int, int> Core::getFPS() { return {_timerFPSLimited->getFPS(), _timerFPSReal->getFPS()}; }