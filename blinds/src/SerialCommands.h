#pragma once

#include "BlindController.h"

class MatterController;

class SerialCommands {
public:
  SerialCommands(BlindController& controller, BlindRadio& radio, TimingSettings& timing,
                 MatterController& matter);

  void printHelp() const;
  void poll();

private:
  BlindController& controller_;
  BlindRadio& radio_;
  TimingSettings& timing_;
  MatterController& matter_;
};
