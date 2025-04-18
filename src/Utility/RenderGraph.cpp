#include "Utility/RenderGraph.h"

GraphPass::GraphPass(std::string name, GraphPassStage stage, std::shared_ptr<EngineState> engineState) {
  _name = name;
  _stage = stage;
  _engineState = engineState;

  switch (stage) {
    case GraphPassStage::GRAPHIC:
      _commandPool = std::make_shared<CommandPool>(vkb::QueueType::graphics, _engineState->getDevice());
      break;
    case GraphPassStage::COMPUTE:
      _commandPool = std::make_shared<CommandPool>(vkb::QueueType::compute, _engineState->getDevice());
      break;
  };
  _commandBuffers.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _commandBuffers[i] = std::make_shared<CommandBuffer>(_commandPool, _engineState->getDevice());
  }
}

void GraphPass::addColorTarget(std::string name, std::shared_ptr<ImageHolder> images) { _colorTargets[name] = images; }

GraphPassStage GraphPass::getStage() { return _stage; }

std::map<std::string, std::shared_ptr<ImageHolder>> GraphPass::getColorTargets() { return _colorTargets; }

std::map<std::string, std::shared_ptr<Image>> GraphPass::getDepthTarget() { return _depthTarget; }

std::map<std::string, std::vector<std::shared_ptr<Buffer>>> GraphPass::getStorageInputs() { return _storageInputs; }

std::map<std::string, std::vector<std::shared_ptr<Buffer>>> GraphPass::getStorageOutputs() { return _storageOutputs; }

std::map<std::string, std::vector<std::shared_ptr<Buffer>>> GraphPass::getVertexBufferInputs() {
  return _vertexBufferInputs;
}

std::map<std::string, std::shared_ptr<ImageHolder>> GraphPass::getTextureInputs() { return _textureInputs; }

void GraphPass::addRenderExecution(std::function<void(std::shared_ptr<CommandBuffer> commandBuffer)> renderExecution) {
  _renderExecution.push_back(renderExecution);
}

void GraphPass::execute() {
  for (auto& render : _renderExecution) {
    auto commandBuffer = _commandBuffers[_engineState->getFrameInFlight()];
    if (commandBuffer->getActive() == false) commandBuffer->beginCommands();
    render(commandBuffer);
  }
}

void GraphPass::addStorageInput(std::string name, std::vector<std::shared_ptr<Buffer>> buffers) {
  _storageInputs[name] = buffers;
}

void GraphPass::addStorageOutput(std::string name, std::vector<std::shared_ptr<Buffer>> buffers) {
  _storageOutputs[name] = buffers;
}

void GraphPass::addVertexBufferInput(std::string name, std::vector<std::shared_ptr<Buffer>> buffers) {
  _vertexBufferInputs[name] = buffers;
}

void GraphPass::addTextureInput(std::string name, std::shared_ptr<ImageHolder> images) {
  _textureInputs[name] = images;
}

void GraphPass::setDepthTarget(std::string name, std::shared_ptr<Image> image) { _depthTarget[name] = image; }

void GraphPass::setEnd(bool end) { _end = end; }

bool GraphPass::getEnd() { return _end; }

std::string GraphPass::getName() { return _name; }

void GraphPass::addSignalSemaphore(std::vector<std::shared_ptr<Semaphore>> signalSemaphore) {
  _signalSemaphores.push_back(signalSemaphore);
}

void GraphPass::addWaitSemaphore(std::vector<std::shared_ptr<Semaphore>> waitSemaphore) {
  _waitSemaphores.push_back(waitSemaphore);
}

std::vector<std::vector<std::shared_ptr<Semaphore>>> GraphPass::getSignalSemaphores() { return _signalSemaphores; }

std::vector<std::vector<std::shared_ptr<Semaphore>>> GraphPass::getWaitSemaphores() { return _waitSemaphores; }

void GraphPass::setCommandBuffers(std::vector<std::shared_ptr<CommandBuffer>> commandBuffers) {
  _commandBuffers = commandBuffers;
}

std::vector<std::shared_ptr<CommandBuffer>> GraphPass::getCommandBuffers() { return _commandBuffers; }

