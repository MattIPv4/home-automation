#include "BlindController.h"

#include <algorithm>

const char* blindStateName(BlindState state) {
  switch (state) {
  case BlindState::Unknown:
    return "UNKNOWN";
  case BlindState::Open:
    return "OPEN";
  case BlindState::Closed:
    return "CLOSED";
  case BlindState::Day:
    return "DAY";
  case BlindState::None:
    return "NONE";
  }
  return "INVALID";
}

bool isKnownBlindState(BlindState state) {
  return state == BlindState::Open || state == BlindState::Closed || state == BlindState::Day;
}

BlindState combinedBlindState(const BlindState (&states)[Config::TARGET_COUNT]) {
  const BlindState first = states[Config::FIRST_BLIND];
  for (size_t blind = Config::FIRST_BLIND + 1; blind < Config::TARGET_COUNT; blind++) {
    if (states[blind] != first) {
      return BlindState::Unknown;
    }
  }
  return first;
}

BlindController::BlindController(BlindRadio& radio, const TimingSettings& timing)
    : radio_(radio), timing_(timing) {}

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------

void BlindController::setState(uint8_t target, BlindState desired) {
  if (!isValidTarget(target)) {
    return;
  }
  if (!isKnownBlindState(desired)) {
    Serial.println(F("[ERR] Requested state must be OPEN, CLOSED or DAY."));
    return;
  }

  if (move_.active || radio_.isTransmitting()) {
    queueRequest(target, desired);
    return;
  }

  const uint8_t mask = Config::GROUP_MASKS[target];
  const BlindState current = targets_[target].settled;
  if (current == desired) {
    Serial.printf("[STATE] Target %u (0x%02X) already %s (no TX needed)\n", target, mask,
                  blindStateName(desired));
    return;
  }

  Serial.printf("[STATE] Target %u (0x%02X): %s -> %s\n", target, mask, blindStateName(current),
                blindStateName(desired));
  startMove(target, current, desired);
}

BlindState BlindController::getState(uint8_t target) const {
  if (!isValidTarget(target)) {
    return BlindState::Unknown;
  }

  if (target == Config::ALL_BLINDS) {
    BlindState states[Config::TARGET_COUNT]{};
    for (uint8_t blind = Config::FIRST_BLIND; blind < Config::TARGET_COUNT; blind++) {
      states[blind] = getState(blind);
    }
    return combinedBlindState(states);
  }

  if (movingTo(target) != BlindState::None) {
    return queuedStateFor(target) == BlindState::None ? movingTo(target) : queuedStateFor(target);
  }
  const BlindState queued = queuedStateFor(target);
  return queued == BlindState::None ? targets_[target].settled : queued;
}

bool BlindController::isBusy() const { return move_.active || radio_.isTransmitting(); }

void BlindController::resetState(uint8_t target) {
  if (!isValidTarget(target)) {
    return;
  }

  resetCount_++;
  // Cancelling a move part-way leaves its blinds in an unknown position.
  if (move_.active) {
    setSettled(move_.target, BlindState::Unknown);
  }
  move_ = Move{};
  clearQueuedCommands();
  setSettled(target, BlindState::Unknown);
  Serial.printf("[STATE] Target %u (0x%02X) reset to UNKNOWN.\n", target,
                Config::GROUP_MASKS[target]);
}

void BlindController::update() {
  // Wait for the radio to finish before looking at its result.
  if (radio_.isTransmitting()) {
    return;
  }
  if (!handlePressResult()) {
    return;
  }

  if (move_.active) {
    runNextStep();
  } else {
    startNextQueuedRequest();
  }
}

void BlindController::printStatus() const {
  Serial.println(F("\n--- Blind States ---"));
  for (uint8_t i = 0; i < Config::TARGET_COUNT; i++) {
    const TargetState& state = targets_[i];
    Serial.printf("  [%u] Mask 0x%02X: %s", i, Config::GROUP_MASKS[i],
                  blindStateName(state.settled));
    if (movingTo(i) != BlindState::None) {
      Serial.printf(" -> %s (moving)", blindStateName(movingTo(i)));
    }
    const BlindState queued = queuedStateFor(i);
    if (queued != BlindState::None) {
      Serial.printf(" [next: %s]", blindStateName(queued));
    }
    Serial.println();
  }
  const size_t remainingSteps = move_.active ? move_.stepCount - move_.nextStep : 0;
  Serial.printf("Queued steps: %u | travelMs=%lu | vaneMs=%lu | dur=%u/%u ms\n\n",
                static_cast<unsigned>(remainingSteps), timing_.travelMs, timing_.vaneMs,
                timing_.activeHoldMs, timing_.releaseHoldMs);
}

// ---------------------------------------------------------------------------------------------
// Target state helpers
// ---------------------------------------------------------------------------------------------

