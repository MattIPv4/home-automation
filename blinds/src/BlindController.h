#pragma once

#include "BlindRadio.h"

// Unknown is an untracked position; None means no state or request is present.
enum class BlindState : uint8_t { Unknown, Open, Closed, Day, None };

const char* blindStateName(BlindState state);
bool isKnownBlindState(BlindState state);
// The shared state of every individual blind (index 1+), or Unknown if they differ.
BlindState combinedBlindState(const BlindState (&states)[Config::TARGET_COUNT]);

// Tracks each blind's estimated position and turns requests like "Blind 2 -> DAY" into a timed
// sequence of UP/DOWN radio presses.
//
// How a request flows:
//   1. setState() either starts a move right away, or queues the request if a move is running.
//   2. Starting a move plans its steps: one or more presses, then a final "settle" step.
//   3. update() runs on every loop and performs each step once its delay has passed.
//   4. When the settle step runs, the destination becomes the settled state and the next
//      queued request (if any) starts.
//
// Only one move runs at a time. Targets use Config::GROUP_MASKS: 0 is all blinds, 1+ are single.
class BlindController {
public:
  BlindController(BlindRadio& radio, const TimingSettings& timing);

  void setState(uint8_t target, BlindState state);
  // The latest requested state: queued, else moving to, else settled.
  // For all blinds, this is Unknown unless every blind agrees.
  BlindState getState(uint8_t target) const;
  bool isBusy() const;
  // Integrations use this counter to discard pending commands after a reset or RF failure.
  uint32_t resetCount() const { return resetCount_; }
  void resetState(uint8_t target);
  void update();
  void printStatus() const;

private:
  struct TargetState {
    BlindState settled = BlindState::Unknown; // Estimated position after the last completed move.
  };

  struct QueuedCommand {
    uint8_t target;
    BlindState state;
  };

  enum class StepAction : uint8_t { Press, Settle };

  struct Step {
    StepAction action;
    Direction direction; // Only used by presses.
    uint32_t delayMs;    // Measured from when the move's first press finished.
    const char* reason;
  };

  // The longest plan (homing to CLOSED) is three presses and a settle.
  static constexpr size_t MAX_STEPS = 4;

  struct Move {
    bool active = false;
    uint8_t target = 0;
    BlindState destination = BlindState::None;
    Step steps[MAX_STEPS]{};
    size_t stepCount = 0;
    size_t nextStep = 0;
    bool pressInFlight = false; // The radio is sending steps[nextStep].
    bool started = false;       // The first press has finished, so startedAtMs is set.
    uint32_t startedAtMs = 0;
  };

  bool isValidTarget(uint8_t target) const;
  BlindState movingTo(uint8_t target) const;
  BlindState queuedStateFor(uint8_t target) const;
  void setSettled(uint8_t target, BlindState state);
  void queueRequest(uint8_t target, BlindState desired);
  bool removeQueuedCommand(uint8_t target);
  void clearQueuedCommands();
  void startNextQueuedRequest();

  void startMove(uint8_t target, BlindState from, BlindState to);
  void planMove(BlindState from, BlindState to);
  void planHoming(BlindState to);
  void addPress(Direction direction, uint32_t delayMs, const char* reason);
  void addSettle(uint32_t delayMs);
  void addStep(Step step);

  bool handlePressResult();
  void runNextStep();
  void finishMove();

  BlindRadio& radio_;
  const TimingSettings& timing_;
  TargetState targets_[Config::TARGET_COUNT];
  // Keep command order so an All request can be followed by a single-blind override.
  QueuedCommand queuedCommands_[Config::TARGET_COUNT]{};
  size_t queuedCommandCount_ = 0;
  Move move_;
  uint32_t resetCount_ = 0;
};
