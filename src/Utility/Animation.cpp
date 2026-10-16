#include <cstring>
#include "Utility/Animation.h"

Animation::Animation(const std::vector<std::shared_ptr<NodeGLTF>>& nodes,
                     const std::vector<std::shared_ptr<SkinGLTF>>& skins,
                     const std::vector<std::shared_ptr<AnimationGLTF>>& animations,
                     std::shared_ptr<EngineState> engineState) {
  _nodes = nodes;
  _skins = skins;
  _animations = animations;
  _engineState = engineState;

  _ssboJoints.resize(_skins.size());
  for (int i = 0; i < _skins.size(); i++) {
    _ssboJoints[i].resize(_engineState->getSettings()->getMaxFramesInFlight());
    for (int j = 0; j < _engineState->getSettings()->getMaxFramesInFlight(); j++) {
      _ssboJoints[i][j] = std::make_shared<RenderGraph::Buffer>(
          sizeof(glm::vec4) + _skins[i]->inverseBindMatrices.size() * sizeof(glm::mat4),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
          *_engineState->getMemoryAllocator());
    }
  }

  // store default descriptor set in 0 index and 0 SSBO
  if (_skins.size() == 0) {
    std::vector<std::shared_ptr<RenderGraph::Buffer>> bufferStubs;
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      bufferStubs.push_back(std::make_shared<RenderGraph::Buffer>(
          sizeof(glm::mat4) + sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
          *_engineState->getMemoryAllocator()));
    }
    _ssboJoints.push_back(bufferStubs);
    auto identityMat = glm::mat4(1.f);
    int jointNumber = 0;
    for (int i = 0; i < _engineState->getSettings()->getMaxFramesInFlight(); i++) {
      std::memcpy(static_cast<std::byte*>(_ssboJoints[0][i]->getAllocationInfo().pMappedData), &jointNumber,
                  static_cast<std::size_t>(sizeof(glm::vec4)));
      std::memcpy(static_cast<std::byte*>(_ssboJoints[0][i]->getAllocationInfo().pMappedData) + (sizeof(glm::vec4)),
                  &identityMat, static_cast<std::size_t>(sizeof(glm::mat4)));
    }
  }
}

std::vector<std::vector<std::shared_ptr<RenderGraph::Buffer>>> Animation::getGlobalMatricesBuffer() {
  return _ssboJoints;
}

void Animation::_updateJoints(int currentImage, std::shared_ptr<NodeGLTF> node) {
  if (node->skin > -1 && !_matricesGlobal.empty()) {
    // Update the joint matrices
    glm::mat4 inverseTransform = glm::inverse(_matricesGlobal[node->index]);
    std::shared_ptr<SkinGLTF> skin = _skins[node->skin];
    _matricesJoint.resize(skin->joints.size());
    for (size_t i = 0; i < _matricesJoint.size(); i++) {
      _matricesJoint[i] = inverseTransform * (_matricesGlobal[skin->joints[i]->index] * skin->inverseBindMatrices[i]);
    }
    auto frameInFlight = currentImage % _engineState->getSettings()->getMaxFramesInFlight();
    const int jointNumber = static_cast<int>(_matricesJoint.size());
    std::memcpy(static_cast<std::byte*>(_ssboJoints[node->skin][frameInFlight]->getAllocationInfo().pMappedData),
                &jointNumber, static_cast<std::size_t>(sizeof(glm::vec4)));
    std::memcpy(static_cast<std::byte*>(_ssboJoints[node->skin][frameInFlight]->getAllocationInfo().pMappedData) +
                    (sizeof(glm::vec4)),
                _matricesJoint.data(), static_cast<std::size_t>(_matricesJoint.size() * sizeof(glm::mat4)));
  }

  for (auto& child : node->children) {
    _updateJoints(currentImage, child);
  }
}

void Animation::_fillMatricesJoint(std::shared_ptr<NodeGLTF> node, glm::mat4 matrixParent) {
  _matricesGlobal[node->index] = matrixParent * node->getLocalMatrix();
  for (auto& child : node->children) {
    _fillMatricesJoint(child, _matricesGlobal[node->index]);
  }
}

std::vector<std::string> Animation::getAnimations() {
  std::vector<std::string> names(_animations.size());
  for (int i = 0; i < _animations.size(); i++) {
    names[i] = _animations[i]->name;
  }
  return names;
}

std::vector<glm::mat4> Animation::getJointMatrices() { return _matricesJoint; }

std::map<int, glm::mat4> Animation::getGlobalMatrices() { return _matricesGlobal; }

void Animation::setAnimation(std::string name, bool smoothTransition) {
  std::unique_lock<std::mutex> lock(_mutex);
  for (int i = 0; i < _animations.size(); i++) {
    if (_animations[i]->name == name) {
      if (_animationIndex != i) {
        _animationIndex = i;
        if (smoothTransition) {
          _transitionFramesCounter = 0;
          _translationPrevious.clear();
          _scalePrevious.clear();
          _rotationPrevious.clear();
          for (const auto& channel : _animations[_animationIndex]->channels) {
            _translationPrevious[channel.node] = channel.node->translation;
            _scalePrevious[channel.node] = channel.node->scale;
            _rotationPrevious[channel.node] = channel.node->rotation;
          }
        }
      }
      break;
    }
  }
}

std::string Animation::getAnimation() {
  std::unique_lock<std::mutex> lock(_mutex);
  return _animations[_animationIndex]->name;
}

