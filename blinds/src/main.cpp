#include <Arduino.h>

#include "BlindController.h"
#include "BlindRadio.h"
#include "Config.h"
#include "SerialCommands.h"

namespace {
TimingSettings timing;
BlindRadio radio(timing);
BlindController controller(radio, timing);
SerialCommands commands(controller, radio, timing);
}

void setup() {
  pinMode(Config::STATUS_LED_PIN, OUTPUT);
  digitalWrite(Config::STATUS_LED_PIN, LOW);

  Serial.begin(Config::SERIAL_BAUD_RATE);
  delay(500);
  Serial.println(F("\nESP32 Blind Controller Booting..."));

  if (!radio.begin()) {
    Serial.println(F("[ERR] nRF24L01+ hardware failed to initialize!"));
    while (true) {
      digitalWrite(Config::STATUS_LED_PIN, !digitalRead(Config::STATUS_LED_PIN));
      delay(250);
    }
  }

  Serial.println(F("[OK] nRF24L01+ initialized."));
  commands.printHelp();
}

void loop() {
  controller.update();
  radio.poll();
  commands.poll();
}
