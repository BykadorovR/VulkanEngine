#include "Core.h"
#include <typeinfo>

Core::Core(std::shared_ptr<Settings> settings) {
  _state = std::make_shared<State>(settings);
  _swapchain = std::make_shared<Swapchain>(settings->getSwapchainColorFormat(), settings->getDepthFormat(), _state);
  _timer = std::make_shared<Timer>();
  _timerFPSReal = std::make_shared<TimerFPS>();
  _timerFPSLimited = std::make_shared<TimerFPS>();

  _commandPoolRender = std::make_shared<CommandPool>(QueueType::GRAPHIC, _state->getDevice());
  _commandBufferRender = std::make_shared<CommandBuffer>(settings->getMaxFramesInFlight(), _commandPoolRender, _state);
  _commandPoolTransfer = std::make_shared<CommandPool>(QueueType::GRAPHIC, _state->getDevice());
  // frameInFlight != 0 can be used in reset
  _commandBufferTransfer = std::make_shared<CommandBuffer>(settings->getMaxFramesInFlight(), _commandPoolTransfer,
                                                           _state);
  _commandPoolEquirectangular = std::make_shared<CommandPool>(QueueType::GRAPHIC, _state->getDevice());
  _commandBufferEquirectangular = std::make_shared<CommandBuffer>(settings->getMaxFramesInFlight(),
                                                                  _commandPoolEquirectangular, _state);
  _commandPoolParticleSystem = std::make_shared<CommandPool>(QueueType::COMPUTE, _state->getDevice());
  _commandBufferParticleSystem = std::make_shared<CommandBuffer>(settings->getMaxFramesInFlight(),
                                                                 _commandPoolParticleSystem, _state);
  _commandPoolPostprocessing = std::make_shared<CommandPool>(QueueType::COMPUTE, _state->getDevice());
  _commandBufferPostprocessing = std::make_shared<CommandBuffer>(settings->getMaxFramesInFlight(),
                                                                 _commandPoolPostprocessing, _state);
  _commandPoolGUI = std::make_shared<CommandPool>(QueueType::GRAPHIC, _state->getDevice());
  _commandBufferGUI = std::make_shared<CommandBuffer>(settings->getMaxFramesInFlight(), _commandPoolGUI, _state);

  _frameSubmitInfoCompute.resize(settings->getMaxFramesInFlight());
  _frameSubmitInfoGraphic.resize(settings->getMaxFramesInFlight());

  // start transfer command buffer
  _commandBufferTransfer->beginCommands();

  _loggerGPU = std::make_shared<LoggerGPU>(_state);
  _loggerPostprocessing = std::make_shared<LoggerGPU>(_state);
  _loggerParticles = std::make_shared<LoggerGPU>(_state);
  _loggerGUI = std::make_shared<LoggerGPU>(_state);
  _loggerGPUDebug = std::make_shared<LoggerGPU>(_state);
  _loggerCPU = std::make_shared<LoggerCPU>();

  _resourceManager = std::make_shared<ResourceManager>(_commandBufferTransfer, _state);

  for (int i = 0; i < settings->getMaxFramesInFlight(); i++) {
    // graphic-presentation
    _semaphoreImageAvailable.push_back(std::make_shared<Semaphore>(VK_SEMAPHORE_TYPE_BINARY, _state->getDevice()));
    _semaphoreRenderFinished.push_back(std::make_shared<Semaphore>(VK_SEMAPHORE_TYPE_BINARY, _state->getDevice()));

    // compute-graphic
    _semaphoreParticleSystem.push_back(std::make_shared<Semaphore>(VK_SEMAPHORE_TYPE_BINARY, _state->getDevice()));
    _semaphoreGUI.push_back(std::make_shared<Semaphore>(VK_SEMAPHORE_TYPE_BINARY, _state->getDevice()));

    // postprocessing semaphore
    _graphicTimelineSemaphore.timelineInfo.push_back(VkTimelineSemaphoreSubmitInfo{});
    _graphicTimelineSemaphore.particleSignal.push_back(uint64_t{0});
    _graphicTimelineSemaphore.semaphore.push_back(
        std::make_shared<Semaphore>(VK_SEMAPHORE_TYPE_TIMELINE, _state->getDevice()));
  }

  for (int i = 0; i < settings->getMaxFramesInFlight(); i++) {
    _fenceInFlight.push_back(std::make_shared<Fence>(_state->getDevice()));
    _fenceParticleSystem.push_back(std::make_shared<Fence>(_state->getDevice()));
  }

  _initializeTextures();

  _gui = std::make_shared<GUI>(_state);
  _gui->initialize(_commandBufferTransfer);

  _blur = std::make_shared<Blur>(_textureBlurIn, _textureBlurOut, _state);
  // for postprocessing descriptors GENERAL is needed
  _swapchain->overrideImageLayout(VK_IMAGE_LAYOUT_GENERAL);
  _postprocessing = std::make_shared<Postprocessing>(_textureRender, _textureBlurIn, _swapchain->getImageViews(),
                                                     _state);
  // but we expect it to be in VK_IMAGE_LAYOUT_PRESENT_SRC_KHR as start value
  _swapchain->changeImageLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, _commandBufferTransfer);
  _state->getInput()->subscribe(std::dynamic_pointer_cast<InputSubscriber>(_gui));

  _pool = std::make_shared<BS::thread_pool>(settings->getThreadsInPool());

  _lightManager = std::make_shared<LightManager>(_commandBufferTransfer, _resourceManager, _state);

  _commandBufferTransfer->endCommands();

  {
    // TODO: remove vkQueueWaitIdle, add fence or semaphore
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_commandBufferTransfer->getCommandBuffer()[_state->getFrameInFlight()];
    auto queue = _state->getDevice()->getQueue(QueueType::GRAPHIC);
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
  }
}