RenderGraph::RenderGraph(std::shared_ptr<Swapchain> swapchain,
                         std::shared_ptr<BS::thread_pool> threadPool,
                         std::shared_ptr<EngineState> engineState) {
  _swapchain = swapchain;
  _threadPool = threadPool;
  _engineState = engineState;

  // create 2 special semaphores
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _semaphoreRenderFinished.push_back(std::make_shared<Semaphore>(_engineState->getDevice()));
    _semaphoreImageAvailable.push_back(std::make_shared<Semaphore>(_engineState->getDevice()));
    _fenceInFlight.push_back(std::make_shared<Fence>(_engineState->getDevice()));
  }
}

std::vector<std::shared_ptr<Semaphore>> RenderGraph::getSemaphoreRenderFinished() { return _semaphoreRenderFinished; }

std::vector<std::shared_ptr<Semaphore>> RenderGraph::getSemaphoreImageAvailable() { return _semaphoreImageAvailable; }

std::vector<std::shared_ptr<Fence>> RenderGraph::getFenceInFlight() { return _fenceInFlight; }

std::shared_ptr<GraphPass> RenderGraph::getPass(std::string name, GraphPassStage stage) {
  auto passIt = std::find_if(_passes.begin(), _passes.end(), [name = name](std::shared_ptr<GraphPass> graphPass) {
    return graphPass->getName() == name;
  });
  if (passIt == _passes.end()) {
    auto pass = std::make_shared<GraphPass>(name, stage, _engineState);
    _passes.push_back(pass);
    return pass;
  }

  return *passIt;
}

std::shared_ptr<GraphPass> RenderGraph::getPassApplication() {
  if (_passApplication == nullptr) {
    _passApplication = std::make_shared<GraphPass>("Application", GraphPassStage::GRAPHIC, _engineState);
    std::vector<std::shared_ptr<Semaphore>> semaphoreApplicationReady;
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      semaphoreApplicationReady.push_back(std::make_shared<Semaphore>(_engineState->getDevice()));
    }
    _passApplication->addSignalSemaphore(semaphoreApplicationReady);
  }
  return _passApplication;
}

void RenderGraph::print() {
  std::deque<std::shared_ptr<GraphPass>> passes;
  passes.push_back(_passApplication);
  for (auto value : _passesOrdered) passes.push_back(value);
  for (auto value : passes) {
    std::cout << "Name: " << value->getName() << ", Stage : " << (int)value->getStage() << std::endl;
    for (auto waitSemaphores : value->getWaitSemaphores()) {
      std::cout << " wait semaphores: ";
      for (auto waitSemaphore : waitSemaphores) {
        std::cout << waitSemaphore->getSemaphore() << " ";
      }
      std::cout << std::endl;
    }
    for (auto signalSemaphores : value->getSignalSemaphores()) {
      std::cout << " signal semaphores: ";
      for (auto signalSemaphore : signalSemaphores) {
        std::cout << signalSemaphore->getSemaphore() << " ";
      }
      std::cout << std::endl;
    }
    for (auto [name, resource] : value->getColorTargets()) {
      std::cout << " color target: " << name << "; ";
      for (auto image : resource->getImages()) {
        std::cout << resource->getImage()->getImage() << " ";
      }
      std::cout << std::endl;
    }
    for (auto [name, resource] : value->getDepthTarget()) {
      std::cout << " depth target: " << name << "; " << resource->getImage() << std::endl;
    }
    for (auto [name, resources] : value->getStorageInputs()) {
      std::cout << " storage input: " << name << "; ";
      for (auto resource : resources) {
        std::cout << resource->getData() << " ";
      }
      std::cout << std::endl;
    }
    for (auto [name, resources] : value->getStorageOutputs()) {
      std::cout << " storage output: " << name << "; ";
      for (auto resource : resources) {
        std::cout << resource->getData() << " ";
      }
      std::cout << std::endl;
    }
    for (auto [name, resource] : value->getTextureInputs()) {
      std::cout << " texture input: " << name << "; ";
      for (auto image : resource->getImages()) {
        std::cout << resource->getImage()->getImage() << " ";
      }
      std::cout << std::endl;
    }
    for (auto [name, resources] : value->getVertexBufferInputs()) {
      std::cout << " vertex buffer input: " << name << "; ";
      for (auto resource : resources) {
        std::cout << resource->getData() << " ";
      }
      std::cout << std::endl;
    }
  }
}

