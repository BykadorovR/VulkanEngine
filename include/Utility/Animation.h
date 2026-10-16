#pragma once
#include "Utility/Loader.h"
#include "Utility/Logger.h"
#include "Utility/EngineState.h"

class Animation {
 private:
  std::vector<std::shared_ptr<SkinGLTF>> _skins;
  std::vector<std::shared_ptr<AnimationGLTF>> _animations;
  std::vector<std::shared_ptr<NodeGLTF>> _nodes;
  std::shared_ptr<EngineState> _engineState;
  // separate descriptor for each skin
  std::vector<std::vector<std::shared_ptr<RenderGraph::Buffer>>> _ssboJoints;
  int _animationIndex = 0;
  std::map<int, glm::mat4> _matricesGlobal;
  // needed for blending between different animations
  std::map<std::shared_ptr<NodeGLTF>, glm::vec3> _translationPrevious, _scalePrevious;
  std::map<std::shared_ptr<NodeGLTF>, glm::quat> _rotationPrevious;
  int _transitionFrames = 30;
  int _transitionFramesCounter = _transitionFrames + 1;

  std::vector<glm::mat4> _matricesJoint;
  bool _play = true;
  std::mutex _mutex;

  void _updateJoints(int currentImage, std::shared_ptr<NodeGLTF> node);
  void _fillMatricesJoint(std::shared_ptr<NodeGLTF> node, glm::mat4 matrixParent);

 public:
  Animation(const std::vector<std::shared_ptr<NodeGLTF>>& nodes,
            const std::vector<std::shared_ptr<SkinGLTF>>& skins,
            const std::vector<std::shared_ptr<AnimationGLTF>>& animations,
            std::shared_ptr<EngineState> engineState);
  std::vector<std::string> getAnimations();
  void setAnimation(std::string name, bool smoothTransition = true);
  std::string getAnimation();
  // set number of frames for transition between animations
  void setTransitionFrames(int transitionFrames);
  void setPlay(bool play);
  void setTime(float time);
  std::tuple<float, float> getTimeRange();
  std::vector<glm::mat4> getJointMatrices();
  std::map<int, glm::mat4> getGlobalMatrices();

  std::tuple<float, float> getTimeline();
  float getCurrentTime();

  void calculateJoints(float deltaTime);
  void updateBuffers(int currentImage);

  std::vector<std::vector<std::shared_ptr<RenderGraph::Buffer>>> getGlobalMatricesBuffer();
};