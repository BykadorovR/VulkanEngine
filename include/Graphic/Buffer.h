#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>
#include <vk_mem_alloc.h>
#include "Utility/EngineState.h"

#undef far

import Buffer;

struct ImageDataBuffer {
  std::shared_ptr<RenderGraph::Buffer> buffer;
  std::tuple<int, int> resolution;
  int channels = 0;
  int number = 0;

  ImageDataBuffer(std::tuple<int, int> resolution,
                  int channels,
                  int number,
                  VkBufferUsageFlags usage,
                  VmaAllocationCreateFlags allocationFlags,
                  const RenderGraph::MemoryAllocator& memoryAllocator)
      : resolution(resolution),
        channels(channels),
        number(number) {
    const auto [width, height] = resolution;
    buffer = std::make_shared<RenderGraph::Buffer>(static_cast<VkDeviceSize>(width * height * channels * number), usage,
                                                   allocationFlags, memoryAllocator);
  }

  std::tuple<int, int> getResolution() const noexcept { return resolution; }
  int getChannels() const noexcept { return channels; }
  int getNumber() const noexcept { return number; }

  template <class T>
  void setData(const T* data, VkDeviceSize size, VkDeviceSize offset = 0) {
    const auto& allocationInfo = buffer->getAllocationInfo();
    if (allocationInfo.pMappedData == nullptr) throw std::logic_error("buffer memory is not mapped");
    if (offset + size > buffer->getSize()) throw std::out_of_range("buffer write exceeds allocation size");
    std::memcpy(static_cast<std::byte*>(allocationInfo.pMappedData) + offset, data, static_cast<std::size_t>(size));
  }

  template <class T>
  void setData(const T* data) {
    setData(data, buffer->getSize());
  }
};

template <class T>
class VertexBuffer {
 protected:
  std::shared_ptr<RenderGraph::Buffer> _buffer;
  std::shared_ptr<EngineState> _engineState;
  std::vector<T> _vertices;
  VkBufferUsageFlagBits _type;

 public:
  VertexBuffer(VkBufferUsageFlagBits type, std::shared_ptr<EngineState> engineState)
      : _engineState(std::move(engineState)),
        _type(type) {}
  std::vector<T> getData() { return _vertices; }
  std::shared_ptr<RenderGraph::Buffer> getBuffer() { return _buffer; }
};

template <class T>
class VertexBufferDynamic : public VertexBuffer<T> {
 public:
  VertexBufferDynamic(VkBufferUsageFlagBits type, std::shared_ptr<EngineState> engineState)
      : VertexBuffer<T>(type, std::move(engineState)) {}

  void setData(std::vector<T> vertices) {
    this->_vertices = std::move(vertices);
    const VkDeviceSize bufferSize = sizeof(this->_vertices[0]) * this->_vertices.size();
    if (this->_buffer == nullptr || bufferSize != this->_buffer->getSize()) {
      this->_buffer = std::make_shared<RenderGraph::Buffer>(
          bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | this->_type,
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
          *this->_engineState->getMemoryAllocator());
    }
    std::memcpy(this->_buffer->getAllocationInfo().pMappedData, this->_vertices.data(),
                static_cast<std::size_t>(bufferSize));
  }
};

template <class T>
class VertexBufferStatic : public VertexBuffer<T> {
 public:
  VertexBufferStatic(VkBufferUsageFlagBits type, std::shared_ptr<EngineState> engineState)
      : VertexBuffer<T>(type, std::move(engineState)) {}

  void setData(std::vector<T> vertices) {
    this->_vertices = std::move(vertices);
    const VkDeviceSize bufferSize = sizeof(this->_vertices[0]) * this->_vertices.size();
    if (this->_buffer == nullptr || bufferSize != this->_buffer->getSize()) {
      this->_buffer = std::make_shared<RenderGraph::Buffer>(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | this->_type,
                                                            0, *this->_engineState->getMemoryAllocator());
    }

    const auto bytes = std::as_bytes(std::span(this->_vertices));
    this->_engineState->getResourceUploader()->upload(*this->_buffer, bytes);
    this->_engineState->getResourceUploader()->submit();
  }
};
