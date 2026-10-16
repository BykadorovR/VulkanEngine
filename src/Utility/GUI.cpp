#include <cstring>
#include <string>
#include <utility>
#include <vk_mem_alloc.h>
#include "Utility/GUI.h"

import Shader;

GUI::GUI(std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;
  _resolution = engineState->getSettings()->getResolution();

  _vertexBuffer.resize(engineState->getSettings()->getMaxFramesInFlight());
  _indexBuffer.resize(engineState->getSettings()->getMaxFramesInFlight());
  _vertexCount.resize(engineState->getSettings()->getMaxFramesInFlight(), 0);
  _indexCount.resize(engineState->getSettings()->getMaxFramesInFlight(), 0);

  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;

  ImGuiStyle& style = ImGui::GetStyle();
  // Color scheme
  style.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
  style.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
  style.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
  // Dimensions
  io.DisplaySize = ImVec2(static_cast<float>(std::get<0>(_resolution)), static_cast<float>(std::get<1>(_resolution)));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

void GUI::reset() {
  ImGuiIO& io = ImGui::GetIO();
  _resolution = _engineState->getSettings()->getResolution();
  io.DisplaySize = ImVec2(static_cast<float>(std::get<0>(_resolution)), static_cast<float>(std::get<1>(_resolution)));
}

void GUI::initialize(const RenderGraph::CommandBuffer* commandBufferTransfer) {
  ImGuiIO& io = ImGui::GetIO();

  // Create font texture
  unsigned char* fontData;
  int texWidth, texHeight;
  io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
  VkDeviceSize uploadSize = texWidth * texHeight * 4 * sizeof(char);

  // check if device extensions are supported
  uint32_t extensionCount;
  vkEnumerateDeviceExtensionProperties(_engineState->getDevice()->getPhysicalDevice(), nullptr, &extensionCount,
                                       nullptr);

  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  vkEnumerateDeviceExtensionProperties(_engineState->getDevice()->getPhysicalDevice(), nullptr, &extensionCount,
                                       availableExtensions.data());
  bool supported = false;
  for (const auto& extension : availableExtensions) {
    if (std::string(extension.extensionName) == std::string(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)) {
      supported = true;
      break;
    }
  }

  auto stagingBuffer = std::make_shared<RenderGraph::Buffer>(
      uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      *_engineState->getMemoryAllocator());

  std::memcpy(static_cast<std::byte*>(stagingBuffer->getAllocationInfo().pMappedData), fontData,
              static_cast<std::size_t>(stagingBuffer->getSize()));

  _fontImage = std::make_unique<RenderGraph::Image>(*_engineState->getMemoryAllocator());
  _fontImage->createImage(_engineState->getSettings()->getLoadTextureColorFormat(), {texWidth, texHeight}, 1, 1,
                          VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  const VkDeviceSize fontOffset = 0;
  const auto* fontBytes = static_cast<const std::byte*>(stagingBuffer->getAllocationInfo().pMappedData);
  _engineState->getResourceUploader()->upload(
      *_fontImage, std::span(fontBytes, static_cast<std::size_t>(stagingBuffer->getSize())), std::span(&fontOffset, 1));
  _engineState->getResourceUploader()->submit();

  _imageView = std::make_shared<RenderGraph::ImageView>(std::move(_fontImage), *_engineState->getDevice());
  _imageView->createImageView(VK_IMAGE_VIEW_TYPE_2D, 0, 0);
  auto fontSampler = std::make_shared<RenderGraph::Sampler>(*_engineState->getDevice());
  fontSampler->createSampler(VK_SAMPLER_ADDRESS_MODE_REPEAT, 1, _engineState->getSettings()->getAnisotropicSamples(),
                             VK_FILTER_LINEAR);
  _fontTexture = std::make_shared<RenderGraph::Texture>(_imageView, std::move(fontSampler));

  auto shader = std::make_shared<RenderGraph::Shader>(*_engineState->getDevice());
  shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                            "shaders/UI/ui_vertex.spv"));
  shader->add(_engineState->getFilesystem()->readFile<char>(_engineState->getSettings()->getShadersPath() +
                                                            "shaders/UI/ui_fragment.spv"));
  _descriptorSetLayout = std::make_shared<RenderGraph::DescriptorSetLayout>(*_engineState->getDevice());
  _descriptorSetLayout->createCustom(shader->getDescriptorSetLayoutBindings().at(0));

  _scaleTranslateBuffer.resize(_engineState->getSettings()->getMaxFramesInFlight());
  _descriptorSet.resize(_engineState->getSettings()->getMaxFramesInFlight());
  for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
    _descriptorSet[i] = std::make_shared<RenderGraph::DescriptorSet>(
        std::vector<RenderGraph::DescriptorSetLayout*>{(_descriptorSetLayout).get()},
        *_engineState->getDescriptorPool(), *_engineState->getDevice(), 0);
    _scaleTranslateBuffer[i] = std::make_shared<RenderGraph::Buffer>(
        sizeof(UniformDataGUI), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());

    _descriptorSet[i]->add({_scaleTranslateBuffer[i].get()});
    _descriptorSet[i]->add({_fontTexture.get()});
    _descriptorSet[i]->initialize(*commandBufferTransfer);
  }

  auto bindingDescriptor = VkVertexInputBindingDescription{.binding = 0,
                                                           .stride = sizeof(ImDrawVert),
                                                           .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
  // the order is different from built-in Mesh type, so need to handle it explicitily
  std::vector<VkVertexInputAttributeDescription> attributeDescriptor{
      {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(ImDrawVert, pos)},
      {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(ImDrawVert, uv)},
      {.location = 2, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(ImDrawVert, col)}};

  const VkPipelineVertexInputStateCreateInfo vertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &bindingDescriptor,
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptor.size()),
      .pVertexAttributeDescriptions = attributeDescriptor.data()};
  RenderGraph::PipelineGraphic pipelineGraphic;
  pipelineGraphic.setColorAttachments({_engineState->getSettings()->getSwapchainColorFormat()});
  pipelineGraphic.setDepthAttachment(std::nullopt);
  pipelineGraphic.setDepthTest(false);
  pipelineGraphic.setDepthWrite(false);
  pipelineGraphic.setAlphaBlending(true);
  std::vector<RenderGraph::DescriptorSetLayout*> descriptorSetLayouts{_descriptorSetLayout.get()};
  _pipeline = std::make_shared<RenderGraph::Pipeline>(*_engineState->getDevice());
  _pipeline->createGraphic(pipelineGraphic, shader->getShaderStageInfo(), descriptorSetLayouts, {}, vertexInput);
  ImGui::NewFrame();
}

