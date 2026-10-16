#include "nvtx3/nvtx3.hpp"
#include <algorithm>
#ifdef __ANDROID__
#include <android/trace.h>
#endif
#include "Utility/Logger.h"

DebugUtils::DebugUtils(VkDevice device) : _device(device) {}

void Logger::LoggerAndroid::begin(std::string name) {
#ifdef __ANDROID__
  ATrace_beginSection(name.c_str());
#else
  static_cast<void>(name);
#endif
}

void Logger::LoggerAndroid::end() {
#ifdef __ANDROID__
  ATrace_endSection();
#endif
}

void Logger::LoggerUtils::begin(std::string marker,
                                const RenderGraph::CommandBuffer* buffer,
                                std::array<float, 4> color) {
  VkDebugUtilsLabelEXT markerInfo = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, .pLabelName = marker.c_str()};
  std::copy(color.begin(), color.end(), markerInfo.color);
  vkCmdBeginDebugUtilsLabelEXT(buffer->getCommandBuffer(), &markerInfo);
}

void Logger::LoggerUtils::end(const RenderGraph::CommandBuffer* buffer) {
  vkCmdEndDebugUtilsLabelEXT(buffer->getCommandBuffer());
}

void Logger::LoggerNVTX::begin(std::string name, std::array<float, 4> color) {
  nvtxEventAttributes_t eventAttrib = {0};
  eventAttrib.version = NVTX_VERSION;
  eventAttrib.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;

  eventAttrib.colorType = NVTX_COLOR_ARGB;
  const auto toByte = [](float component) { return static_cast<uint32_t>(std::clamp(component, 0.0f, 1.0f) * 255.0f); };
  const uint32_t convertedColor = (toByte(color[3]) << 24) | (toByte(color[0]) << 16) | (toByte(color[1]) << 8) |
                                  toByte(color[2]);
  eventAttrib.color = convertedColor;

  eventAttrib.messageType = NVTX_MESSAGE_TYPE_ASCII;
  eventAttrib.message.ascii = name.c_str();

  nvtxRangePushEx(&eventAttrib);
}

void Logger::LoggerNVTX::end() { nvtxRangePop(); }

Logger::Logger() {
#ifdef __ANDROID__
  _loggerAndroid = std::make_shared<LoggerAndroid>();
#else
  _loggerUtils = std::make_shared<LoggerUtils>();
  _loggerNVTX = std::make_shared<LoggerNVTX>();
#endif
}

void Logger::begin(std::string marker, const RenderGraph::CommandBuffer* buffer, std::array<float, 4> color) {
  std::unique_lock<std::mutex> lock(_mutex);
#ifdef __ANDROID__
  _loggerAndroid->begin(marker);
#else
  if (buffer)
    _loggerUtils->begin(marker, buffer, color);
  else
    _loggerNVTX->begin(marker, color);
#endif
}

void Logger::end(const RenderGraph::CommandBuffer* buffer) {
  std::unique_lock<std::mutex> lock(_mutex);
#ifdef __ANDROID__
  _loggerAndroid->end();
#else
  if (buffer)
    _loggerUtils->end(buffer);
  else
    _loggerNVTX->end();
#endif
}
