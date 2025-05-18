#pragma once
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <deque>
#include "Vulkan/Image.h"
#include "Vulkan/Swapchain.h"
#include "Vulkan/Sync.h"
#include "BS_thread_pool.hpp"

class ImageHolder {
 protected:
  std::vector<std::shared_ptr<Image>> _images;

 public:
  ImageHolder(std::vector<std::shared_ptr<Image>> images);
  virtual std::shared_ptr<Image> getImage() = 0;
  virtual int getIndex() = 0;
  std::vector<std::shared_ptr<Image>> getImages();
};

class ImageHolderSwapchain : public ImageHolder {
 private:
  std::shared_ptr<Swapchain> _swapchain;

 public:
  ImageHolderSwapchain(std::vector<std::shared_ptr<Image>> images, std::shared_ptr<Swapchain> swapchain);
  int getIndex() override;
  std::shared_ptr<Image> getImage() override;
};

class ImageHolderFlight : public ImageHolder {
 private:
  std::shared_ptr<EngineState> _engineState;

 public:
  ImageHolderFlight(std::vector<std::shared_ptr<Image>> images, std::shared_ptr<EngineState> engineState);
  int getIndex() override;
  std::shared_ptr<Image> getImage() override;
};

enum class GraphPassStage { GRAPHIC = 0, COMPUTE = 1, TRANSFER = 2 };

class GraphStorage {
 private:
  std::map<std::string, std::shared_ptr<ImageHolder>> _imageHolders;
  std::map<std::string, std::shared_ptr<Image>> _images;
  std::map<std::string, std::vector<std::shared_ptr<Buffer>>> _buffers;

 public:
  void add(std::string name, std::shared_ptr<ImageHolder> imageHolder);
  void add(std::string name, std::shared_ptr<Image> image);
  void add(std::string name, std::vector<std::shared_ptr<Buffer>> buffers);
  std::shared_ptr<ImageHolder> getImageHolder(std::string name);
  std::shared_ptr<Image> getImage(std::string name);
  std::vector<std::shared_ptr<Buffer>> getBuffer(std::string name);
};

class GraphPass {
 private:
  std::string _name;
  GraphPassStage _stage;
  std::shared_ptr<GraphStorage> _graphStorage;
  std::shared_ptr<EngineState> _engineState;
  std::vector<std::string> _colorTargets, _storageInputs, _storageOutputs, _vertexBufferInputs, _textureInputs;
  std::optional<std::string> _depthTarget;
  std::shared_ptr<CommandPool> _commandPool;
  std::vector<std::shared_ptr<CommandBuffer>> _commandBuffers;
  std::shared_ptr<RenderPass> _renderPass;
  std::map<std::vector<int>, std::vector<std::shared_ptr<Framebuffer>>> _frameBuffers;
  std::vector<std::vector<std::shared_ptr<Semaphore>>> _signalSemaphores, _waitSemaphores;
  std::vector<std::function<void(std::vector<std::shared_ptr<Framebuffer>> framebuffer,
                                 std::shared_ptr<CommandBuffer> commandBuffer)>>
      _renderExecutions;
  std::vector<std::function<void(std::shared_ptr<CommandBuffer> commandBuffer)>> _computeExecutions;
  bool _end = false;

 public:
  GraphPass(std::string name,
            GraphPassStage stage,
            std::shared_ptr<GraphStorage> graphStorage,
            std::shared_ptr<EngineState> engineState);
  // handle attachments
  void addColorTarget(std::string name);
  void setDepthTarget(std::string name);
  // handle input to shaders
  void addTextureInput(std::string name);
  void addUniformInput();
  void addStorageInput(std::string name);
  // handle output from shaders
  void addStorageOutput(std::string name);
  // handle vertices
  void addVertexBufferInput(std::string name);
  void addIndexBufferInput();
  // mark the last vertex in render graph
  void setEnd(bool end);

  void addSignalSemaphore(std::vector<std::shared_ptr<Semaphore>> signalSemaphore);
  void addWaitSemaphore(std::vector<std::shared_ptr<Semaphore>> waitSemaphore);
  std::vector<std::vector<std::shared_ptr<Semaphore>>> getSignalSemaphores();
  std::vector<std::vector<std::shared_ptr<Semaphore>>> getWaitSemaphores();

  void setCommandBuffers(std::vector<std::shared_ptr<CommandBuffer>> commandBuffers);
  void setRenderPass(std::shared_ptr<RenderPass> renderPass);
  std::shared_ptr<RenderPass> getRenderPass();
  void setFrameBuffers(std::map<std::vector<int>, std::vector<std::shared_ptr<Framebuffer>>> frameBuffers);
  std::map<std::vector<int>, std::vector<std::shared_ptr<Framebuffer>>> getFrameBuffers();
  std::vector<std::shared_ptr<CommandBuffer>> getCommandBuffers();

  GraphPassStage getStage();
  std::vector<std::string> getColorTargets();
  std::optional<std::string> getDepthTarget();
  std::vector<std::string> getStorageInputs();
  std::vector<std::string> getStorageOutputs();
  std::vector<std::string> getVertexBufferInputs();
  std::vector<std::string> getTextureInputs();
  bool getEnd();
  std::string getName();
  // set function that does render pass work
  void addComputeExecution(std::function<void(std::shared_ptr<CommandBuffer>)> computeExecution);
  void addRenderExecution(
      std::function<void(std::vector<std::shared_ptr<Framebuffer>>, std::shared_ptr<CommandBuffer>)> renderExecution);
  void execute();
};

class RenderGraph {
 private:
  std::vector<std::shared_ptr<GraphPass>> _passes;
  std::shared_ptr<GraphStorage> _graphStorage;
  std::shared_ptr<BS::thread_pool> _threadPool;
  std::shared_ptr<GraphPass> _passApplication;
  std::deque<std::shared_ptr<GraphPass>> _passesOrdered;
  std::shared_ptr<Swapchain> _swapchain;
  std::shared_ptr<EngineState> _engineState;
  // special semaphores
  std::vector<std::shared_ptr<Semaphore>> _semaphoreRenderFinished, _semaphoreImageAvailable;
  std::vector<std::shared_ptr<Fence>> _fenceInFlight;

  void _calculateFrameBuffers();

 public:
  RenderGraph(std::shared_ptr<Swapchain> swapchain,
              std::shared_ptr<BS::thread_pool> threadPool,
              std::shared_ptr<EngineState> engineState);
  std::shared_ptr<GraphStorage> getGraphStorage();
  std::shared_ptr<GraphPass> getPass(std::string name, GraphPassStage stage);
  std::shared_ptr<GraphPass> getPassApplication();
  std::vector<std::shared_ptr<Semaphore>> getSemaphoreRenderFinished();
  std::vector<std::shared_ptr<Semaphore>> getSemaphoreImageAvailable();
  std::vector<std::shared_ptr<Fence>> getFenceInFlight();
  void calculate();
  void print();
  void render();
  void reset();
};