void RenderGraph::calculate() {
  auto getDependencies = [&](std::string name) -> std::vector<std::string> {
    std::vector<std::string> dependencies;
    auto value = *std::find_if(_passes.rbegin(), _passes.rend(), [name = name](std::shared_ptr<GraphPass> graphPass) {
      return graphPass->getName() == name;
    });
    for (auto [name, resource] : value->getStorageInputs()) {
      dependencies.push_back(name);
    }
    for (auto [name, resource] : value->getStorageOutputs()) {
      dependencies.push_back(name);
    }
    for (auto [name, resource] : value->getTextureInputs()) {
      dependencies.push_back(name);
    }
    for (auto [name, resource] : value->getVertexBufferInputs()) {
      dependencies.push_back(name);
    }
    for (auto [name, resource] : value->getColorTargets()) {
      dependencies.push_back(name);
    }

    return dependencies;
  };

  auto findTarget = [](std::vector<std::shared_ptr<GraphPass>> passes,
                       std::string findName) -> std::shared_ptr<GraphPass> {
    for (auto it = passes.rbegin(); it != passes.rend(); ++it) {
      auto value = *it;
      for (auto [name, resource] : value->getColorTargets()) {
        if (name == findName) return value;
      }
      for (auto [name, resource] : value->getDepthTarget()) {
        if (name == findName) return value;
      }
      for (auto [name, resource] : value->getStorageOutputs()) {
        if (name == findName) return value;
      }
    }
    return nullptr;
  };

  std::shared_ptr<GraphPass> root{nullptr};
  for (auto& value : _passes) {
    if (value->getEnd()) root = {value};
  }
  auto passesBackup = _passes;
  // we should for every root pass run traversal process
  std::function<void(std::shared_ptr<GraphPass>)> traverse = [&](std::shared_ptr<GraphPass> node) {
    passesBackup.erase(std::remove(passesBackup.begin(), passesBackup.end(), node), passesBackup.end());
    _passesOrdered.push_front(node);
    auto dependencies = getDependencies(node->getName());
    for (auto dependency : dependencies) {
      // we want to find who writes to this texture, so we are looking for color target or depth target
      auto targetPass = findTarget(passesBackup, dependency);
      if (targetPass) {
        traverse(targetPass);
      }
    }
  };

  if (root) {
    traverse(root);
  }

  // set semaphores between passes
  bool flagWaitForSwapchain = true;
  bool queueTypeChange = false;
  std::optional<GraphPassStage> passStagePrevious;
  for (int i = 0; i < _passesOrdered.size(); i++) {
    auto node = _passesOrdered[i];
    if (passStagePrevious.has_value() && node->getStage() != passStagePrevious) queueTypeChange = true;
    // signal semaphore for the previous pass
    // wait semaphore for the current pass
    if (queueTypeChange) {
      std::vector<std::shared_ptr<Semaphore>> semaphoreQueueType;
      for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
        semaphoreQueueType.push_back(std::make_shared<Semaphore>(_engineState->getDevice()));
      }
      node->addWaitSemaphore(semaphoreQueueType);
      _passesOrdered[i - 1]->addSignalSemaphore(semaphoreQueueType);
      queueTypeChange = false;
    }
    // special case if we read from swapchain
    // who first interact with swapchain that should wait for the semaphore
    if (flagWaitForSwapchain) {
      for (auto [name, target] : node->getColorTargets()) {
        if (std::dynamic_pointer_cast<ImageHolderSwapchain>(target)) {
          node->addWaitSemaphore(_semaphoreImageAvailable);
          flagWaitForSwapchain = false;
          break;
        }
      }
    }
    // end node should signal end semaphore
    if (node->getEnd()) {
      if (node->getSignalSemaphores().size() == 0) {
        node->addSignalSemaphore(_semaphoreRenderFinished);
      }
    }

    passStagePrevious = node->getStage();
  }
}