bool GUI::drawListBox(std::vector<std::string> list, std::map<std::string, int*> variable, int displayedNumber) {
  bool result = false;

  for (auto& [key, value] : variable) {
    std::vector<const char*> listFormatted;
    for (auto& item : list) {
      listFormatted.push_back(item.c_str());
    }
    if (ImGui::ListBox(key.c_str(), value, listFormatted.data(), static_cast<int>(listFormatted.size()),
                       displayedNumber))
      result = true;
  }

  return result;
}

bool GUI::drawButton(std::string label, bool hideWindow) {
  bool result = false;

  ImGuiWindowFlags flags = 0;
  if (hideWindow) {
    ImGui::SetNextWindowBgAlpha(0.f);
    flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground;
  }
  if (ImGui::Button(label.c_str())) {
    result = true;
  }
  return result;
}

bool GUI::drawCheckbox(std::map<std::string, bool*> variable) {
  bool result = false;

  for (auto& [key, value] : variable) {
    if (ImGui::Checkbox(key.c_str(), value)) result = true;
  }

  return result;
}

bool GUI::drawSlider(std::map<std::string, float*> variable, std::map<std::string, std::tuple<float, float>> range) {
  bool result = false;
  for (auto& [key, value] : variable) {
    if (ImGui::SliderFloat(key.c_str(), value, std::get<0>(range[key]), std::get<1>(range[key]), "%.2f",
                           ImGuiSliderFlags_AlwaysClamp))
      result = true;
  }
  return result;
}

bool GUI::drawInputFloat(std::map<std::string, float*> variable) {
  bool result = false;
  for (auto& [key, value] : variable) {
    ImGui::PushItemWidth(100);
    if (ImGui::InputFloat(key.c_str(), value, 0.01f, 1.f, "%.2f")) result = true;
    ImGui::PopItemWidth();
  }

  return result;
}

bool GUI::drawInputText(std::string label, char* buffer, int size) {
  bool result = false;
  if (ImGui::InputText(label.c_str(), buffer, size, ImGuiInputTextFlags_EnterReturnsTrue)) result = true;

  return result;
}