void Core::setCamera(std::shared_ptr<Camera> camera) { _camera = camera; }

void Core::_initializeTextures() {
  auto settings = _state->getSettings();
  _textureRender.resize(settings->getMaxFramesInFlight());
  _textureBlurIn.resize(settings->getMaxFramesInFlight());
  _textureBlurOut.resize(settings->getMaxFramesInFlight());
  for (int i = 0; i < settings->getMaxFramesInFlight(); i++) {
    auto graphicImage = std::make_shared<Image>(
        settings->getResolution(), 1, 1, settings->getGraphicColorFormat(), VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _state);
    graphicImage->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                               _commandBufferTransfer);
    auto graphicImageView = std::make_shared<ImageView>(graphicImage, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1,
                                                        VK_IMAGE_ASPECT_COLOR_BIT, _state);
    _textureRender[i] = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1, graphicImageView, _state);
    {
      auto blurImage = std::make_shared<Image>(settings->getResolution(), 1, 1, settings->getGraphicColorFormat(),
                                               VK_IMAGE_TILING_OPTIMAL,
                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _state);
      blurImage->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                              _commandBufferTransfer);
      auto blurImageView = std::make_shared<ImageView>(blurImage, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1,
                                                       VK_IMAGE_ASPECT_COLOR_BIT, _state);
      _textureBlurIn[i] = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1, blurImageView, _state);
    }
    {
      auto blurImage = std::make_shared<Image>(settings->getResolution(), 1, 1, settings->getGraphicColorFormat(),
                                               VK_IMAGE_TILING_OPTIMAL,
                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _state);
      blurImage->changeLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                              _commandBufferTransfer);
      auto blurImageView = std::make_shared<ImageView>(blurImage, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1,
                                                       VK_IMAGE_ASPECT_COLOR_BIT, _state);
      _textureBlurOut[i] = std::make_shared<Texture>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1, blurImageView, _state);
    }
  }
}

void Core::_directionalLightCalculator(int index) {
  auto frameInFlight = _state->getFrameInFlight();

  auto commandBuffer = _lightManager->getDirectionalLightCommandBuffers()[index];
  auto loggerGPU = _lightManager->getDirectionalLightLoggers()[index];

  // record command buffer
  commandBuffer->beginCommands();
  loggerGPU->setCommandBufferName("Directional command buffer", commandBuffer);

  loggerGPU->begin("Directional to depth buffer " + std::to_string(_timer->getFrameCounter()));
  // change layout to write one
  VkImageMemoryBarrier imageMemoryBarrier{};
  imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  // We won't be changing the layout of the image
  imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  imageMemoryBarrier.image = _lightManager->getDirectionalLights()[index]
                                 ->getDepthTexture()[frameInFlight]
                                 ->getImageView()
                                 ->getImage()
                                 ->getImage();
  imageMemoryBarrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
  imageMemoryBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

  vkCmdPipelineBarrier(commandBuffer->getCommandBuffer()[frameInFlight],
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &imageMemoryBarrier);
  //
  auto directionalLights = _lightManager->getDirectionalLights();
  VkClearValue clearDepth;
  clearDepth.depthStencil = {1.0f, 0};
  const VkRenderingAttachmentInfo depthAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = directionalLights[index]->getDepthTexture()[frameInFlight]->getImageView()->getImageView(),
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = clearDepth,
  };

  auto [width, height] =
      directionalLights[index]->getDepthTexture()[frameInFlight]->getImageView()->getImage()->getResolution();
  VkRect2D renderArea{};
  renderArea.extent.width = width;
  renderArea.extent.height = height;
  const VkRenderingInfoKHR renderInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
      .renderArea = renderArea,
      .layerCount = 1,
      .pDepthAttachment = &depthAttachmentInfo,
  };

  // TODO: only one depth texture?
  vkCmdBeginRendering(commandBuffer->getCommandBuffer()[frameInFlight], &renderInfo);
  // Set depth bias (aka "Polygon offset")
  // Required to avoid shadow mapping artifacts
  vkCmdSetDepthBias(commandBuffer->getCommandBuffer()[frameInFlight], _state->getSettings()->getDepthBiasConstant(),
                    0.0f, _state->getSettings()->getDepthBiasSlope());

  // draw scene here
  auto globalFrame = _timer->getFrameCounter();
  for (auto shadowable : _shadowables) {
    std::string drawableName = typeid(shadowable).name();
    loggerGPU->begin(drawableName + " to directional depth buffer " + std::to_string(globalFrame));
    shadowable->drawShadow(LightType::DIRECTIONAL, index, 0, commandBuffer);
    loggerGPU->end();
  }
  vkCmdEndRendering(commandBuffer->getCommandBuffer()[frameInFlight]);
  loggerGPU->end();

  // record command buffer
  commandBuffer->endCommands();
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer->getCommandBuffer()[frameInFlight];
  std::unique_lock<std::mutex> lock(_frameSubmitMutexGraphic);
  _frameSubmitInfoGraphic[frameInFlight].push_back(submitInfo);
}

