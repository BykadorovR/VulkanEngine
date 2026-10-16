#pragma once
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
#include <imgui.h>
#include "Utility/Input.h"
#include "Utility/EngineState.h"

import Buffer;
import Command;
import DescriptorBuffer;
import Graph;
import Pipeline;
import Shader;
import Texture;

struct UniformDataGUI {
  glm::vec2 scale;
  glm::vec2 translate;
};

class GUI : public InputSubscriberExclusive, public RenderGraph::GraphElement {
 private:
  std::tuple<int, int> _resolution;
  std::shared_ptr<EngineState> _engineState;
  std::unique_ptr<RenderGraph::Image> _fontImage;
  std::shared_ptr<RenderGraph::Pipeline> _pipeline;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _vertexBuffer;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _indexBuffer;
  std::vector<std::shared_ptr<RenderGraph::Buffer>> _scaleTranslateBuffer;
  std::vector<std::shared_ptr<RenderGraph::DescriptorSet>> _descriptorSet;
  std::shared_ptr<RenderGraph::DescriptorSetLayout> _descriptorSetLayout;
  std::shared_ptr<RenderGraph::Texture> _fontTexture;
  std::shared_ptr<RenderGraph::ImageView> _imageView;
  std::vector<int32_t> _vertexCount;
  std::vector<int32_t> _indexCount;

 public:
  GUI(std::shared_ptr<EngineState> engineState);
  void reset();
  void initialize(const RenderGraph::CommandBuffer* commandBufferTransfer);
  void startWindow(std::string name, float fontScale = 1.f);
  std::tuple<int, int> getWindowSize();
  std::tuple<int, int> getWindowPosition();
  void setWindowSize(std::tuple<int, int> size);
  void setWindowPosition(std::tuple<int, int> position);
  void endWindow();
  bool startTree(std::string name, bool open = true);
  void endTree();
  void drawText(std::vector<std::string> text);
  bool drawSlider(std::map<std::string, float*> variable, std::map<std::string, std::tuple<float, float>> range);
  bool drawButton(std::string label, bool hideWindow = false);
  bool drawCheckbox(std::map<std::string, bool*> variable);
  bool drawListBox(std::vector<std::string> list, std::map<std::string, int*> variable, int displayedNumber);
  bool drawInputFloat(std::map<std::string, float*> variable);
  bool drawInputInt(std::map<std::string, int*> variable);
  bool drawInputText(std::string label, char* buffer, int size);
  void updateBuffers();
  void draw(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void update(int currentFrame, const RenderGraph::CommandBuffer& commandBuffer) override;
  void reset(const std::vector<std::shared_ptr<RenderGraph::ImageView>>& swapchain) override;
  bool cursorNotify(float xPos, float yPos) override;
  bool mouseNotify(int button, int action, int mods) override;
  bool keyNotify(int key, int scancode, int action, int mods) override;
  bool charNotify(unsigned int code) override;
  bool scrollNotify(double xOffset, double yOffset) override;
  ~GUI();
};
