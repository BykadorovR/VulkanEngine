#pragma once
#include "Device.h"
#include <map>

class Shader {
 private:
  std::shared_ptr<Device> _device;
  std::map<VkShaderStageFlagBits, VkPipelineShaderStageCreateInfo> _shaders;
  VkSpecializationInfo _specializationInfo;
  VkShaderModule _createShaderModule(const std::vector<char>& code);

 public:
  Shader(std::shared_ptr<Device> device);
  void add(std::string path, VkShaderStageFlagBits type);
  void setSpecializationInfo(VkSpecializationInfo info, VkShaderStageFlagBits type);
  VkPipelineShaderStageCreateInfo& getShaderStageInfo(VkShaderStageFlagBits type);
  ~Shader();
};