#pragma once

#include "BlindController.h"

class SerialCommands {
public:
  SerialCommands(BlindController& controller, BlindRadio& radio, TimingSettings& timing);

  void printHelp() const;
  void poll();

private:
  BlindController& controller_;
  BlindRadio& radio_;
  TimingSettings& timing_;
};
