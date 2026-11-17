#pragma once

#include <Arduino.h>

class WiFiController {
public:
  void begin(const char* ssid, const char* password);
  void update();

private:
  void connect();

  const char* ssid_ = nullptr;
  const char* password_ = nullptr;
  uint32_t lastConnectAttemptMs_ = 0;
  bool configured_ = false;
  bool connected_ = false;
};