void RenderGraph::render() {
  auto frameInFlight = _engineState->getFrameInFlight();
  // application update should be called in main thread because of ImGUI
  _passApplication->execute();

  std::vector<std::pair<std::shared_ptr<GraphPass>, std::future<void>>> renderFutures;
  for (auto& pass : _passesOrdered) {
    renderFutures.push_back({pass, _threadPool->submit(std::bind(&GraphPass::execute, pass))});
  }

  std::vector<std::shared_ptr<CommandBuffer>> commandBufferSubmit;
  std::vector<VkSemaphore> signalSemaphores;
  std::vector<VkSemaphore> waitSemaphores;
  // first submit application
  {
    for (auto& semaphores : _passApplication->getSignalSemaphores()) {
      signalSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());
      waitSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());
    }
    // need to change the layout from SRC_KHR to GENERAL
    auto swapchainImageViews = _swapchain->getImageViews();
    auto textureInput = _swapchain->getImageViews()[_swapchain->getSwapchainIndex()];
    textureInput->getImage()->changeLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL,
                                           VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                                           _passApplication->getCommandBuffers()[frameInFlight]);
    // need to end command buffers before submit
    _passApplication->getCommandBuffers()[frameInFlight]->endCommands();
    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &_passApplication->getCommandBuffers()[frameInFlight]->getCommandBuffer(),
        .signalSemaphoreCount = (uint32_t)signalSemaphores.size(),
        .pSignalSemaphores = signalSemaphores.data()};
    vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::graphics), 1, &submitInfo, VK_NULL_HANDLE);
    signalSemaphores.clear();
  }
  // process rest of the stages
  std::optional<GraphPassStage> previousStage;
  for (int i = 0; i < renderFutures.size(); i++) {
    std::shared_ptr<GraphPass> graphPass = renderFutures[i].first;
    std::future<void> renderFuture = std::move(renderFutures[i].second);
    if (renderFuture.valid()) renderFuture.get();
    // stage change or last iteration
    if (previousStage.has_value()) {
      if (previousStage != graphPass->getStage()) {
        // need to end command buffers before submit
        std::vector<VkCommandBuffer> commandBufferRawSubmit;
        for (auto& commandBuffer : commandBufferSubmit) {
          commandBuffer->endCommands();
          commandBufferRawSubmit.push_back(commandBuffer->getCommandBuffer());
        }

        std::vector<VkPipelineStageFlags> waitStages(waitSemaphores.size());
        if (previousStage == GraphPassStage::COMPUTE)
          for (auto& waitStage : waitStages) waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        else
          for (auto& waitStage : waitStages) waitStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        // submit + semaphores
        VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                .waitSemaphoreCount = (uint32_t)waitSemaphores.size(),
                                .pWaitSemaphores = waitSemaphores.data(),
                                .pWaitDstStageMask = waitStages.data(),
                                .commandBufferCount = (uint32_t)commandBufferRawSubmit.size(),
                                .pCommandBuffers = commandBufferRawSubmit.data(),
                                .signalSemaphoreCount = (uint32_t)signalSemaphores.size(),
                                .pSignalSemaphores = signalSemaphores.data()};
        if (previousStage == GraphPassStage::COMPUTE)
          vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::compute), 1, &submitInfo, VK_NULL_HANDLE);
        else
          vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::graphics), 1, &submitInfo, VK_NULL_HANDLE);
        //
        commandBufferSubmit.clear();
        signalSemaphores.clear();
        waitSemaphores.clear();
        waitStages.clear();
      } else {
        // put EXECUTION AND MEMORY barriers if needed (not layout transition ones)
        // IMPORTANT: we should add any barrier to the previous stage because potentially all command buffer are already
        // recorded. So we need to add barrier to the end of the previous command buffer.
        {
          switch (graphPass->getStage()) {
            case GraphPassStage::GRAPHIC: {
              std::set<std::shared_ptr<Image>> images;
              for (auto& [key, value] : graphPass->getTextureInputs()) {
                images.insert(value->getImage());
              }

              std::vector<VkImageMemoryBarrier> executionBarriers;
              for (auto& image : images) {
                executionBarriers.push_back(
                    VkImageMemoryBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                         .oldLayout = image->getImageLayout(),
                                         .newLayout = image->getImageLayout(),
                                         .image = image->getImage(),
                                         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}});
              }
              vkCmdPipelineBarrier(renderFutures[i - 1].first->getCommandBuffers()[frameInFlight]->getCommandBuffer(),
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   0, 0, nullptr, 0, nullptr, executionBarriers.size(), executionBarriers.data());
              break;
            }
            case GraphPassStage::COMPUTE: {
              std::vector<VkImageMemoryBarrier> imageBarriers;
              for (auto& [key, value] : graphPass->getTextureInputs()) {
                std::shared_ptr<Image> image = value->getImage();
                imageBarriers.push_back(
                    VkImageMemoryBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                         .oldLayout = image->getImageLayout(),
                                         .newLayout = image->getImageLayout(),
                                         .image = image->getImage(),
                                         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}});
              }
              std::vector<VkBufferMemoryBarrier> bufferBarriers;
              for (auto& [key, value] : graphPass->getStorageInputs()) {
                bufferBarriers.push_back(VkBufferMemoryBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                                               .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                                               .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                                               .buffer = value[frameInFlight]->getData(),
                                                               .size = VK_WHOLE_SIZE});
              }
              vkCmdPipelineBarrier(renderFutures[i - 1].first->getCommandBuffers()[frameInFlight]->getCommandBuffer(),
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                                   nullptr, bufferBarriers.size(), bufferBarriers.data(), imageBarriers.size(),
                                   imageBarriers.data());
              break;
            }
          };
        }
      }
    }

    commandBufferSubmit.push_back(graphPass->getCommandBuffers()[frameInFlight]);
    for (auto& semaphores : graphPass->getSignalSemaphores())
      signalSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());

    for (auto& semaphores : graphPass->getWaitSemaphores()) {
      waitSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());
    }

    previousStage = graphPass->getStage();
  }
  // need to end command buffers before submit
  std::vector<VkCommandBuffer> commandBufferRawSubmit;
  for (auto& commandBuffer : commandBufferSubmit) {
    commandBuffer->endCommands();
    commandBufferRawSubmit.push_back(commandBuffer->getCommandBuffer());
  }

  std::vector<VkPipelineStageFlags> waitStages(waitSemaphores.size());
  if (previousStage == GraphPassStage::COMPUTE)
    for (auto& waitStage : waitStages) waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  else
    for (auto& waitStage : waitStages) waitStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  // submit remaining commands
  VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                          .waitSemaphoreCount = (uint32_t)waitSemaphores.size(),
                          .pWaitSemaphores = waitSemaphores.data(),
                          .pWaitDstStageMask = waitStages.data(),
                          .commandBufferCount = (uint32_t)commandBufferRawSubmit.size(),
                          .pCommandBuffers = commandBufferRawSubmit.data(),
                          .signalSemaphoreCount = (uint32_t)signalSemaphores.size(),
                          .pSignalSemaphores = signalSemaphores.data()};
  if (previousStage == GraphPassStage::COMPUTE)
    vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::compute), 1, &submitInfo,
                  _fenceInFlight[frameInFlight]->getFence());
  else
    vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::graphics), 1, &submitInfo,
                  _fenceInFlight[frameInFlight]->getFence());
}