void Core::_pointLightCalculator(int index, int face) {
  auto frameInFlight = _state->getFrameInFlight();

  auto commandBuffer = _lightManager->getPointLightCommandBuffers()[index][face];
  auto loggerGPU = _lightManager->getPointLightLoggers()[index][face];
  // record command buffer
  loggerGPU->setCommandBufferName("Point " + std::to_string(index) + "x" + std::to_string(face) + " command buffer",
                                  commandBuffer);
  commandBuffer->beginCommands();
  loggerGPU->begin("Point to depth buffer " + std::to_string(_timer->getFrameCounter()));
  // cubemap is the only image, rest is image views, so we need to perform change only once
  if (face == 0) {
    // change layout to write one
    VkImageMemoryBarrier imageMemoryBarrier{};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    // We won't be changing the layout of the image
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    imageMemoryBarrier.image = _lightManager->getPointLights()[index]
                                   ->getDepthCubemap()[frameInFlight]
                                   ->getTexture()
                                   ->getImageView()
                                   ->getImage()
                                   ->getImage();
    imageMemoryBarrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    vkCmdPipelineBarrier(commandBuffer->getCommandBuffer()[frameInFlight],
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &imageMemoryBarrier);
  }
  auto pointLights = _lightManager->getPointLights();
  VkClearValue clearDepth;
  clearDepth.depthStencil = {1.0f, 0};
  const VkRenderingAttachmentInfo depthAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = pointLights[index]
                       ->getDepthCubemap()[frameInFlight]
                       ->getTextureSeparate()[face][0]
                       ->getImageView()
                       ->getImageView(),
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = clearDepth,
  };

  auto [width, height] = pointLights[index]
                             ->getDepthCubemap()[frameInFlight]
                             ->getTextureSeparate()[face][0]
                             ->getImageView()
                             ->getImage()
                             ->getResolution();
  VkRect2D renderArea{};
  renderArea.extent.width = width;
  renderArea.extent.height = height;
  const VkRenderingInfo renderInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = renderArea,
      .layerCount = 1,
      .pDepthAttachment = &depthAttachmentInfo,
  };

  // TODO: only one depth texture?
  vkCmdBeginRendering(commandBuffer->getCommandBuffer()[frameInFlight], &renderInfo);
  // Set depth bias (aka "Polygon offset")
  // Required to avoid shadow mapping artifacts
  vkCmdSetDepthBias(commandBuffer->getCommandBuffer()[frameInFlight], _state->getSettings()->getDepthBiasConstant(),
                    0.0f, _state->getSettings()->getDepthBiasSlope());

  // draw scene here
  auto globalFrame = _timer->getFrameCounter();
  float aspect = std::get<0>(_state->getSettings()->getResolution()) /
                 std::get<1>(_state->getSettings()->getResolution());

  // draw scene here
  for (auto shadowable : _shadowables) {
    std::string drawableName = typeid(shadowable).name();
    loggerGPU->begin(drawableName + " to point depth buffer " + std::to_string(globalFrame));
    shadowable->drawShadow(LightType::POINT, index, face, commandBuffer);
    loggerGPU->end();
  }
  vkCmdEndRendering(commandBuffer->getCommandBuffer()[frameInFlight]);
  loggerGPU->end();

  // record command buffer
  commandBuffer->endCommands();
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer->getCommandBuffer()[frameInFlight];
  std::unique_lock<std::mutex> lock(_frameSubmitMutexGraphic);
  _frameSubmitInfoGraphic[frameInFlight].push_back(submitInfo);
}

