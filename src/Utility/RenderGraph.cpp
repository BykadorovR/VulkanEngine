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

void GraphPass::addColorTarget(std::string name, std::vector<std::shared_ptr<Image>> images) {
  _colorTargets[name] = images;
}

GraphPassStage GraphPass::getStage() { return _stage; }

std::map<std::string, std::vector<std::shared_ptr<Image>>> GraphPass::getColorTargets() { return _colorTargets; }

std::map<std::string, std::shared_ptr<Image>> GraphPass::getDepthTarget() { return _depthTarget; }

std::map<std::string, std::vector<std::shared_ptr<Buffer>>> GraphPass::getStorageInputs() { return _storageInputs; }

std::map<std::string, std::vector<std::shared_ptr<Buffer>>> GraphPass::getStorageOutputs() { return _storageOutputs; }

std::map<std::string, std::vector<std::shared_ptr<Buffer>>> GraphPass::getVertexBufferInputs() {
  return _vertexBufferInputs;
}

std::map<std::string, std::vector<std::shared_ptr<Image>>> GraphPass::getTextureInputs() { return _textureInputs; }

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

void GraphPass::addTextureInput(std::string name, std::vector<std::shared_ptr<Image>> images) {
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
    for (auto [name, resources] : value->getColorTargets()) {
      std::cout << " color target: " << name << "; ";
      for (auto resource : resources) {
        std::cout << resource->getImage() << " ";
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
    for (auto [name, resources] : value->getTextureInputs()) {
      std::cout << " texture input: " << name << "; ";
      for (auto resource : resources) {
        std::cout << resource->getImage() << " ";
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
  GraphPassStage passStagePrevious = _passApplication->getStage();
  for (int i = 0; i < _passesOrdered.size(); i++) {
    auto node = _passesOrdered[i];
    if (node->getStage() != passStagePrevious) queueTypeChange = true;
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
        auto swapchainImageViews = _swapchain->getImageViews();
        for (auto swapchainImageView : swapchainImageViews) {
          if (std::find(target.begin(), target.end(), swapchainImageView->getImage()) != target.end()) {
            node->addWaitSemaphore(_semaphoreImageAvailable);
            flagWaitForSwapchain = false;
            break;
          }
        }
        if (flagWaitForSwapchain == false) break;
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
  std::vector<VkPipelineStageFlags> waitStages;
  // first submit application
  {
    for (auto& semaphores : _passApplication->getSignalSemaphores()) {
      signalSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());
      waitSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());
      waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    // need to change the layout from SRC_KHR to GENERAL
    auto swapchainImageViews = _swapchain->getImageViews();
    auto textureInput = _swapchain->getImageViews()[_swapchain->getSwapchainIndex()];
    textureInput->getImage()->changeLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL,
                                           VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                                           _passApplication->getCommandBuffers()[frameInFlight]);
    std::cout << "Change layout: " << textureInput->getImage()->getImage()
              << ", from: VK_IMAGE_LAYOUT_PRESENT_SRC_KHR to VK_IMAGE_LAYOUT_GENERAL" << std::endl;
    // need to end command buffers before submit
    _passApplication->getCommandBuffers()[frameInFlight]->endCommands();
    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &_passApplication->getCommandBuffers()[frameInFlight]->getCommandBuffer(),
        .signalSemaphoreCount = (uint32_t)signalSemaphores.size(),
        .pSignalSemaphores = signalSemaphores.data()};
    vkQueueSubmit(_engineState->getDevice()->getQueue(vkb::QueueType::graphics), 1, &submitInfo, VK_NULL_HANDLE);
    std::cout << "Submit command buffer for pass: " << _passApplication << std::endl;
    signalSemaphores.clear();
  }
  // process rest of the stages
  GraphPassStage previousStage = _passApplication->getStage();
  for (int i = 0; i < renderFutures.size(); i++) {
    std::shared_ptr<GraphPass> graphPass = renderFutures[i].first;
    std::future<void> renderFuture = std::move(renderFutures[i].second);
    if (renderFuture.valid()) renderFuture.get();
    std::cout << "Handling: " << graphPass->getName() << std::endl;
    // stage change or last iteration
    if (previousStage != graphPass->getStage()) {
      // need to end command buffers before submit
      std::vector<VkCommandBuffer> commandBufferRawSubmit;
      for (auto& commandBuffer : commandBufferSubmit) {
        commandBuffer->endCommands();
        commandBufferRawSubmit.push_back(commandBuffer->getCommandBuffer());
      }
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
      std::cout << "Submit command buffer for pass: " << graphPass << std::endl;
      commandBufferSubmit.clear();
      signalSemaphores.clear();
      waitSemaphores.clear();
      waitStages.clear();
    }
    // else {
    //   // put barrier if needed
    //   // IMPORTANT: we should add any barrier to the previous stage because potentially all command buffer are
    //   already
    //   // recorded. So we need to add barrier to the end of the previous command buffer.
    //   for (auto [key, value] : graphPass->getTextureInputs()) {
    //     auto textureInput = value[frameInFlight];
    //     {
    //       switch (graphPass->getStage()) {
    //         case GraphPassStage::GRAPHIC: {
    //           VkImageLayout newImageLayout = textureInput->getImageLayout();
    //           VkImageLayout oldImageLayout = textureInput->getImageLayout();
    //           VkImageMemoryBarrier imageMemoryBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    //                                                   .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    //                                                   .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    //                                                   .oldLayout = oldImageLayout,
    //                                                   .newLayout = newImageLayout,
    //                                                   .image = textureInput->getImage(),
    //                                                   .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    //           vkCmdPipelineBarrier(renderFutures[i -
    //           1].first->getCommandBuffers()[frameInFlight]->getCommandBuffer(),
    //                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    //                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
    //                                &imageMemoryBarrier);
    //           std::cout << "Change layout: " << key << " " << textureInput->getImage() << ", from: " <<
    //           oldImageLayout
    //                     << " to "
    //                     << newImageLayout << std::endl;
    //           break;
    //         }
    //         case GraphPassStage::COMPUTE: {
    //           VkImageLayout newImageLayout = textureInput->getImageLayout();
    //           // the pass that writes to swapchain should use SRC_KHR layout as old
    //           // it will be transfered back to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR due to RenderPass
    //           VkImageLayout oldImageLayout = textureInput->getImageLayout();
    //           VkImageMemoryBarrier colorBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    //                                             .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    //                                             .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    //                                             .oldLayout = oldImageLayout,
    //                                             .newLayout = newImageLayout,
    //                                             .image = textureInput->getImage(),
    //                                             .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    //                                                                  .baseMipLevel = 0,
    //                                                                  .levelCount = 1,
    //                                                                  .baseArrayLayer = 0,
    //                                                                  .layerCount = 1}};
    //           vkCmdPipelineBarrier(renderFutures[i -
    //           1].first->getCommandBuffers()[frameInFlight]->getCommandBuffer(),
    //                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
    //                                nullptr, 0, nullptr, 1, &colorBarrier);
    //           std::cout << "Change layout: " << key << " " << textureInput->getImage() << ", from: " <<
    //           oldImageLayout
    //                     << " to "
    //                     << newImageLayout << std::endl;
    //           break;
    //         }
    //       };
    //     }
    //   }
    // }

    commandBufferSubmit.push_back(graphPass->getCommandBuffers()[frameInFlight]);
    for (auto& semaphores : graphPass->getSignalSemaphores())
      signalSemaphores.push_back(semaphores[frameInFlight]->getSemaphore());

    for (auto& semaphores : graphPass->getWaitSemaphores()) {
      switch (graphPass->getStage()) {
        case GraphPassStage::COMPUTE:
          waitStages.push_back(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
          break;
        case GraphPassStage::GRAPHIC:
          waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
          break;
        case GraphPassStage::TRANSFER:
          waitStages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);
          break;
      };
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
  std::cout << "Submit command buffer for last pass" << std::endl;
}