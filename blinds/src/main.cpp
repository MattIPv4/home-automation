#include <Arduino.h>

#include "BlindController.h"
#include "BlindRadio.h"
#include "Config.h"
#include "MatterController.h"
#include "Secrets.h"
#include "SerialCommands.h"
#include "WiFiController.h"

namespace {
TimingSettings timing;
BlindRadio radio(timing);
BlindController controller(radio, timing);
WiFiController wifi;
MatterController matter(controller);
SerialCommands commands(controller, radio, timing, matter);
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
  wifi.begin(Secrets::WIFI_SSID, Secrets::WIFI_PASS);
}

void loop() {
  wifi.update();
  controller.update();
  matter.update();
  radio.poll();
  commands.poll();
  // Let core 0's idle task run, including when Wi-Fi is disabled. RF runs on core 1.
  delay(1);
}
