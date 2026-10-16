#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "Graphic/Camera.h"

CameraDirectionalLight::CameraDirectionalLight() {
  _eye = glm::vec3(0.f, 15.f, 0.f);
  _direction = glm::vec3(0.f, -1.f, 0.f);
  _up = glm::vec3(0.f, 0.f, -1.f);

  _rect = {-10.f, 10.f, -10.f, 10.f};
  _near = 0.1f;
  _far = 20.f;
}

void CameraDirectionalLight::setPosition(glm::vec3 position) { _eye = position; }

void CameraDirectionalLight::setArea(std::array<float, 4> rect, float near, float far) {
  _rect = rect;
  _near = near;
  _far = far;
}

float CameraDirectionalLight::getNear() { return _near; }

float CameraDirectionalLight::getFar() { return _far; }

glm::vec3 CameraDirectionalLight::getPosition() { return _eye; }

glm::mat4 CameraDirectionalLight::getView() { return glm::lookAt(_eye, _eye + _direction, _up); }

glm::mat4 CameraDirectionalLight::getProjection() {
  return glm::ortho(_rect[0], _rect[1], _rect[2], _rect[3], _near, _far);
}

CameraPointLight::CameraPointLight() {
  _eye = glm::vec3(0.f, 15.f, 0.f);
  // up is inverted for X and Z because of some specific cubemap Y coordinate stuff
  _direction = {glm::vec3(1.0, 0.0, 0.0),  glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, 1.0, 0.0),
                glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, 1.0),  glm::vec3(0.0, 0.0, -1.0)};
  _up = {glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, 1.0),
         glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, -1.0, 0.0)};

  _near = 0.1f;
  _far = 40.f;
}

void CameraPointLight::setPosition(glm::vec3 position) { _eye = position; }

void CameraPointLight::setArea(float near, float far) {
  _near = near;
  _far = far;
}

float CameraPointLight::getNear() { return _near; }

float CameraPointLight::getFar() { return _far; }

glm::vec3 CameraPointLight::getPosition() { return _eye; }

glm::mat4 CameraPointLight::getView(int face) {
  auto viewMatrix = glm::lookAt(_eye, _eye + _direction[face], _up[face]);
  return viewMatrix;
}

glm::mat4 CameraPointLight::getProjection() {
  float aspect = 1.f;
  return glm::perspective(glm::radians(90.f), aspect, 0.1f, _far);
}

Camera::Camera() {
  _eye = glm::vec3(0.f, 0.f, 3.f);
  _direction = glm::vec3(0.f, 0.f, -1.f);
  _up = glm::vec3(0.f, 1.f, 0.f);
  _far = 100.f;
  _near = 0.1f;
}

void Camera::setViewParameters(glm::vec3 eye, glm::vec3 direction, glm::vec3 up) {
  _eye = eye;
  _direction = direction;
  _up = up;
}

glm::vec3 Camera::getEye() { return _eye; }

glm::vec3 Camera::getDirection() { return _direction; }

glm::vec3 Camera::getUp() { return _up; }

float Camera::getFar() { return _far; }

float Camera::getNear() { return _near; }

void CameraOrtho::update() { ; }

void CameraOrtho::setProjectionParameters(std::array<float, 4> rect, float near, float far) {
  _rect = rect;
  _near = near;
  _far = far;
}

glm::mat4 CameraOrtho::getProjection() {
  auto projection = glm::ortho(_rect[0], _rect[1], _rect[2], _rect[3], _near, _far);
  return projection;
}

glm::mat4 CameraOrtho::getView() { return glm::lookAt(_eye, _eye + _direction, _up); }

CameraPerspective::CameraPerspective(std::shared_ptr<EngineState> engineState) {
  _yaw = -90.f;
  _pitch = 0.f;
  _roll = 0.f;
  _fov = 60.f;
  _aspect = (static_cast<float>(std::get<0>(engineState->getSettings()->getResolution())) /
             static_cast<float>(std::get<1>(engineState->getSettings()->getResolution())));
  _direction.x = cos(glm::radians(_yaw)) * cos(glm::radians(_pitch));
  _direction.y = sin(glm::radians(_pitch));
  _direction.z = sin(glm::radians(_yaw)) * cos(glm::radians(_pitch));
  _direction = glm::normalize(_direction);
}

