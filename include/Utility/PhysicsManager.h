#pragma once
#include <type_traits>
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <glm/glm.hpp>
#include "BS_thread_pool.hpp"
#include "Utility/EngineState.h"

// Layer that objects can be in, determines which other objects it can collide with
// Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
// layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics
// simulation but only if you do collision testing).
namespace Layers {
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING = 1;
static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};  // namespace Layers

/// Class that determines if two object layers can collide
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
 public:
  virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
    switch (inObject1) {
      case Layers::NON_MOVING:
        return inObject2 == Layers::MOVING;  // Non moving only collides with moving
      case Layers::MOVING:
        return true;  // Moving collides with everything
      default:
        JPH_ASSERT(false);
        return false;
    }
  }
};

// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
namespace BroadPhaseLayers {
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr JPH::uint NUM_LAYERS(2);
};  // namespace BroadPhaseLayers

// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
 public:
  BPLayerInterfaceImpl() {
    // Create a mapping table from object to broad phase layer
    mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
    mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
  }

  virtual JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }

  virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
    JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
    return mObjectToBroadPhase[inLayer];
  }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
    switch ((JPH::BroadPhaseLayer::Type)inLayer) {
      case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:
        return "NON_MOVING";
      case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:
        return "MOVING";
      default:
        JPH_ASSERT(false);
        return "INVALID";
    }
  }
#endif  // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

 private:
  JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

/// Class that determines if an object layer can collide with a broadphase layer
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
 public:
  virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
    switch (inLayer1) {
      case Layers::NON_MOVING:
        return inLayer2 == BroadPhaseLayers::MOVING;
      case Layers::MOVING:
        return true;
      default:
        JPH_ASSERT(false);
        return false;
    }
  }
};

class CustomThreadPool : public JPH::JobSystemWithBarrier {
 private:
  std::shared_ptr<BS::thread_pool> _pool;
  int _inNumThreads;
  /// Array of jobs (fixed size)
  using AvailableJobs = JPH::FixedSizeFreeList<Job>;
  AvailableJobs _jobs;
  std::shared_ptr<Logger> _logger;

 public:
  CustomThreadPool(int inMaxJobs,
                   int inMaxBarriers,
                   int inNumThreads,
                   std::shared_ptr<Logger> logger,
                   std::shared_ptr<BS::thread_pool> pool);
  /// Create a new job, the job is started immediately if inNumDependencies == 0 otherwise it starts when
  /// RemoveDependency causes the dependency counter to reach 0.
  JPH::JobHandle CreateJob(const char* inName,
                           JPH::ColorArg inColor,
                           const JobFunction& inJobFunction,
                           JPH::uint32 inNumDependencies) override;

  /// Adds a job to the job queue
  void QueueJob(Job* inJob) override;
  /// Adds a number of jobs at once to the job queue
  void QueueJobs(Job** inJobs, JPH::uint inNumJobs) override;

  /// Get maximum number of concurrently executing jobs
  int GetMaxConcurrency() const override;

  /// Frees a job
  void FreeJob(Job* inJob) override;
  ~CustomThreadPool() override;
};

class PhysicsManager {
 private:
  std::shared_ptr<EngineState> _engineState;
  JPH::PhysicsSystem _physicsSystem;
  std::shared_ptr<JPH::TempAllocatorImpl> _tempAllocator;
  std::shared_ptr<CustomThreadPool> _jobSystem;
  float _deltaTime = 1.0f / 60.0f;
  const int _collisionSteps = 1;

  // Create mapping table from object layer to broadphase layer
  // Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
  BPLayerInterfaceImpl _broadPhaseLayerInterface;
  // Create class that filters object vs broadphase layers
  // Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
  ObjectVsBroadPhaseLayerFilterImpl _objectVsBroadphaseLayerFilter;
  // Create class that filters object vs object layers
  // Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
  ObjectLayerPairFilterImpl _objectVsObjectLayerFilter;

 public:
  PhysicsManager(std::shared_ptr<BS::thread_pool> pool, std::shared_ptr<EngineState> engineState);
  JPH::BodyInterface& getBodyInterface();
  JPH::PhysicsSystem& getPhysicsSystem();
  glm::vec3 getGravity();
  void setDeltaTime(float deltaTime);
  float getDeltaTime();
  void update();
  ~PhysicsManager();
};