bool BlindController::isValidTarget(uint8_t target) const {
  if (target >= Config::TARGET_COUNT) {
    Serial.println(F("[ERR] Target index must be 0 (All) or 1-4."));
    return false;
  }
  return true;
}

BlindState BlindController::movingTo(uint8_t target) const {
  // A move on all blinds also moves each individual blind.
  if (move_.active && (move_.target == target || move_.target == Config::ALL_BLINDS)) {
    return move_.destination;
  }
  return BlindState::None;
}

BlindState BlindController::queuedStateFor(uint8_t target) const {
  BlindState state = BlindState::None;
  for (size_t i = 0; i < queuedCommandCount_; i++) {
    const QueuedCommand& command = queuedCommands_[i];
    if (command.target == Config::ALL_BLINDS || command.target == target) {
      state = command.state;
    }
  }
  return state;
}

void BlindController::setSettled(uint8_t target, BlindState state) {
  targets_[target].settled = state;
  if (target == Config::ALL_BLINDS) {
    for (uint8_t blind = Config::FIRST_BLIND; blind < Config::TARGET_COUNT; blind++) {
      targets_[blind].settled = state;
    }
  } else {
    // One blind moved on its own, so the blinds may no longer share a single position.
    targets_[Config::ALL_BLINDS].settled = BlindState::Unknown;
  }
}

void BlindController::queueRequest(uint8_t target, BlindState desired) {
  const uint8_t mask = Config::GROUP_MASKS[target];
  if (movingTo(target) == desired) {
    if (target == Config::ALL_BLINDS) {
      clearQueuedCommands();
      Serial.printf("[STATE] Target %u (0x%02X) already moving to %s\n", target, mask,
                    blindStateName(desired));
      return;
    }
    // Keep a later single-blind request if an earlier queued All command would override this move.
    if (queuedStateFor(target) == BlindState::None) {
      removeQueuedCommand(target);
      Serial.printf("[STATE] Target %u (0x%02X) already moving to %s\n", target, mask,
                    blindStateName(desired));
      return;
    }
  }

  // A new All request supersedes every queued command. A single-blind request replaces only
  // that blind's earlier request and stays after any queued All request.
  if (target == Config::ALL_BLINDS) {
    clearQueuedCommands();
  } else {
    removeQueuedCommand(target);
  }

  if (queuedCommandCount_ >= Config::TARGET_COUNT) {
    Serial.println(F("[WARN] Command queue full; dropping deferred request."));
    return;
  }
  queuedCommands_[queuedCommandCount_++] = {target, desired};
  Serial.printf(
      "[DEFER] RF/move in progress; queued Target %u (0x%02X) -> %s to run after settle\n", target,
      mask, blindStateName(desired));
}

bool BlindController::removeQueuedCommand(uint8_t target) {
  for (size_t i = 0; i < queuedCommandCount_; i++) {
    if (queuedCommands_[i].target != target) {
      continue;
    }
    for (size_t next = i + 1; next < queuedCommandCount_; next++) {
      queuedCommands_[next - 1] = queuedCommands_[next];
    }
    queuedCommandCount_--;
    return true;
  }
  return false;
}

void BlindController::clearQueuedCommands() { queuedCommandCount_ = 0; }

void BlindController::startNextQueuedRequest() {
  if (queuedCommandCount_ == 0) {
    return;
  }

  const QueuedCommand next = queuedCommands_[0];
  for (size_t i = 1; i < queuedCommandCount_; i++) {
    queuedCommands_[i - 1] = queuedCommands_[i];
  }
  queuedCommandCount_--;

  Serial.printf("[NEXT] Running deferred command for Target %u -> %s\n", next.target,
                blindStateName(next.state));
  setState(next.target, next.state);
}

// ---------------------------------------------------------------------------------------------
// Planning a move
// ---------------------------------------------------------------------------------------------

void BlindController::startMove(uint8_t target, BlindState from, BlindState to) {
  move_ = Move{};
  move_.active = true;
  move_.target = target;
  move_.destination = to;
  planMove(from, to);
}

