#pragma once

#include "BlindRadio.h"

enum class BlindState : uint8_t { Unknown, Open, Closed, Day, None };

class BlindController {
public:
  BlindController(BlindRadio& radio, const TimingSettings& timing);

  void setState(uint8_t target, BlindState state);
  // Includes queued destinations; target 0 is UNKNOWN unless all individuals agree.
  BlindState getState(uint8_t target) const;
  void resetState(uint8_t target);
  void update();
  void printStatus() const;

private:
  struct TargetState {
    BlindState confirmed = BlindState::Unknown;
    BlindState pending = BlindState::None;
    BlindState queued = BlindState::None;
    bool moving = false;
  };

  enum class StepType : uint8_t { Press, Complete };

  struct ScheduledStep {
    StepType type;
    uint8_t target;
    Direction direction;
    uint32_t delayMs;
    uint32_t executeAtMs;
    const char* reason;
    BlindState finalState;
  };

  static constexpr size_t MAX_QUEUED_STEPS = 6;

  bool anyMoveInProgress() const;
  void commitState(uint8_t target, BlindState state);
  void runNextCommand();
  void planTransition(uint8_t target, BlindState current, BlindState desired);
  void schedulePress(uint8_t target, Direction direction, uint32_t delayMs, const char* reason);
  void scheduleCompletion(uint8_t target, BlindState state, uint32_t delayMs);
  void enqueue(ScheduledStep step);

  BlindRadio& radio_;
  const TimingSettings& timing_;
  TargetState targets_[Config::TARGET_COUNT];
  ScheduledStep steps_[MAX_QUEUED_STEPS];
  size_t queueHead_ = 0;
  size_t queueCount_ = 0;
};
