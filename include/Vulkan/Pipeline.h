#pragma once
#include "Utility/Settings.h"
#include "Vulkan/Shader.h"
#include "Vulkan/Buffer.h"
#include "Vulkan/Descriptor.h"
#include "Vulkan/Render.h"

class Pipeline {
 protected:
  std::shared_ptr<Device> _device;
  std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>> _descriptorSetLayout;
  std::map<std::string, VkPushConstantRange> _pushConstants;
  VkPipeline _pipeline = VK_NULL_HANDLE;
  VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;

 public:
  Pipeline(std::shared_ptr<Device> device);
  std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>>& getDescriptorSetLayout();
  std::map<std::string, VkPushConstantRange>& getPushConstants();
  VkPipeline& getPipeline();
  VkPipelineLayout& getPipelineLayout();
  ~Pipeline();
};

class PipelineGraphic : public Pipeline {
 private:
  VkPipelineDynamicStateCreateInfo _dynamicState;
  std::vector<VkDynamicState> _dynamicStates;
  VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
  VkPipelineViewportStateCreateInfo _viewportState;
  VkPipelineRasterizationStateCreateInfo _rasterizer;
  VkPipelineMultisampleStateCreateInfo _multisampling;
  VkPipelineColorBlendAttachmentState _blendAttachmentState;
  VkPipelineColorBlendStateCreateInfo _colorBlending;
  VkPipelineDepthStencilStateCreateInfo _depthStencil;
  std::optional<VkPipelineTessellationStateCreateInfo> _tessellationState;
  std::shared_ptr<RenderPassManager> _renderPassManager;
  VkVertexInputBindingDescription _bindingDescription;
  std::vector<VkVertexInputAttributeDescription> _attributeDescriptions;
  std::shared_ptr<Shader> _shader;

  VkGraphicsPipelineCreateInfo _pipelineInfoDynamic;
  void _createPipelineLayout(
      std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>> descriptorSetLayout,
      std::map<std::string, VkPushConstantRange> pushConstants);
  void _createCustomStatic(
      std::shared_ptr<Shader> shader,
      std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>> descriptorSetLayout,
      std::map<std::string, VkPushConstantRange> pushConstants,
      VkVertexInputBindingDescription bindingDescription,
      std::vector<VkVertexInputAttributeDescription> attributeDescriptions,
      std::shared_ptr<RenderPass> renderPass);
  void _notifyDynamic(std::shared_ptr<RenderPass> renderPass);

 public:
  PipelineGraphic(std::shared_ptr<RenderPassManager> renderPassManager, std::shared_ptr<Device> device);
  void setCullMode(VkCullModeFlags cullMode);
  void setPolygonMode(VkPolygonMode polygonMode);
  void setAlphaBlending(bool alphaBlending);
  void setTopology(VkPrimitiveTopology topology);
  void setDepthBias(bool depthBias);
  void setDepthTest(bool depthTest);
  void setDepthWrite(bool depthWrite);
  void setDepthCompateOp(VkCompareOp depthCompareOp);
  void setColorBlendOp(VkBlendOp colorBlendOp);
  void setTesselation(int patchControlPoints);
  void createCustom(std::shared_ptr<Shader> shader,
                    std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>> descriptorSetLayout,
                    std::map<std::string, VkPushConstantRange> pushConstants,
                    VkVertexInputBindingDescription bindingDescription,
                    std::vector<VkVertexInputAttributeDescription> attributeDescriptions,
                    RenderPassScenario renderPassScenario);
  void createCustom(std::shared_ptr<Shader> shader,
                    std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>> descriptorSetLayout,
                    std::map<std::string, VkPushConstantRange> pushConstants,
                    VkVertexInputBindingDescription bindingDescription,
                    std::vector<VkVertexInputAttributeDescription> attributeDescriptions,
                    std::shared_ptr<RenderPass> renderPass);
};

class PipelineCompute : public Pipeline {
 public:
  PipelineCompute(std::shared_ptr<Device> device);
  void createCustom(std::shared_ptr<Shader> shader,
                    std::vector<std::pair<std::string, std::shared_ptr<DescriptorSetLayout>>> descriptorSetLayout,
                    std::map<std::string, VkPushConstantRange> pushConstants);
};