// How the blinds respond to a single press:
//   OPEN   --DOWN--> CLOSED  (takes travelMs: the blind drops)
//   CLOSED --UP----> OPEN    (takes travelMs: the blind rises)
//   CLOSED --DOWN--> DAY     (takes vaneMs: the vanes tilt open)
//   DAY    --UP----> CLOSED  (takes vaneMs: the vanes tilt closed)
// Every delay is measured from when the move's first press finished, not from the previous step.
void BlindController::planMove(BlindState from, BlindState to) {
  if (from == BlindState::Unknown) {
    planHoming(to);
    return;
  }

  const uint32_t travelMs = timing_.travelMs;
  const uint32_t vaneMs = timing_.vaneMs;

  if (from == BlindState::Open && to == BlindState::Closed) {
    addPress(Direction::Down, 0, "OPEN->CLOSED: DOWN drops blind");
    addSettle(travelMs);
  } else if (from == BlindState::Open && to == BlindState::Day) {
    addPress(Direction::Down, 0, "OPEN->DAY: first DOWN drops blind");
    addPress(Direction::Down, travelMs, "OPEN->DAY: second DOWN sets DAY");
    addSettle(travelMs + vaneMs);
  } else if (from == BlindState::Closed && to == BlindState::Open) {
    addPress(Direction::Up, 0, "CLOSED->OPEN: UP opens blind");
    addSettle(travelMs);
  } else if (from == BlindState::Closed && to == BlindState::Day) {
    addPress(Direction::Down, 0, "CLOSED->DAY: DOWN sets DAY");
    addSettle(vaneMs);
  } else if (from == BlindState::Day && to == BlindState::Open) {
    addPress(Direction::Up, 0, "DAY->OPEN: first UP closes vanes");
    addPress(Direction::Up, vaneMs, "DAY->OPEN: second UP opens blind");
    addSettle(vaneMs + travelMs);
  } else if (from == BlindState::Day && to == BlindState::Closed) {
    addPress(Direction::Up, 0, "DAY->CLOSED: UP closes vanes");
    addSettle(vaneMs);
  }
}

// The position is unknown, so use presses that reach the destination from any starting point.
void BlindController::planHoming(BlindState to) {
  const uint32_t travelMs = timing_.travelMs;
  const uint32_t vaneMs = timing_.vaneMs;

  if (to == BlindState::Open) {
    // The first UP may tilt the vanes or raise the blind; wait for either to finish.
    const uint32_t firstUpMs = std::max(travelMs, vaneMs);
    addPress(Direction::Up, 0, "Homing: first UP");
    addPress(Direction::Up, firstUpMs, "Homing: second UP opens blind");
    addSettle(firstUpMs + travelMs);
    return;
  }

  // Two DOWN presses reach DAY without opening the blinds first.
  addPress(Direction::Down, 0, "Homing: first DOWN");
  addPress(Direction::Down, travelMs, "Homing: second DOWN sets DAY");
  if (to == BlindState::Day) {
    addSettle(travelMs + vaneMs);
    return;
  }

  addPress(Direction::Up, travelMs + vaneMs, "Homing: UP closes vanes");
  addSettle(travelMs + (2 * vaneMs));
}

void BlindController::addPress(Direction direction, uint32_t delayMs, const char* reason) {
  addStep({StepAction::Press, direction, delayMs, reason});
}

void BlindController::addSettle(uint32_t delayMs) {
  addStep({StepAction::Settle, Direction::Down, delayMs, "Motor settled"});
}

void BlindController::addStep(Step step) {
  if (move_.stepCount >= MAX_STEPS) {
    Serial.println(F("[WARN] Step queue full; dropping scheduled step."));
    return;
  }
  move_.steps[move_.stepCount++] = step;

  const char* action = step.action == StepAction::Settle
                           ? "Settle complete"
                           : (step.direction == Direction::Up ? "UP" : "DOWN");
  Serial.printf("[QUEUE] Mask 0x%02X -> %s in %lu ms (%s)\n", Config::GROUP_MASKS[move_.target],
                action, step.delayMs, step.reason);
}

// ---------------------------------------------------------------------------------------------
// Running a move
// ---------------------------------------------------------------------------------------------

// Returns false if a press failed and the move was cancelled.
bool BlindController::handlePressResult() {
  // Always take the result, including for raw serial pulses, so the radio can accept more presses.
  BlindRadio::TxResult result;
  const bool hasResult = radio_.takeTxResult(result);
  if (!move_.pressInFlight) {
    return true;
  }

  if (!hasResult || !result.succeeded) {
    Serial.println(F("[ERR] RF press failed; cancelling the move and deferred commands."));
    resetState(move_.target);
    return false;
  }

  move_.pressInFlight = false;
  move_.nextStep++;
  if (!move_.started) {
    move_.started = true;
    move_.startedAtMs = result.finishedAtMs;
  }
  return true;
}

void BlindController::runNextStep() {
  const Step& step = move_.steps[move_.nextStep];

  // The first press runs right away. Unsigned subtraction keeps working when millis() wraps.
  if (move_.started && millis() - move_.startedAtMs < step.delayMs) {
    return;
  }

  if (step.action == StepAction::Press) {
    // If the radio rejects the press, it is retried on the next update.
    move_.pressInFlight = radio_.sendPress(Config::GROUP_MASKS[move_.target], step.direction,
                                           step.reason, step.delayMs);
    return;
  }

  finishMove();
}

void BlindController::finishMove() {
  const uint8_t target = move_.target;
  const BlindState destination = move_.destination;
  move_ = Move{};

  setSettled(target, destination);
  Serial.printf("[STATE] Target %u (0x%02X) settled at %s\n", target, Config::GROUP_MASKS[target],
                blindStateName(destination));
  startNextQueuedRequest();
}
