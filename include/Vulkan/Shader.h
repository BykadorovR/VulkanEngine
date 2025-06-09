#pragma once
#include "Utility/EngineState.h"
#include <map>

class Shader {
 private:
  std::shared_ptr<EngineState> _engineState;
  std::map<VkShaderStageFlagBits, VkPipelineShaderStageCreateInfo> _shaders;
  VkSpecializationInfo _specializationInfo;
  VkShaderModule _createShaderModule(const std::vector<char>& code);

 public:
  Shader(std::shared_ptr<EngineState> device);
  void add(std::string path, VkShaderStageFlagBits type);
  void setSpecializationInfo(VkSpecializationInfo info, VkShaderStageFlagBits type);
  std::vector<VkPipelineShaderStageCreateInfo> getShaderStageInfos();
  VkPipelineShaderStageCreateInfo getShaderStageInfo(VkShaderStageFlagBits type);
  ~Shader();
};