#pragma once
#include <windows.h>
#undef near
#undef far
#include "Core.h"
#include "Shape3D.h"

class InputHandler : public InputSubscriber {
 private:
  bool _cursorEnabled = false;

 public:
  void cursorNotify(GLFWwindow* window, float xPos, float yPos) override;
  void mouseNotify(GLFWwindow* window, int button, int action, int mods) override;
  void keyNotify(GLFWwindow* window, int key, int scancode, int action, int mods) override;
  void charNotify(GLFWwindow* window, unsigned int code) override;
  void scrollNotify(GLFWwindow* window, double xOffset, double yOffset) override;
};

class Main {
 private:
  std::shared_ptr<Core> _core;
  std::shared_ptr<CameraFly> _camera;
  std::shared_ptr<InputHandler> _inputHandler;

  std::shared_ptr<PointLight> _pointLightVertical, _pointLightHorizontal;
  std::shared_ptr<DirectionalLight> _directionalLight;
  std::shared_ptr<Shape3D> _cubeColoredLightVertical, _cubeColoredLightHorizontal;
  std::shared_ptr<Animation> _animationFish;

 public:
  Main();
  void update();
  void reset(int width, int height);
  void start();
};