void Core::_computeParticles() {
  auto frameInFlight = _state->getFrameInFlight();

  _commandBufferParticleSystem->beginCommands();
  _loggerParticles->setCommandBufferName("Particles compute command buffer", _commandBufferParticleSystem);

  // any read from SSBO should wait for write to SSBO
  // First dispatch writes to a storage buffer, second dispatch reads from that storage buffer.
  VkMemoryBarrier memoryBarrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
  vkCmdPipelineBarrier(_commandBufferParticleSystem->getCommandBuffer()[frameInFlight],
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // srcStageMask
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
                       0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

  _loggerParticles->begin("Particle system compute " + std::to_string(_timer->getFrameCounter()));
  for (auto& particleSystem : _particleSystem) {
    particleSystem->drawCompute(_commandBufferParticleSystem);
    particleSystem->updateTimer(_timer->getElapsedCurrent());
  }
  _loggerParticles->end();

  VkSubmitInfo submitInfoCompute{};
  submitInfoCompute.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfoCompute.signalSemaphoreCount = 1;
  submitInfoCompute.pSignalSemaphores = &_semaphoreParticleSystem[frameInFlight]->getSemaphore();
  submitInfoCompute.commandBufferCount = 1;
  submitInfoCompute.pCommandBuffers = &_commandBufferParticleSystem->getCommandBuffer()[frameInFlight];

  // end command buffer
  _commandBufferParticleSystem->endCommands();
  std::unique_lock<std::mutex> lock(_frameSubmitMutexCompute);
  _frameSubmitInfoCompute[frameInFlight].push_back(submitInfoCompute);
}

void Core::_computePostprocessing(int swapchainImageIndex) {
  auto frameInFlight = _state->getFrameInFlight();

  _commandBufferPostprocessing->beginCommands();
  _loggerPostprocessing->setCommandBufferName("Postprocessing command buffer", _commandBufferPostprocessing);
  int bloomPasses = _state->getSettings()->getBloomPasses();
  // blur cycle:
  // in - out - horizontal
  // out - in - vertical
  for (int i = 0; i < bloomPasses; i++) {
    _loggerPostprocessing->begin("Blur horizontal compute " + std::to_string(_timer->getFrameCounter()));
    _blur->drawCompute(frameInFlight, true, _commandBufferPostprocessing);
    _loggerPostprocessing->end();

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
      vkCmdPipelineBarrier(_commandBufferPostprocessing->getCommandBuffer()[frameInFlight],
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // srcStageMask
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
                           0, 0, nullptr, 0, nullptr,
                           1,             // imageMemoryBarrierCount
                           &colorBarrier  // pImageMemoryBarriers
      );
    }

    _loggerPostprocessing->begin("Blur vertical compute " + std::to_string(_timer->getFrameCounter()));
    _blur->drawCompute(frameInFlight, false, _commandBufferPostprocessing);
    _loggerPostprocessing->end();

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
      vkCmdPipelineBarrier(_commandBufferPostprocessing->getCommandBuffer()[frameInFlight],
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // srcStageMask
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
                           0, 0, nullptr, 0, nullptr,
                           1,             // imageMemoryBarrierCount
                           &colorBarrier  // pImageMemoryBarriers
      );
    }
  }
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
    vkCmdPipelineBarrier(_commandBufferPostprocessing->getCommandBuffer()[frameInFlight],
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,     // srcStageMask
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
                         0, 0, nullptr, 0, nullptr,
                         1,             // imageMemoryBarrierCount
                         &colorBarrier  // pImageMemoryBarriers
    );
  }

  _loggerPostprocessing->begin("Postprocessing compute " + std::to_string(_timer->getFrameCounter()));
  _postprocessing->drawCompute(frameInFlight, swapchainImageIndex, _commandBufferPostprocessing);
  _loggerPostprocessing->end();
  _commandBufferPostprocessing->endCommands();
}

