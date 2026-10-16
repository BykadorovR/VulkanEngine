#pragma once
#include <volk.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

import Command;

class DebugUtils {
 private:
  VkDevice _device;

 public:
  explicit DebugUtils(VkDevice device);
  template <class T>
  void setName(const std::string& name, VkObjectType type, T handler) {
    VkDebugUtilsObjectNameInfoEXT nameInfo = {VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    nameInfo.objectType = type;
    nameInfo.objectHandle = (uint64_t)handler;
    nameInfo.pObjectName = name.c_str();
    vkSetDebugUtilsObjectNameEXT(_device, &nameInfo);
  }

  template <class T>
  void setName(const std::string& name, VkObjectType type, const std::vector<T>& handlers) {
    for (std::size_t i = 0; i < handlers.size(); ++i) {
      std::string modified = name + " " + std::to_string(i);
      setName(modified, type, handlers[i]);
    }
  }
};

class Logger {
 private:
  class LoggerAndroid {
   public:
    void begin(std::string name);
    void end();
  };

  class LoggerUtils {
   public:
    void begin(std::string marker, const RenderGraph::CommandBuffer* buffer, std::array<float, 4> color);
    void end(const RenderGraph::CommandBuffer* buffer);
  };

  class LoggerNVTX {
   public:
    void begin(std::string name, std::array<float, 4> color = {0.f, 0.f, 0.f, 1.f});
    void end();
  };

  std::shared_ptr<LoggerAndroid> _loggerAndroid;
  std::shared_ptr<LoggerUtils> _loggerUtils;
  std::shared_ptr<LoggerNVTX> _loggerNVTX;
  std::mutex _mutex;

 public:
  Logger();
  void begin(std::string marker,
             const RenderGraph::CommandBuffer* buffer = nullptr,
             std::array<float, 4> color = {0.f, 0.f, 0.f, 1.f});
  void end(const RenderGraph::CommandBuffer* buffer = nullptr);
};