void RenderGraph::reset() {
  std::vector<std::shared_ptr<Image>> images;
  for (auto& imageView : _swapchain->getImageViews()) images.push_back(imageView->getImage());
  auto swapchainHolder = std::make_shared<ImageHolderSwapchain>(images, _swapchain);
  for (auto& pass : _passesOrdered) {
    // get all texture inputs/color target and reinitialize
    for (auto [key, value] : pass->getColorTargets()) {
      if (std::dynamic_pointer_cast<ImageHolderSwapchain>(value)) {
        pass->addColorTarget(key, swapchainHolder);
      }
    }
    for (auto [key, value] : pass->getTextureInputs()) {
      if (std::dynamic_pointer_cast<ImageHolderSwapchain>(value)) {
        pass->addTextureInput(key, swapchainHolder);
      }
    }
  }
}

ImageHolder::ImageHolder(std::vector<std::shared_ptr<Image>> images) { _images = images; }

std::vector<std::shared_ptr<Image>> ImageHolder::getImages() { return _images; }

ImageHolderSwapchain::ImageHolderSwapchain(std::vector<std::shared_ptr<Image>> images,
                                           std::shared_ptr<Swapchain> swapchain)
    : ImageHolder(images) {
  _swapchain = swapchain;
}
std::shared_ptr<Image> ImageHolderSwapchain::getImage() { return _images[_swapchain->getSwapchainIndex()]; }

ImageHolderFlight::ImageHolderFlight(std::vector<std::shared_ptr<Image>> images,
                                     std::shared_ptr<EngineState> engineState)
    : ImageHolder(images) {
  _engineState = engineState;
}

std::shared_ptr<Image> ImageHolderFlight::getImage() { return _images[_engineState->getFrameInFlight()]; }