void Core::_debugVisualizations(int swapchainImageIndex) {
  auto frameInFlight = _state->getFrameInFlight();

  _commandBufferGUI->beginCommands();
  _loggerGPUDebug->setCommandBufferName("GUI command buffer", _commandBufferGUI);

  VkClearValue clearColor;
  clearColor.color = _state->getSettings()->getClearColor();
  const VkRenderingAttachmentInfo colorAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = _swapchain->getImageViews()[swapchainImageIndex]->getImageView(),
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = clearColor,
  };

  const VkRenderingAttachmentInfo depthAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = _swapchain->getDepthImageView()->getImageView(),
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
  };

  auto [width, height] = _swapchain->getImageViews()[swapchainImageIndex]->getImage()->getResolution();
  VkRect2D renderArea{};
  renderArea.extent.width = width;
  renderArea.extent.height = height;
  const VkRenderingInfo renderInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = renderArea,
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorAttachmentInfo,
      .pDepthAttachment = &depthAttachmentInfo,
  };

  vkCmdBeginRendering(_commandBufferGUI->getCommandBuffer()[frameInFlight], &renderInfo);
  _loggerGPUDebug->begin("Render GUI " + std::to_string(_timer->getFrameCounter()));
  _gui->updateBuffers(frameInFlight);
  _gui->drawFrame(frameInFlight, _commandBufferGUI);
  _loggerGPUDebug->end();
  vkCmdEndRendering(_commandBufferGUI->getCommandBuffer()[frameInFlight]);

  VkImageMemoryBarrier colorBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                    .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                                    .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                    .image = _swapchain->getImageViews()[swapchainImageIndex]->getImage()->getImage(),
                                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                         .baseMipLevel = 0,
                                                         .levelCount = 1,
                                                         .baseArrayLayer = 0,
                                                         .layerCount = 1}};

  vkCmdPipelineBarrier(_commandBufferGUI->getCommandBuffer()[frameInFlight],
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // srcStageMask
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,  // dstStageMask
                       0, 0, nullptr, 0, nullptr,
                       1,             // imageMemoryBarrierCount
                       &colorBarrier  // pImageMemoryBarriers
  );

  _commandBufferGUI->endCommands();
}

void Core::_renderGraphic() {
  auto frameInFlight = _state->getFrameInFlight();

  // record command buffer
  _commandBufferRender->beginCommands();
  _loggerGPU->setCommandBufferName("Draw command buffer", _commandBufferRender);

  /////////////////////////////////////////////////////////////////////////////////////////
  // depth to screne barrier
  /////////////////////////////////////////////////////////////////////////////////////////
  // Image memory barrier to make sure that writes are finished before sampling from the texture
  int directionalNum = _lightManager->getDirectionalLights().size();
  int pointNum = _lightManager->getPointLights().size();
  std::vector<VkImageMemoryBarrier> imageMemoryBarrier(directionalNum + pointNum);
  for (int i = 0; i < directionalNum; i++) {
    imageMemoryBarrier[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    // We won't be changing the layout of the image
    imageMemoryBarrier[i].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    imageMemoryBarrier[i].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    imageMemoryBarrier[i].image = _lightManager->getDirectionalLights()[i]
                                      ->getDepthTexture()[frameInFlight]
                                      ->getImageView()
                                      ->getImage()
                                      ->getImage();
    imageMemoryBarrier[i].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    imageMemoryBarrier[i].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    imageMemoryBarrier[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageMemoryBarrier[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  }

  for (int i = 0; i < pointNum; i++) {
    int id = directionalNum + i;
    imageMemoryBarrier[id].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    // We won't be changing the layout of the image
    imageMemoryBarrier[id].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    imageMemoryBarrier[id].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    imageMemoryBarrier[id].image = _lightManager->getPointLights()[i]
                                       ->getDepthCubemap()[frameInFlight]
                                       ->getTexture()
                                       ->getImageView()
                                       ->getImage()
                                       ->getImage();
    imageMemoryBarrier[id].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    imageMemoryBarrier[id].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    imageMemoryBarrier[id].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageMemoryBarrier[id].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier[id].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  }
  vkCmdPipelineBarrier(_commandBufferRender->getCommandBuffer()[frameInFlight],
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, imageMemoryBarrier.size(),
                       imageMemoryBarrier.data());

  /////////////////////////////////////////////////////////////////////////////////////////
  // render graphic
  /////////////////////////////////////////////////////////////////////////////////////////
  VkClearValue clearColor;
  clearColor.color = _state->getSettings()->getClearColor();
  std::vector<VkRenderingAttachmentInfo> colorAttachmentInfo(2);
  // here we render scene as is
  colorAttachmentInfo[0] = VkRenderingAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = _textureRender[frameInFlight]->getImageView()->getImageView(),
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = clearColor,
  };
  // here we render bloom that will be applied on postprocessing stage to simple render
  colorAttachmentInfo[1] = VkRenderingAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = _textureBlurIn[frameInFlight]->getImageView()->getImageView(),
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = clearColor,
  };

  VkClearValue clearDepth;
  clearDepth.depthStencil = {1.0f, 0};
  const VkRenderingAttachmentInfo depthAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = _swapchain->getDepthImageView()->getImageView(),
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = clearDepth,
  };

  auto [width, height] = _textureRender[frameInFlight]->getImageView()->getImage()->getResolution();
  VkRect2D renderArea{};
  renderArea.extent.width = width;
  renderArea.extent.height = height;
  const VkRenderingInfo renderInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = renderArea,
      .layerCount = 1,
      .colorAttachmentCount = (uint32_t)colorAttachmentInfo.size(),
      .pColorAttachments = colorAttachmentInfo.data(),
      .pDepthAttachment = &depthAttachmentInfo,
  };

  auto globalFrame = _timer->getFrameCounter();
  vkCmdBeginRendering(_commandBufferRender->getCommandBuffer()[frameInFlight], &renderInfo);
  _loggerGPU->begin("Render light " + std::to_string(globalFrame));
  _lightManager->draw(frameInFlight);
  _loggerGPU->end();

  // draw scene here
  // TODO: should be just before ModelManagers
  for (auto& animation : _animations) {
    if (_futureAnimationUpdate[animation].valid()) {
      _futureAnimationUpdate[animation].get();
    }
  }

  // should be draw first
  if (_skybox) {
    _loggerGPU->begin("Render skybox " + std::to_string(globalFrame));
    _skybox->draw(_state->getSettings()->getResolution(), _camera, _commandBufferRender);
    _loggerGPU->end();
  }

  for (auto& drawable : _drawables[AlphaType::OPAQUE]) {
    // TODO: add getName() to drawable?
    std::string drawableName = typeid(drawable.get()).name();
    _loggerGPU->begin("Render " + drawableName + " " + std::to_string(globalFrame));
    drawable->draw(_state->getSettings()->getResolution(), _camera, _commandBufferRender);
    _loggerGPU->end();
  }

  std::sort(_drawables[AlphaType::TRANSPARENT].begin(), _drawables[AlphaType::TRANSPARENT].end(),
            [camera = _camera](std::shared_ptr<Drawable> left, std::shared_ptr<Drawable> right) {
              return glm::distance(glm::vec3(left->getModel()[3]), camera->getEye()) >
                     glm::distance(glm::vec3(right->getModel()[3]), camera->getEye());
            });
  for (auto& drawable : _drawables[AlphaType::TRANSPARENT]) {
    // TODO: add getName() to drawable?
    std::string drawableName = typeid(drawable.get()).name();
    _loggerGPU->begin("Render " + drawableName + " " + std::to_string(globalFrame));
    drawable->draw(_state->getSettings()->getResolution(), _camera, _commandBufferRender);
    _loggerGPU->end();
  }

  // submit model3D update
  for (auto& animation : _animations) {
    _futureAnimationUpdate[animation] = _pool->submit([&]() {
      _loggerCPU->begin("Update animation " + std::to_string(globalFrame));
      // we want update model for next frame, current frame we can't touch and update because it will be used on GPU
      animation->updateAnimation(_timer->getElapsedCurrent());
      _loggerCPU->end();
    });
  }

  vkCmdEndRendering(_commandBufferRender->getCommandBuffer()[frameInFlight]);
  _commandBufferRender->endCommands();
}