bool GUI::drawInputInt(std::map<std::string, int*> variable) {
  bool result = false;
  for (auto& [key, value] : variable) {
    ImGui::PushItemWidth(100);
    if (ImGui::InputInt(key.c_str(), value, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) result = true;
    ImGui::PopItemWidth();
  }

  return result;
}

void GUI::drawText(std::vector<std::string> text) {
  for (auto value : text) {
    ImGui::Text("%s", value.c_str());
  }
}

void GUI::updateBuffers() {
  auto current = _engineState->getFrameInFlight();

  ImGui::Render();
  ImDrawData* imDrawData = ImGui::GetDrawData();

  // Note: Alignment is done inside buffer creation
  VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
  VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

  if ((vertexBufferSize == 0) || (indexBufferSize == 0)) {
    return;
  }

  if ((_vertexBuffer[current] == nullptr) || (_vertexCount[current] != imDrawData->TotalVtxCount)) {
    _vertexBuffer[current] = std::make_shared<RenderGraph::Buffer>(
        vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());
    _vertexCount[current] = imDrawData->TotalVtxCount;
  }

  // Index buffer
  if ((_indexBuffer[current] == nullptr) || (_indexCount[current] != imDrawData->TotalIdxCount)) {
    _indexBuffer[current] = std::make_shared<RenderGraph::Buffer>(
        indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        *_engineState->getMemoryAllocator());
    _indexCount[current] = imDrawData->TotalIdxCount;
  }

  // Upload data
  int vertexDst = 0;
  int indexDst = 0;
  for (int n = 0; n < imDrawData->CmdListsCount; n++) {
    const ImDrawList* cmdList = imDrawData->CmdLists[n];
    std::memcpy(static_cast<std::byte*>(_vertexBuffer[current]->getAllocationInfo().pMappedData) + (vertexDst),
                cmdList->VtxBuffer.Data, static_cast<std::size_t>(cmdList->VtxBuffer.Size * sizeof(ImDrawVert)));
    std::memcpy(static_cast<std::byte*>(_indexBuffer[current]->getAllocationInfo().pMappedData) + (indexDst),
                cmdList->IdxBuffer.Data, static_cast<std::size_t>(cmdList->IdxBuffer.Size * sizeof(ImDrawIdx)));
    vertexDst += cmdList->VtxBuffer.Size * sizeof(ImDrawVert);
    indexDst += cmdList->IdxBuffer.Size * sizeof(ImDrawIdx);
  }
}

void GUI::draw(int, const RenderGraph::CommandBuffer& commandBuffer) {
  const auto logger = _engineState->getLogger();
  logger->begin("Render GUI " + std::to_string(_engineState->getFrameInFlight()), &commandBuffer);
  updateBuffers();

  auto current = _engineState->getFrameInFlight();

  ImGuiIO& io = ImGui::GetIO();

  vkCmdBindPipeline(commandBuffer.getCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->getPipeline());

  VkViewport viewport{.x = 0.0f,
                      .y = 0.0f,
                      .width = static_cast<float>(std::get<0>(_resolution)),
                      .height = static_cast<float>(std::get<1>(_resolution)),
                      .minDepth = 0.0f,
                      .maxDepth = 1.0f};
  vkCmdSetViewport(commandBuffer.getCommandBuffer(), 0, 1, &viewport);

  // UI scale and translate via push constants
  UniformDataGUI uniformData{.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y),
                             .translate = glm::vec2(-1.0f)};
  std::memcpy(static_cast<std::byte*>(_scaleTranslateBuffer[current]->getAllocationInfo().pMappedData), &uniformData,
              static_cast<std::size_t>(_scaleTranslateBuffer[current]->getSize()));

  _descriptorSet[current]->bind(VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->getPipelineLayout(), commandBuffer);

  // Render commands
  ImDrawData* imDrawData = ImGui::GetDrawData();
  int32_t vertexOffset = 0;
  int32_t indexOffset = 0;

  if (imDrawData->CmdListsCount > 0) {
    VkDeviceSize offsets[1] = {0};
    const auto vertexBuffer = _vertexBuffer[current]->getBuffer();
    vkCmdBindVertexBuffers(commandBuffer.getCommandBuffer(), 0, 1, &vertexBuffer, offsets);
    vkCmdBindIndexBuffer(commandBuffer.getCommandBuffer(), _indexBuffer[current]->getBuffer(), 0, VK_INDEX_TYPE_UINT16);

    for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
      const ImDrawList* cmdList = imDrawData->CmdLists[i];
      for (int32_t j = 0; j < cmdList->CmdBuffer.Size; j++) {
        const ImDrawCmd* pcmd = &cmdList->CmdBuffer[j];
        VkRect2D scissorRect{
            .offset = {.x = std::max((int32_t)(pcmd->ClipRect.x), 0), .y = std::max((int32_t)(pcmd->ClipRect.y), 0)},
            .extent = {.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x),
                       .height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y)}};
        vkCmdSetScissor(commandBuffer.getCommandBuffer(), 0, 1, &scissorRect);
        vkCmdDrawIndexed(commandBuffer.getCommandBuffer(), pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
        indexOffset += pcmd->ElemCount;
      }
      vertexOffset += cmdList->VtxBuffer.Size;
    }
  }

  ImGui::NewFrame();
  logger->end(&commandBuffer);
}

