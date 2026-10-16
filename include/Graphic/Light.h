#pragma once
#include <memory>
#include <vector>
#include "Primitive/Cubemap.h"
#include "Utility/Settings.h"
#include "Graphic/Camera.h"
#include "Utility/Logger.h"

enum class LightType { DIRECTIONAL = 0, POINT = 1, AMBIENT = 2 };
enum class ShadowAlgorithm { PCF = 0, VSM = 1 };

class AmbientLight {
 private:
  struct LightFields {
    alignas(16) glm::vec3 color;
  };
  std::shared_ptr<LightFields> _light = nullptr;

 public:
  AmbientLight();
  void setColor(glm::vec3 color);
  int getSize();
  void* getData();
};

class DirectionalLight {
 private:
  struct LightFields {
    alignas(16) glm::vec3 color;
    alignas(16) glm::vec3 position;
  };
  std::shared_ptr<LightFields> _light = nullptr;
  std::shared_ptr<CameraDirectionalLight> _camera;

 public:
  DirectionalLight();
  void setColor(glm::vec3 color);
  glm::vec3 getColor();
  std::shared_ptr<CameraDirectionalLight> getCamera();
  int getSize();
  void* getData();
};

class PointLight {
 private:
  struct LightFields {
    // attenuation
    float quadratic = 0.f;
    int distance;
    // parameters
    float far;
    // if alignment changed need to change implementation, sizeof(glm::vec4) is used in LightManager
    alignas(16) glm::vec3 color;
    alignas(16) glm::vec3 position;
  };
  std::shared_ptr<EngineState> _engineState;
  std::shared_ptr<LightFields> _light = nullptr;
  int _attenuationIndex = 4;
  std::shared_ptr<CameraPointLight> _camera;

 public:
  PointLight(std::shared_ptr<EngineState> engineState);
  void setColor(glm::vec3 color);
  glm::vec3 getColor();
  std::shared_ptr<CameraPointLight> getCamera();
  void setAttenuationIndex(int index);
  int getAttenuationIndex();
  int getDistance();
  int getSize();
  void* getData();
};
