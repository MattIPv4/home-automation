#include <Arduino.h>

#include "AlexaController.h"
#include "BlindController.h"
#include "BlindRadio.h"
#include "Config.h"
#include "Secrets.h"
#include "SerialCommands.h"

namespace {
TimingSettings timing;
BlindRadio radio(timing);
BlindController controller(radio, timing);
AlexaController alexa(controller);
SerialCommands commands(controller, radio, timing);
}

void setup() {
  pinMode(Config::STATUS_LED_PIN, OUTPUT);
  digitalWrite(Config::STATUS_LED_PIN, LOW);

  Serial.begin(Config::SERIAL_BAUD_RATE);
  delay(500);
  Serial.println(F("\nESP32 Blind Controller Booting..."));

  if (!radio.begin()) {
    Serial.println(F("[ERR] Radio hardware or transmitter task failed to initialize!"));
    while (true) {
      digitalWrite(Config::STATUS_LED_PIN, !digitalRead(Config::STATUS_LED_PIN));
      delay(250);
    }
  }

  Serial.println(F("[OK] nRF24L01+ initialized."));
  commands.printHelp();
  alexa.begin(Secrets::WIFI_SSID, Secrets::WIFI_PASS);
}

void loop() {
  alexa.update();
  controller.update();
  radio.poll();
  commands.poll();
  // Let core 0's idle task run, including when Wi-Fi is disabled. RF runs on core 1.
  delay(1);
}
