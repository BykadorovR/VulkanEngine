#pragma once
#include <optional>
#include <utility>
#include "Camera.h"
#include "Primitive/Drawable.h"
#undef near
#undef far

class CameraRTS : public CameraPerspective, public InputSubscriber {
 private:
  std::shared_ptr<Drawable> _object;
  std::shared_ptr<EngineState> _engineState;
  glm::vec3 _shift = glm::vec3(0.f, 10.f, 0.f);
  int _threshold = 100;
  float _moveSpeed = 0.5f;
  std::pair<float, float> _offset;
  std::optional<glm::vec3> _zoomPoint;

 public:
  CameraRTS(std::shared_ptr<Drawable> object, std::shared_ptr<EngineState> engineState);
  void update() override;
  void setShift(glm::vec3 shift);
  void setThreshold(int threshold);
  void setSpeed(float speed);

  void cursorNotify(float xPos, float yPos) override;
  void mouseNotify(int button, int action, int mods) override;
  void keyNotify(int key, int scancode, int action, int mods) override;
  void charNotify(unsigned int code) override;
  void scrollNotify(double xOffset, double yOffset) override;
};
