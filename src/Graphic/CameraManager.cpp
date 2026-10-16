#include "Graphic/CameraManager.h"

void CameraManager::addCamera(std::string name, std::shared_ptr<Camera> camera) { _cameras[name] = camera; }

std::shared_ptr<Camera> CameraManager::getCamera(std::string name) { return _cameras[name]; }

void CameraManager::setCurrentCamera(std::shared_ptr<Camera> camera) { _currentCamera = camera; }

std::shared_ptr<Camera> CameraManager::getCurrentCamera() { return _currentCamera; }

void CameraManager::update() {
  if (_currentCamera == nullptr) throw std::runtime_error("No current camera was specified");
  _currentCamera->update();
}