VkResult Core::_getImageIndex(uint32_t* imageIndex) {
  auto frameInFlight = _state->getFrameInFlight();
  std::vector<VkFence> waitFences = {_fenceInFlight[frameInFlight]->getFence()};
  auto result = vkWaitForFences(_state->getDevice()->getLogicalDevice(), waitFences.size(), waitFences.data(), VK_TRUE,
                                UINT64_MAX);
  if (result != VK_SUCCESS) throw std::runtime_error("Can't wait for fence");

  _frameSubmitInfoCompute[frameInFlight].clear();
  _frameSubmitInfoGraphic[frameInFlight].clear();
  // RETURNS ONLY INDEX, NOT IMAGE
  // semaphore to signal, once image is available
  result = vkAcquireNextImageKHR(_state->getDevice()->getLogicalDevice(), _swapchain->getSwapchain(), UINT64_MAX,
                                 _semaphoreImageAvailable[frameInFlight]->getSemaphore(), VK_NULL_HANDLE, imageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    _reset();
    return result;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  // Only reset the fence if we are submitting work
  result = vkResetFences(_state->getDevice()->getLogicalDevice(), waitFences.size(), waitFences.data());
  if (result != VK_SUCCESS) throw std::runtime_error("Can't reset fence");

  return result;
}

void Core::_reset() {
  auto surfaceCapabilities = _state->getDevice()->getSupportedSurfaceCapabilities();
  VkExtent2D extent = surfaceCapabilities.currentExtent;
  while (extent.width == 0 || extent.height == 0) {
    surfaceCapabilities = _state->getDevice()->getSupportedSurfaceCapabilities();
    extent = surfaceCapabilities.currentExtent;
    glfwWaitEvents();
  }
  _swapchain->reset();
  _state->getSettings()->setResolution({extent.width, extent.height});
  _textureRender.clear();
  _textureBlurIn.clear();
  _textureBlurOut.clear();

  _commandBufferTransfer->beginCommands();

  _initializeTextures();
  _swapchain->overrideImageLayout(VK_IMAGE_LAYOUT_GENERAL);
  _postprocessing->reset(_textureRender, _textureBlurIn, _swapchain->getImageViews());
  _swapchain->changeImageLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, _commandBufferTransfer);
  _gui->reset();
  _blur->reset(_textureBlurIn, _textureBlurOut);

  _commandBufferTransfer->endCommands();

  {
    // TODO: remove vkQueueWaitIdle, add fence or semaphore
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_commandBufferTransfer->getCommandBuffer()[_state->getFrameInFlight()];
    auto queue = _state->getDevice()->getQueue(QueueType::GRAPHIC);
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
  }

  _callbackReset(extent.width, extent.height);
}

