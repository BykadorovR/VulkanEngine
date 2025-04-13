#pragma once
#include "Engine/Core.h"
#include "Primitive/Shape3D.h"
#include "Primitive/Terrain.h"
#include "Graphic/CameraRTS.h"
#include <glm/glm.hpp>

class InputHandler : public InputSubscriber {
 private:
  std::function<void(glm::vec2)> _callbackMove;
  std::shared_ptr<Core> _core;
  glm::vec2 _position;

 public:
  InputHandler(std::shared_ptr<Core> core);
  void setMoveCallback(std::function<void(glm::vec2)> callback);
  void cursorNotify(float xPos, float yPos) override;
  void mouseNotify(int button, int action, int mods) override;
  void keyNotify(int key, int scancode, int action, int mods) override;
  void charNotify(unsigned int code) override;
  void scrollNotify(double xOffset, double yOffset) override;
};

class Main {
 private:
  std::shared_ptr<Core> _core;
  std::shared_ptr<GUI> _gui;
  std::shared_ptr<CameraFly> _cameraFly;
  std::shared_ptr<CameraRTS> _cameraRTS;
  std::shared_ptr<InputHandler> _inputHandler;

  std::shared_ptr<PointLight> _pointLightVertical, _pointLightHorizontal;
  std::shared_ptr<DirectionalLight> _directionalLight;
  std::shared_ptr<Shape3D> _cubeColoredLightVertical, _cubeColoredLightHorizontal, _cubePlayer, _boundingBox, _capsule;
  std::shared_ptr<Model3D> _modelSimple;
  std::shared_ptr<PhysicsManager> _physicsManager;
  std::shared_ptr<TerrainPhysics> _terrainPhysics;
  std::shared_ptr<Shape3DPhysics> _shape3DPhysics;
  std::shared_ptr<Model3DPhysics> _model3DPhysics;
  std::shared_ptr<MaterialColor> _materialColor;
  std::shared_ptr<TerrainGPU> _terrain;
  std::shared_ptr<TerrainCPU> _terrainCPU;
  bool _showGPU = true, _showCPU = true;
  int _patchX = 12, _patchY = 12;
  float _heightScale = 64.f;
  float _heightShift = 16.f;
  std::array<float, 4> _heightLevels = {16, 128, 192, 256};
  int _minTessellationLevel = 4, _maxTessellationLevel = 4;
  float _minDistance = 30, _maxDistance = 100;
  int _cameraIndex = 0;
  glm::vec3 _terrainPosition = glm::vec3(0.f, 0.f, 0.f);
  glm::vec3 _terrainScale = glm::vec3(1.f);
  void _createTerrainColor();
  float _speed = 3.f;
  glm::vec3 _fading = {0.f, 0.f, 0.f};
  std::optional<glm::vec3> _endPoint;
  std::optional<float> _stripeLeft, _stripeRight, _stripeTop, _stripeBot;
  std::vector<int> _patchTextures;
  std::vector<int> _patchRotationsIndex;
  glm::vec3 _direction = {0.f, 0.f, 1.f};
  float _angle = 0.f;
  float _modelScale = 2.f;
  float _boundingBoxScale = 1.5f;
  glm::quat _rotation = glm::identity<glm::quat>();

  void _loadTerrain(std::string path);

 public:
  Main();
  void update();
  void reset(int width, int height);
  void start();
};