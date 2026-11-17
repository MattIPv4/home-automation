#include "SerialCommands.h"

#include "MatterController.h"

namespace {
enum class CommandType {
  Pair,
  Open,
  Close,
  Day,
  Reset,
  Status,
  Sniff,
  Help,
  Time,
  Hold,
  Raw,
  Unknown
};

struct CommandName {
  const char* name;
  CommandType type;
};

constexpr CommandName COMMAND_NAMES[] = {
    {"pair", CommandType::Pair},   {"o", CommandType::Open},      {"O", CommandType::Open},
    {"c", CommandType::Close},     {"C", CommandType::Close},     {"d", CommandType::Day},
    {"D", CommandType::Day},       {"reset", CommandType::Reset}, {"status", CommandType::Status},
    {"sniff", CommandType::Sniff}, {"time", CommandType::Time},   {"hold", CommandType::Hold},
    {"h", CommandType::Help},      {"?", CommandType::Help},
};

CommandType parseCommand(const String& name) {
  for (const CommandName& command : COMMAND_NAMES) {
    if (name == command.name) {
      return command.type;
    }
  }
  if (name.length() == 4 && tolower(name[0]) == 'g') {
    return CommandType::Raw;
  }
  return CommandType::Unknown;
}

bool hasArgumentCount(size_t count, size_t minimum, size_t maximum) {
  if (count >= minimum && count <= maximum) {
    return true;
  }
  Serial.println(F("[ERR] Incorrect number of arguments. Type 'h' for help."));
  return false;
}

}

SerialCommands::SerialCommands(BlindController& controller, BlindRadio& radio,
                               TimingSettings& timing, MatterController& matter)
    : controller_(controller), radio_(radio), timing_(timing), matter_(matter) {}

void SerialCommands::printHelp() const {
  Serial.println(F("\n=== Blind Controller Commands ==="));
  Serial.println(F("  pair               - Clear all Matter pairings and pair again (no reboot)"));
  Serial.println(F("  o [0-4]            - Move to OPEN    (0/omitted = All [0xFF])"));
  Serial.println(F("  c [0-4]            - Move to CLOSED  (0/omitted = All [0xFF])"));
  Serial.println(F("  d [0-4]            - Move to DAY     (0/omitted = All [0xFF])"));
  Serial.println(F("  reset [0-4]        - Reset state to UNKNOWN to force re-home"));
  Serial.println(F("  status             - Show tracked state & pending queue"));
  Serial.println(F("  sniff              - Toggle filtered/all-ID RF logging (default filtered)"));
  Serial.println(F("  time [travel vane] - Get/set movement times in ms"));
  Serial.println(F("  hold [act rel]     - Get/set TX hold times in ms"));
  Serial.println(F("  gXXu / gXXd        - Send raw pulse to hex mask XX (e.g. g03u)"));
  Serial.println(F("  h / ?              - Show this help menu\n"));
}

void SerialCommands::poll() {
  if (!Serial.available()) {
    return;
  }

  String command = Serial.readStringUntil('\n');
  command.trim();
  if (command.isEmpty()) {
    return;
  }

  // Commands take at most two arguments, so allow three tokens.
  String args[3];
  size_t tokenCount = 0;
  size_t position = 0;
  while (position < command.length()) {
    if (isspace(static_cast<unsigned char>(command[position]))) {
      position++;
      continue;
    }

    if (tokenCount == 3) {
      Serial.println(F("[ERR] Too many arguments. Type 'h' for help."));
      return;
    }

    const size_t start = position;
    while (position < command.length() && !isspace(static_cast<unsigned char>(command[position]))) {
      position++;
    }
    args[tokenCount++] = command.substring(start, position);
  }

  const String& name = args[0];
  const size_t argumentCount = tokenCount - 1;
  const CommandType type = parseCommand(name);

  switch (type) {
  case CommandType::Pair:
    if (!hasArgumentCount(argumentCount, 0, 0)) {
      return;
    }
    matter_.pair();
    break;

  case CommandType::Open:
  case CommandType::Close:
  case CommandType::Day:
  case CommandType::Reset: {
    if (!hasArgumentCount(argumentCount, 0, 1)) {
      return;
    }
    const uint8_t target = argumentCount == 1 ? static_cast<uint8_t>(args[1].toInt()) : 0;
    if (type == CommandType::Reset) {
      controller_.resetState(target);
    } else if (type == CommandType::Open) {
      controller_.setState(target, BlindState::Open);
    } else if (type == CommandType::Close) {
      controller_.setState(target, BlindState::Closed);
    } else {
      controller_.setState(target, BlindState::Day);
    }
    break;
  }

  case CommandType::Status:
    if (!hasArgumentCount(argumentCount, 0, 0)) {
      return;
    }
    controller_.printStatus();
    radio_.printSniffMode();
    break;

  case CommandType::Sniff:
    if (!hasArgumentCount(argumentCount, 0, 0)) {
      return;
    }
    radio_.toggleSniffMode();
    break;

  case CommandType::Help:
    if (!hasArgumentCount(argumentCount, 0, 0)) {
      return;
    }
    printHelp();
    break;

  case CommandType::Time:
    if (argumentCount != 0) {
      if (!hasArgumentCount(argumentCount, 2, 2)) {
        return;
      }
      timing_.travelMs = args[1].toInt();
      timing_.vaneMs = args[2].toInt();
    }
    Serial.printf("travelMs = %lu ms | vaneMs = %lu ms\n", timing_.travelMs, timing_.vaneMs);
    break;

  case CommandType::Hold:
    if (argumentCount != 0) {
      if (!hasArgumentCount(argumentCount, 2, 2)) {
        return;
      }
      timing_.activeHoldMs = args[1].toInt();
      timing_.releaseHoldMs = args[2].toInt();
    }
    Serial.printf("activeHoldMs = %u ms | releaseHoldMs = %u ms\n", timing_.activeHoldMs,
                  timing_.releaseHoldMs);
    break;

  case CommandType::Raw: {
    if (!hasArgumentCount(argumentCount, 0, 0)) {
      return;
    }
    const uint8_t mask = static_cast<uint8_t>(strtol(name.substring(1, 3).c_str(), nullptr, 16));
    const Direction direction = tolower(name[3]) == 'u' ? Direction::Up : Direction::Down;
    radio_.sendPress(mask, direction, "Raw manual pulse");
    break;
  }

  case CommandType::Unknown:
    Serial.printf("Unknown command '%s'. Type 'h' for help.\n", command.c_str());
    break;
  }
}