void Core::_drawFrame(int imageIndex) {
  auto frameInFlight = _state->getFrameInFlight();
  // submit compute particles
  auto particlesFuture = _pool->submit(std::bind(&Core::_computeParticles, this));

  /////////////////////////////////////////////////////////////////////////////////////////
  // render to depth buffer
  /////////////////////////////////////////////////////////////////////////////////////////
  std::vector<std::future<void>> shadowFutures;
  for (int i = 0; i < _lightManager->getDirectionalLights().size(); i++) {
    shadowFutures.push_back(_pool->submit(std::bind(&Core::_directionalLightCalculator, this, i)));
  }

  for (int i = 0; i < _lightManager->getPointLights().size(); i++) {
    for (int j = 0; j < 6; j++) {
      shadowFutures.push_back(_pool->submit(std::bind(&Core::_pointLightCalculator, this, i, j)));
    }
  }

  auto postprocessingFuture = _pool->submit(std::bind(&Core::_computePostprocessing, this, imageIndex));
  auto debugVisualizationFuture = _pool->submit(std::bind(&Core::_debugVisualizations, this, imageIndex));

  /////////////////////////////////////////////////////////////////////////////////////////////////
  // Render graphics
  /////////////////////////////////////////////////////////////////////////////////////////////////
  _renderGraphic();

  // wait for shadow to complete before render
  for (auto& shadowFuture : shadowFutures) {
    if (shadowFuture.valid()) {
      shadowFuture.get();
    }
  }

  // wait for particles to complete before render
  if (particlesFuture.valid()) particlesFuture.get();
  {
    // timeline signal structure
    _graphicTimelineSemaphore.particleSignal[frameInFlight] = _timer->getFrameCounter() + 1;
    _graphicTimelineSemaphore.timelineInfo[frameInFlight].sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    _graphicTimelineSemaphore.timelineInfo[frameInFlight].pNext = NULL;
    _graphicTimelineSemaphore.timelineInfo[frameInFlight].signalSemaphoreValueCount = 1;
    _graphicTimelineSemaphore.timelineInfo[frameInFlight].pSignalSemaphoreValues =
        &_graphicTimelineSemaphore.particleSignal[frameInFlight];
    // binary wait structure
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &_graphicTimelineSemaphore.timelineInfo[frameInFlight];
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &_semaphoreParticleSystem[frameInFlight]->getSemaphore();
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_commandBufferRender->getCommandBuffer()[frameInFlight];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &_graphicTimelineSemaphore.semaphore[frameInFlight]->getSemaphore();

    std::unique_lock<std::mutex> lock(_frameSubmitMutexGraphic);
    _frameSubmitInfoGraphic[frameInFlight].push_back(submitInfo);
  }
  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Render compute postprocessing
  //////////////////////////////////////////////////////////////////////////////////////////////////
  std::vector<VkSemaphore> waitSemaphoresPostprocessing = {
      _semaphoreImageAvailable[frameInFlight]->getSemaphore(),
      _graphicTimelineSemaphore.semaphore[frameInFlight]->getSemaphore()};
  // binary semaphore ignores wait value
  std::vector<uint64_t> waitValuesPostprocessing = {_graphicTimelineSemaphore.particleSignal[frameInFlight],
                                                    _graphicTimelineSemaphore.particleSignal[frameInFlight]};
  {
    if (postprocessingFuture.valid()) postprocessingFuture.get();
    VkTimelineSemaphoreSubmitInfo waitParticlesSemaphore{};
    waitParticlesSemaphore.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    waitParticlesSemaphore.pNext = NULL;
    waitParticlesSemaphore.waitSemaphoreValueCount = waitSemaphoresPostprocessing.size();
    waitParticlesSemaphore.pWaitSemaphoreValues = waitValuesPostprocessing.data();
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &waitParticlesSemaphore;
    submitInfo.waitSemaphoreCount = waitSemaphoresPostprocessing.size();
    submitInfo.pWaitSemaphores = waitSemaphoresPostprocessing.data();
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &_semaphoreGUI[frameInFlight]->getSemaphore();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_commandBufferPostprocessing->getCommandBuffer()[frameInFlight];
    std::unique_lock<std::mutex> lock(_frameSubmitMutexCompute);
    _frameSubmitInfoCompute[frameInFlight].push_back(submitInfo);
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Render debug visualization
  //////////////////////////////////////////////////////////////////////////////////////////////////
  {
    if (debugVisualizationFuture.valid()) debugVisualizationFuture.get();

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &_semaphoreGUI[frameInFlight]->getSemaphore();
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_commandBufferGUI->getCommandBuffer()[frameInFlight];

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &_semaphoreRenderFinished[frameInFlight]->getSemaphore();
    std::unique_lock<std::mutex> lock(_frameSubmitMutexGraphic);
    _frameSubmitInfoGraphic[frameInFlight].push_back(submitInfo);
  }

  vkQueueSubmit(_state->getDevice()->getQueue(QueueType::COMPUTE), _frameSubmitInfoCompute[frameInFlight].size(),
                _frameSubmitInfoCompute[frameInFlight].data(), nullptr);
  vkQueueSubmit(_state->getDevice()->getQueue(QueueType::GRAPHIC), _frameSubmitInfoGraphic[frameInFlight].size(),
                _frameSubmitInfoGraphic[frameInFlight].data(), _fenceInFlight[frameInFlight]->getFence());
}

void Core::_displayFrame(uint32_t* imageIndex) {
  auto frameInFlight = _state->getFrameInFlight();

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

  std::vector<VkSemaphore> waitSemaphoresPresent = {_semaphoreRenderFinished[frameInFlight]->getSemaphore()};

  presentInfo.waitSemaphoreCount = waitSemaphoresPresent.size();
  presentInfo.pWaitSemaphores = waitSemaphoresPresent.data();

  VkSwapchainKHR swapChains[] = {_swapchain->getSwapchain()};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;

  presentInfo.pImageIndices = imageIndex;

  // TODO: change to own present queue
  auto result = vkQueuePresentKHR(_state->getDevice()->getQueue(QueueType::PRESENT), &presentInfo);

  // getResized() can be valid only here, we can get inconsistencies in semaphores if VK_ERROR_OUT_OF_DATE_KHR is not
  // reported by Vulkan
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || _state->getWindow()->getResized()) {
    _state->getWindow()->setResized(false);
    _reset();
  } else if (result != VK_SUCCESS) {
    throw std::runtime_error("failed to present swap chain image!");
  }
}

