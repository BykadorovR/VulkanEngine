#include "Utility/Timer.h"

TimerFPS::TimerFPS() {
  _fps = 0;
  _reset();
}

void TimerFPS::_reset() {
  _frameCounter = 0;
  _sumTime = 0;
}

void TimerFPS::tick() { _startTimeCurrent = std::chrono::high_resolution_clock::now(); }

void TimerFPS::tock() {
  _frameCounter++;
  auto end = std::chrono::high_resolution_clock::now();

  // calculate frames per second
  std::chrono::duration<double> elapsed = end - _startTimeCurrent;
  _sumTime += elapsed.count();
  if (_sumTime > 1.f) {
    _fps = static_cast<int>(_frameCounter);
    _reset();
  }
}

int TimerFPS::getFPS() { return _fps; }

Timer::Timer(std::shared_ptr<EngineState> engineState) {
  _engineState = engineState;

  _frameCounter = 0;
  _frameCounterSleep = 0;
  _elapsedCurrent = 0;
  _fpsMaxPrevious = 0;
  _startTime = std::chrono::high_resolution_clock::now();
}

void Timer::reset() {
  _frameCounterSleep = 0;
  _startTime = std::chrono::high_resolution_clock::now();
}

void Timer::tick() { _startTimeCurrent = std::chrono::high_resolution_clock::now(); }

void Timer::tock() {
  _frameCounter++;
  _frameCounterSleep++;
  auto end = std::chrono::high_resolution_clock::now();

  // calculate current timing for models and particle systems
  std::chrono::duration<double> elapsedCurrent = (end - _startTimeCurrent);
  _elapsedCurrent = static_cast<float>(elapsedCurrent.count());
}

float Timer::getElapsedCurrent() { return _elapsedCurrent; }

void Timer::sleep(int fpsMax) {
  auto logger = _engineState->getLogger();
  // have to reset timer if FPS has been changed
  if (_fpsMaxPrevious && _fpsMaxPrevious != fpsMax) reset();
  const auto end = std::chrono::high_resolution_clock::now();
  _fpsMaxPrevious = fpsMax;
  const auto elapsedSleep = std::chrono::duration_cast<std::chrono::milliseconds>(end - _startTime);
  const auto expectedSleep = std::chrono::milliseconds{
      static_cast<std::chrono::milliseconds::rep>((1000.0 / fpsMax) * _frameCounterSleep)};
  if (elapsedSleep < expectedSleep) {
    const auto sleepDuration = expectedSleep - elapsedSleep;
    logger->begin("Sleep for: " + std::to_string(sleepDuration.count()));
    std::this_thread::sleep_for(sleepDuration);
    logger->end();
  }
}

uint64_t Timer::getFrameCounter() { return _frameCounter; }