void GUI::startWindow(std::string name, float fontScale) {
  ImGui::Begin(name.c_str(), 0, ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::GetFont()->Scale = fontScale;
  ImGui::PushFont(ImGui::GetFont());
}

void GUI::setWindowSize(std::tuple<int, int> size) {
  ImGui::SetWindowSize(ImVec2(static_cast<float>(std::get<0>(size)), static_cast<float>(std::get<1>(size))));
}

void GUI::setWindowPosition(std::tuple<int, int> position) {
  ImGui::SetWindowPos(ImVec2(static_cast<float>(std::get<0>(position)), static_cast<float>(std::get<1>(position))));
}

std::tuple<int, int> GUI::getWindowSize() {
  const auto size = ImGui::GetWindowSize();
  return {static_cast<int>(size.x), static_cast<int>(size.y)};
}

std::tuple<int, int> GUI::getWindowPosition() {
  const auto position = ImGui::GetWindowPos();
  return {static_cast<int>(position.x), static_cast<int>(position.y)};
}

void GUI::endWindow() {
  ImGui::PopFont();
  ImGui::End();
}

bool GUI::startTree(std::string name, bool open) {
  ImGui::SetNextItemOpen(open, ImGuiCond_FirstUseEver);
  return ImGui::TreeNode(name.c_str());
}

void GUI::endTree() { ImGui::TreePop(); }

GUI::~GUI() { ImGui::DestroyContext(); }

bool GUI::cursorNotify(float xPos, float yPos) {
  ImGuiIO& io = ImGui::GetIO();
  io.MousePos = ImVec2(xPos, yPos);

  if (io.WantCaptureMouse) {
    return true;
  }

  return false;
}

bool GUI::mouseNotify(int button, int action, int) {
  ImGuiIO& io = ImGui::GetIO();
#ifdef __ANDROID__
  if (action == 1) io.MouseDown[0] = true;
  if (action == 0) io.MouseDown[0] = false;
#else
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    io.MouseDown[0] = true;
  }
  if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
    io.MouseDown[1] = true;
  }
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
    io.MouseDown[0] = false;
  }
  if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
    io.MouseDown[1] = false;
  }
#endif
  if (io.WantCaptureMouse) {
    return true;
  }

  return false;
}

bool GUI::charNotify(unsigned int code) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddInputCharacter(code);

  if (io.WantCaptureKeyboard) {
    return true;
  }

  return false;
}

bool GUI::scrollNotify(double xOffset, double yOffset) {
  ImGuiIO& io = ImGui::GetIO();
  io.MouseWheelH += static_cast<float>(xOffset);
  io.MouseWheel += static_cast<float>(yOffset);

  if (io.WantCaptureMouse) {
    return true;
  }

  return false;
}

bool GUI::keyNotify(int key, int, int action, int) {
#ifndef __ANDROID__
  ImGuiIO& io = ImGui::GetIO();
  if (key == GLFW_KEY_BACKSPACE && action == GLFW_PRESS) {
    io.AddKeyEvent(ImGuiKey_Backspace, true);
    io.AddKeyEvent(ImGuiKey_Backspace, false);
  }
  if (key == GLFW_KEY_DELETE && action == GLFW_PRESS) {
    io.AddKeyEvent(ImGuiKey_Delete, true);
    io.AddKeyEvent(ImGuiKey_Delete, false);
  }
  if (key == GLFW_KEY_LEFT && action == GLFW_PRESS) {
    io.AddKeyEvent(ImGuiKey_LeftArrow, true);
    io.AddKeyEvent(ImGuiKey_LeftArrow, false);
  }
  if (key == GLFW_KEY_RIGHT && action == GLFW_PRESS) {
    io.AddKeyEvent(ImGuiKey_RightArrow, true);
    io.AddKeyEvent(ImGuiKey_RightArrow, false);
  }
  if (key == GLFW_KEY_ENTER && action == GLFW_PRESS) {
    io.AddKeyEvent(ImGuiKey_Enter, true);
    io.AddKeyEvent(ImGuiKey_Enter, false);
  }
  if (key == GLFW_KEY_LEFT_CONTROL && action == GLFW_PRESS) {
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    io.AddKeyEvent(ImGuiKey_ModCtrl, true);
  }
  if (key == GLFW_KEY_LEFT_CONTROL && action == GLFW_RELEASE) {
    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    io.AddKeyEvent(ImGuiKey_ModCtrl, false);
  }

  if (io.WantCaptureKeyboard) {
    return true;
  }
#endif

  return false;
}

void GUI::update(int, const RenderGraph::CommandBuffer&) {}

void GUI::reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>&) {}