void Animation::setTransitionFrames(int transitionFrames) {
  _transitionFrames = transitionFrames;
  _transitionFramesCounter = transitionFrames + 1;
}

void Animation::setPlay(bool play) {
  std::unique_lock<std::mutex> lock(_mutex);
  _play = play;
}

std::tuple<float, float> Animation::getTimeline() {
  return {_animations[_animationIndex]->start, _animations[_animationIndex]->end};
}

void Animation::setTime(float time) {
  std::unique_lock<std::mutex> lock(_mutex);
  _animations[_animationIndex]->currentTime = time;
}

std::tuple<float, float> Animation::getTimeRange() {
  return {0.0f, _animations[_animationIndex]->end - _animations[_animationIndex]->start};
}

// TODO: mutex?
float Animation::getCurrentTime() { return _animations[_animationIndex]->currentTime; }

void Animation::calculateJoints(float deltaTime) {
  std::unique_lock<std::mutex> lock(_mutex);
  if (!_play || _animations.empty() || _animationIndex >= _animations.size()) {
    return;
  }
  auto logger = _engineState->getLogger();

  logger->begin("Update translate/scale/rotation");
  std::shared_ptr<AnimationGLTF> animation = _animations[_animationIndex];
  animation->currentTime += deltaTime;
  animation->currentTime = fmod(animation->currentTime, animation->end - animation->start);
  for (auto& channel : animation->channels) {
    AnimationSamplerGLTF& sampler = animation->samplers[channel.samplerIndex];
    if (sampler.interpolation == "LINEAR") {
      if (sampler.inputs.size() <= 1) continue;
      auto higher = std::find_if(sampler.inputs.begin(), sampler.inputs.end(),
                                 [current = animation->currentTime, start = animation->start](float value) {
                                   return value > (current + start);
                                 });
      // Get the input keyframe values for the current time stamp
      if (higher != sampler.inputs.begin()) {
        if (higher == sampler.inputs.end() && (animation->currentTime + animation->start) > sampler.inputs.back())
          continue;
        const auto right = static_cast<std::size_t>(std::distance(sampler.inputs.begin(), higher));
        const auto left = right - 1;
        const float a = (animation->currentTime + animation->start - sampler.inputs[left]) /
                        (sampler.inputs[right] - sampler.inputs[left]);
        if (channel.path == "translation") {
          channel.node->translation = glm::mix(sampler.outputsVec4[left], sampler.outputsVec4[right], a);
        }
        if (channel.path == "rotation") {
          glm::quat q1;
          q1.x = sampler.outputsVec4[left].x;
          q1.y = sampler.outputsVec4[left].y;
          q1.z = sampler.outputsVec4[left].z;
          q1.w = sampler.outputsVec4[left].w;

          glm::quat q2;
          q2.x = sampler.outputsVec4[right].x;
          q2.y = sampler.outputsVec4[right].y;
          q2.z = sampler.outputsVec4[right].z;
          q2.w = sampler.outputsVec4[right].w;

          channel.node->rotation = glm::normalize(glm::slerp(q1, q2, a));
        }
        if (channel.path == "scale") {
          channel.node->scale = glm::mix(sampler.outputsVec4[left], sampler.outputsVec4[right], a);
        }
      }
    } else if (sampler.interpolation == "STEP") {
      auto higher = std::find_if(sampler.inputs.begin(), sampler.inputs.end(),
                                 [current = animation->currentTime, start = animation->start](float value) {
                                   return value > (current + start);
                                 });
      // Get the input keyframe values for the current time stamp
      if (sampler.inputs.empty() || sampler.outputsVec4.empty()) continue;
      const auto value = higher == sampler.inputs.end()
                             ? sampler.outputsVec4.size() - 1
                             : static_cast<std::size_t>(std::distance(sampler.inputs.begin(), higher));
      if (channel.path == "translation") {
        channel.node->translation = sampler.outputsVec4[value];
      }
      if (channel.path == "rotation") {
        glm::quat q;
        q.x = sampler.outputsVec4[value].x;
        q.y = sampler.outputsVec4[value].y;
        q.z = sampler.outputsVec4[value].z;
        q.w = sampler.outputsVec4[value].w;
        channel.node->rotation = glm::normalize(q);
      }
      if (channel.path == "scale") {
        channel.node->scale = sampler.outputsVec4[value];
      }
    }

    // mix with previous position
    if (_transitionFramesCounter <= _transitionFrames) {
      float smooth = (float)_transitionFramesCounter / (float)_transitionFrames;
      auto translateOld = _translationPrevious[channel.node];
      channel.node->translation = glm::mix(translateOld, channel.node->translation, smooth);
      _translationPrevious[channel.node] = channel.node->translation;
      auto scaleOld = _scalePrevious[channel.node];
      channel.node->scale = glm::mix(scaleOld, channel.node->scale, smooth);
      _scalePrevious[channel.node] = channel.node->scale;
      auto rotateOld = _rotationPrevious[channel.node];
      channel.node->rotation = glm::normalize(glm::slerp(rotateOld, channel.node->rotation, smooth));
      _rotationPrevious[channel.node] = channel.node->rotation;
    }
  }
  if (_transitionFramesCounter <= _transitionFrames) _transitionFramesCounter++;
  logger->end();

  logger->begin("Update matrixes");
  _matricesGlobal.clear();
  for (auto& node : _nodes) {
    _fillMatricesJoint(node, glm::mat4(1.f));
  }
  logger->end();
}

void Animation::updateBuffers(int currentImage) {
  for (auto& node : _nodes) {
    _updateJoints(currentImage, node);
  }
}
