#include "WiFiController.h"

#include <WiFi.h>

#include "Config.h"

void WiFiController::begin(const char* ssid, const char* password) {
  if (ssid == nullptr || ssid[0] == '\0' || password == nullptr) {
    Serial.println(F("[WIFI] Credentials not configured; Wi-Fi/Matter disabled. "
                     "Serial/RF remain available."));
    return;
  }
  WiFi.persistent(false);
  if (!WiFi.mode(WIFI_STA) || !WiFi.setAutoReconnect(false) || !WiFi.enableIPv6()) {
    Serial.println(F("[ERR] Could not configure Wi-Fi/IPv6; Matter unavailable."));
    return;
  }
  ssid_ = ssid;
  password_ = password;
  configured_ = true;
  connect();
}

void WiFiController::connect() {
  Serial.println(F("[WIFI] Connecting..."));
  lastConnectAttemptMs_ = millis();
  if (WiFi.begin(ssid_, password_) == WL_CONNECT_FAILED) {
    Serial.println(F("[WARN] Wi-Fi connection could not start; will retry."));
  }
}

void WiFiController::update() {
  if (!configured_) {
    return;
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != connected_) {
    connected_ = connected;
    Serial.println(connected ? F("[WIFI] Connected.")
                             : F("[WARN] Wi-Fi disconnected; Matter unavailable. "
                                 "Serial/RF remain available."));
  }
  if (!connected && millis() - lastConnectAttemptMs_ >= Config::WIFI_RECONNECT_INTERVAL_MS) {
    Serial.printf("[WARN] Wi-Fi not connected (status %d); retrying.\n", WiFi.status());
    if (!WiFi.disconnect(false, false, 0)) {
      Serial.println(F("[WARN] Wi-Fi disconnect failed; attempting to connect again."));
    }
    connect();
  }
}