void CameraPerspective::update() { ; }

void CameraPerspective::setAspect(float aspect) { _aspect = aspect; }

glm::vec3 CameraPerspective::getAngles() { return glm::vec3(_yaw, _pitch, _roll); }

void CameraPerspective::setAngles(float yaw, float pitch, float roll) {
  _yaw = yaw;
  _pitch = pitch;
  _roll = roll;
}

void CameraPerspective::setProjectionParameters(float fov, float near, float far) {
  _fov = fov;
  _near = near;
  _far = far;
}

glm::mat4 CameraPerspective::getView() { return glm::lookAt(_eye, _eye + _direction, _up); }

glm::mat4 CameraPerspective::getProjection() {
  auto projection = glm::perspective(glm::radians(_fov), _aspect, _near, _far);
  return projection;
}

float CameraPerspective::getFOV() { return _fov; }

CameraFly::CameraFly(std::shared_ptr<EngineState> engineState) : CameraPerspective(engineState) {
  _engineState = engineState;
  _once = false;
  _xLast = 0.f;
  _yLast = 0.f;
}

void CameraFly::update() {
  _direction.x = cos(glm::radians(_yaw)) * cos(glm::radians(_pitch));
  _direction.y = sin(glm::radians(_pitch));
  _direction.z = sin(glm::radians(_yaw)) * cos(glm::radians(_pitch));
  _direction = glm::normalize(_direction);

#ifdef __ANDROID__
  if (_keyStatus[87]) {
    _eye += _moveSpeed * _direction;
  }

  if (_keyStatus[83]) {
    _eye -= _moveSpeed * _direction;
  }
#else
  if (_keyStatus[GLFW_KEY_W]) {
    _eye += _moveSpeed * _direction;
  }

  if (_keyStatus[GLFW_KEY_S]) {
    _eye -= _moveSpeed * _direction;
  }

  if (_keyStatus[GLFW_KEY_A]) {
    _eye -= _moveSpeed * glm::normalize(glm::cross(_direction, _up));
  }

  if (_keyStatus[GLFW_KEY_D]) {
    _eye += _moveSpeed * glm::normalize(glm::cross(_direction, _up));
  }

  if (_keyStatus[GLFW_KEY_SPACE]) {
    _eye += _moveSpeed * _up;
  }
#endif
}

void CameraFly::setViewParameters(glm::vec3 eye, glm::vec3 direction, glm::vec3 up) {
  _once = false;
  CameraPerspective::setViewParameters(eye, direction, up);
}

void CameraFly::setSpeed(float rotate, float translate) {
  _sensitivity = rotate;
  _moveSpeed = translate;
}

void CameraFly::cursorNotify(float xPos, float yPos) {
#ifndef __ANDROID__
  if (glfwGetInputMode((GLFWwindow*)(_engineState->getWindow()->getWindow()), GLFW_CURSOR) != GLFW_CURSOR_DISABLED) {
    _once = false;
  }
#endif

  if (_once == false) {
    _xLast = xPos;
    _yLast = yPos;
    _once = true;
  }

  float xOffset = xPos - _xLast;
  float yOffset = _yLast - yPos;
  _xLast = xPos;
  _yLast = yPos;

  xOffset *= _sensitivity;
  yOffset *= _sensitivity;

  _yaw += xOffset;
  _pitch += yOffset;

  if (_pitch > 89.0f) _pitch = 89.0f;
  if (_pitch < -89.0f) _pitch = -89.0f;
}

void CameraFly::mouseNotify(int, int action, int) {
#ifdef __ANDROID__
  if (action == 0) _once = false;
#else
  static_cast<void>(action);
#endif
}

void CameraFly::keyNotify(int key, int, int action, int) {
#ifdef __ANDROID__
  if (action == 1) _keyStatus[key] = true;
  if (action == 0) _keyStatus[key] = false;
#else
  if (action == GLFW_PRESS) _keyStatus[key] = true;
  if (action == GLFW_RELEASE) _keyStatus[key] = false;
#endif
}

void CameraFly::charNotify(unsigned int) {}

void CameraFly::scrollNotify(double, double) {}
