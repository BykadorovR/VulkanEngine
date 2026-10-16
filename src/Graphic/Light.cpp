#include "Graphic/Light.h"

int PointLight::getSize() { return sizeof(LightFields); }

PointLight::PointLight(std::shared_ptr<EngineState> engineState) {
  _light = std::make_shared<LightFields>();
  _engineState = engineState;
  _camera = std::make_shared<CameraPointLight>();

  setAttenuationIndex(_attenuationIndex);
}

void* PointLight::getData() {
  _light->position = _camera->getPosition();
  _light->far = _camera->getFar();
  return _light.get();
}

void PointLight::setColor(glm::vec3 color) { _light->color = color; }

glm::vec3 PointLight::getColor() { return _light->color; }

std::shared_ptr<CameraPointLight> PointLight::getCamera() { return _camera; }

int PointLight::getAttenuationIndex() { return _attenuationIndex; }

void PointLight::setAttenuationIndex(int index) {
  _attenuationIndex = index;
  _light->distance = std::get<0>(_engineState->getSettings()->getAttenuations()[index]);
  _light->quadratic = std::get<1>(_engineState->getSettings()->getAttenuations()[index]);
}

int PointLight::getDistance() { return _light->distance; }

DirectionalLight::DirectionalLight() {
  _light = std::make_shared<LightFields>();
  _camera = std::make_shared<CameraDirectionalLight>();
}

int DirectionalLight::getSize() { return sizeof(LightFields); }

void* DirectionalLight::getData() {
  _light->position = _camera->getPosition();
  return _light.get();
}

void DirectionalLight::setColor(glm::vec3 color) { _light->color = color; }

glm::vec3 DirectionalLight::getColor() { return _light->color; }

std::shared_ptr<CameraDirectionalLight> DirectionalLight::getCamera() { return _camera; }

AmbientLight::AmbientLight() { _light = std::make_shared<LightFields>(); }

void AmbientLight::setColor(glm::vec3 color) { _light->color = color; }

int AmbientLight::getSize() { return sizeof(LightFields); }

void* AmbientLight::getData() { return _light.get(); }
