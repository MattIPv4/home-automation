#include "BlindController.h"

namespace {
const char* stateName(BlindState state) {
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
}

BlindController::BlindController(BlindRadio& radio, const TimingSettings& timing)
    : radio_(radio), timing_(timing) {}

bool BlindController::anyMoveInProgress() const {
  for (const TargetState& target : targets_) {
    if (target.moving) {
      return true;
    }
  }
  return false;
}

void BlindController::commitState(uint8_t target, BlindState state) {
  targets_[target].confirmed = state;
  targets_[target].pending = BlindState::None;
  targets_[target].moving = false;

  if (target == 0) {
    for (size_t i = 1; i < Config::TARGET_COUNT; i++) {
      targets_[i].confirmed = state;
      targets_[i].pending = BlindState::None;
      targets_[i].moving = false;
    }
  } else {
    targets_[0].confirmed = BlindState::Unknown;
    targets_[0].pending = BlindState::None;
    targets_[0].moving = false;
  }
}

void BlindController::enqueue(ScheduledStep step) {
  if (queueCount_ >= MAX_QUEUED_STEPS) {
    Serial.println(F("[WARN] Step queue full; dropping scheduled step."));
    return;
  }

  step.executeAtMs = millis() + step.delayMs;
  const size_t slot = (queueHead_ + queueCount_) % MAX_QUEUED_STEPS;
  steps_[slot] = step;
  queueCount_++;

  const char* action = step.type == StepType::Complete
                           ? "Settle complete"
                           : (step.direction == Direction::Up ? "UP" : "DOWN");
  Serial.printf("[QUEUE] Mask 0x%02X -> %s in %lu ms (%s)\n", Config::GROUP_MASKS[step.target],
                action, step.delayMs, step.reason);
}

void BlindController::schedulePress(uint8_t target, Direction direction, uint32_t delayMs,
                                    const char* reason) {
  enqueue({StepType::Press, target, direction, delayMs, 0, reason, BlindState::None});
}

void BlindController::scheduleCompletion(uint8_t target, BlindState state, uint32_t delayMs) {
  enqueue({StepType::Complete, target, Direction::Down, delayMs, 0, "Motor settled", state});
}

void BlindController::runNextCommand() {
  for (size_t i = 0; i < Config::TARGET_COUNT; i++) {
    if (targets_[i].queued != BlindState::None) {
      const BlindState next = targets_[i].queued;
      targets_[i].queued = BlindState::None;
      Serial.printf("[NEXT] Running deferred command for Target %u -> %s\n",
                    static_cast<unsigned>(i), stateName(next));
      setState(i, next);
      return;
    }
  }
}

void BlindController::update() {
  if (queueCount_ == 0) {
    return;
  }

  const ScheduledStep& next = steps_[queueHead_];
  // Signed subtraction keeps this deadline check working when millis() wraps.
  if (static_cast<int32_t>(millis() - next.executeAtMs) < 0) {
    return;
  }

  const ScheduledStep step = next;
  queueHead_ = (queueHead_ + 1) % MAX_QUEUED_STEPS;
  queueCount_--;

  if (step.type == StepType::Press) {
    radio_.sendPress(Config::GROUP_MASKS[step.target], step.direction, step.reason, step.delayMs);
  } else {
    commitState(step.target, step.finalState);
    Serial.printf("[STATE] Target %u (0x%02X) settled at %s\n", step.target,
                  Config::GROUP_MASKS[step.target], stateName(step.finalState));
    runNextCommand();
  }
}

void BlindController::resetState(uint8_t target) {
  if (target >= Config::TARGET_COUNT) {
    Serial.println(F("[ERR] Target index must be 0 (All) or 1-4."));
    return;
  }

  queueHead_ = 0;
  queueCount_ = 0;
  for (TargetState& state : targets_) {
    state.queued = BlindState::None;
  }
  commitState(target, BlindState::Unknown);
  Serial.printf("[STATE] Target %u (0x%02X) reset to UNKNOWN.\n", target,
                Config::GROUP_MASKS[target]);
}

void BlindController::setState(uint8_t target, BlindState desired) {
  if (target >= Config::TARGET_COUNT) {
    Serial.println(F("[ERR] Target index must be 0 (All) or 1-4."));
    return;
  }
  if (desired != BlindState::Open && desired != BlindState::Closed && desired != BlindState::Day) {
    Serial.println(F("[ERR] Requested state must be OPEN, CLOSED or DAY."));
    return;
  }

  const uint8_t mask = Config::GROUP_MASKS[target];
  TargetState& state = targets_[target];

  if (anyMoveInProgress()) {
    if (state.moving && state.pending == desired) {
      state.queued = BlindState::None;
      Serial.printf("[STATE] Target %u (0x%02X) already moving to %s\n", target, mask,
                    stateName(desired));
      return;
    }

    if (target == 0) {
      for (size_t i = 1; i < Config::TARGET_COUNT; i++) {
        targets_[i].queued = BlindState::None;
      }
    } else {
      targets_[0].queued = BlindState::None;
    }
    state.queued = desired;
    Serial.printf("[DEFER] Move in progress; queued Target %u (0x%02X) -> %s to run after settle\n",
                  target, mask, stateName(desired));
    return;
  }

  if (state.confirmed == desired) {
    Serial.printf("[STATE] Target %u (0x%02X) already %s (no TX needed)\n", target, mask,
                  stateName(desired));
    return;
  }

  Serial.printf("[STATE] Target %u (0x%02X): %s -> %s\n", target, mask, stateName(state.confirmed),
                stateName(desired));
  state.moving = true;
  state.pending = desired;
  if (target == 0) {
    for (size_t i = 1; i < Config::TARGET_COUNT; i++) {
      targets_[i].moving = true;
      targets_[i].pending = desired;
    }
  }

  planTransition(target, state.confirmed, desired);
}

void BlindController::planTransition(uint8_t target, BlindState current, BlindState desired) {
  const uint8_t mask = Config::GROUP_MASKS[target];
  const uint32_t travelMs = timing_.travelMs;
  const uint32_t vaneMs = timing_.vaneMs;

  // Every delay below is measured from the end of the first radio press,
  // not from the previous scheduled step.
  if (current == BlindState::Unknown) {
    // Double-DOWN homes to DAY without first opening the blinds.
    radio_.sendPress(mask, Direction::Down, "Homing: first DOWN");
    schedulePress(target, Direction::Down, travelMs, "Homing: second DOWN sets DAY");

    if (desired == BlindState::Day) {
      scheduleCompletion(target, desired, travelMs);
    } else {
      schedulePress(target, Direction::Up, travelMs + vaneMs, "Homing: UP closes vanes");
      if (desired == BlindState::Closed) {
        scheduleCompletion(target, desired, travelMs + (2 * vaneMs));
      } else {
        schedulePress(target, Direction::Up, travelMs + (2 * vaneMs),
                      "Homing: second UP opens blind");
        scheduleCompletion(target, desired, (2 * travelMs) + (2 * vaneMs));
      }
    }
    return;
  }

  switch (desired) {
  case BlindState::Open:
    if (current == BlindState::Day) {
      radio_.sendPress(mask, Direction::Up, "DAY->OPEN: first UP closes vanes");
      schedulePress(target, Direction::Up, vaneMs, "DAY->OPEN: second UP opens blind");
      scheduleCompletion(target, desired, vaneMs + travelMs);
    } else {
      radio_.sendPress(mask, Direction::Up, "CLOSED->OPEN: UP opens blind");
      scheduleCompletion(target, desired, travelMs);
    }
    break;

  case BlindState::Closed:
    if (current == BlindState::Day) {
      radio_.sendPress(mask, Direction::Up, "DAY->CLOSED: UP closes vanes");
      scheduleCompletion(target, desired, vaneMs);
    } else {
      radio_.sendPress(mask, Direction::Down, "OPEN->CLOSED: DOWN drops blind");
      scheduleCompletion(target, desired, travelMs);
    }
    break;

  case BlindState::Day:
    if (current == BlindState::Open) {
      radio_.sendPress(mask, Direction::Down, "OPEN->DAY: first DOWN drops blind");
      schedulePress(target, Direction::Down, travelMs, "OPEN->DAY: second DOWN sets DAY");
      scheduleCompletion(target, desired, travelMs);
    } else {
      radio_.sendPress(mask, Direction::Down, "CLOSED->DAY: DOWN sets DAY");
      scheduleCompletion(target, desired, vaneMs);
    }
    break;

  case BlindState::Unknown:
  case BlindState::None:
    break; // setState() rejects these before planning a transition.
  }
}

void BlindController::printStatus() const {
  Serial.println(F("\n--- Blind States ---"));
  for (size_t i = 0; i < Config::TARGET_COUNT; i++) {
    const TargetState& state = targets_[i];
    Serial.printf("  [%u] Mask 0x%02X: %s", static_cast<unsigned>(i), Config::GROUP_MASKS[i],
                  stateName(state.confirmed));
    if (state.moving) {
      Serial.printf(" -> %s (moving)", stateName(state.pending));
    }
    if (state.queued != BlindState::None) {
      Serial.printf(" [next: %s]", stateName(state.queued));
    }
    Serial.println();
  }
  Serial.printf("Queued steps: %u | travelMs=%lu | vaneMs=%lu | dur=%u/%u ms\n\n",
                static_cast<unsigned>(queueCount_), timing_.travelMs, timing_.vaneMs,
                timing_.activeHoldMs, timing_.releaseHoldMs);
}