void Core::draw() {
  while (!glfwWindowShouldClose(_state->getWindow()->getWindow())) {
    glfwPollEvents();
    _timer->tick();
    _timerFPSReal->tick();
    _timerFPSLimited->tick();
    _state->setFrameInFlight(_timer->getFrameCounter() % _state->getSettings()->getMaxFramesInFlight());

    // business/application update loop callback
    uint32_t imageIndex;
    while (_getImageIndex(&imageIndex) != VK_SUCCESS)
      ;
    _callbackUpdate();
    _drawFrame(imageIndex);
    _timerFPSReal->tock();
    // if GPU frames are limited by driver it will happen during display
    _displayFrame(&imageIndex);

    _timer->sleep(_state->getSettings()->getDesiredFPS(), _loggerCPU);
    _timer->tock();
    _timerFPSLimited->tock();
  }
  vkDeviceWaitIdle(_state->getDevice()->getLogicalDevice());
}

std::shared_ptr<CommandBuffer> Core::getCommandBufferTransfer() { return _commandBufferTransfer; }

std::shared_ptr<ResourceManager> Core::getResourceManager() { return _resourceManager; }

std::shared_ptr<Postprocessing> Core::getPostprocessing() { return _postprocessing; }

std::shared_ptr<Blur> Core::getBlur() { return _blur; }

std::shared_ptr<LightManager> Core::getLightManager() { return _lightManager; }

std::shared_ptr<State> Core::getState() { return _state; }

std::shared_ptr<GUI> Core::getGUI() { return _gui; }

std::tuple<int, int> Core::getFPS() { return {_timerFPSLimited->getFPS(), _timerFPSReal->getFPS()}; }

void Core::addDrawable(std::shared_ptr<Drawable> drawable, AlphaType type) { _drawables[type].push_back(drawable); }

void Core::addShadowable(std::shared_ptr<Shadowable> shadowable) { _shadowables.push_back(shadowable); }

void Core::addAnimation(std::shared_ptr<Animation> animation) { _animations.push_back(animation); }

void Core::addSkybox(std::shared_ptr<Skybox> skybox) { _skybox = skybox; }

void Core::addParticleSystem(std::shared_ptr<ParticleSystem> particleSystem) {
  _particleSystem.push_back(particleSystem);
  addDrawable(particleSystem);
}

void Core::removeDrawable(std::shared_ptr<Drawable> drawable) {
  for (auto& [_, drawableVector] : _drawables)
    drawableVector.erase(std::remove(drawableVector.begin(), drawableVector.end(), drawable), drawableVector.end());
}

const std::vector<std::shared_ptr<Drawable>>& Core::getDrawables(AlphaType type) { return _drawables[type]; }

void Core::registerUpdate(std::function<void()> update) { _callbackUpdate = update; }

void Core::registerReset(std::function<void(int width, int height)> reset) { _callbackReset = reset; }