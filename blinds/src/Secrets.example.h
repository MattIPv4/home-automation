#pragma once

#include <Arduino.h>

// Copy this file to Secrets.h, set your Wi-Fi credentials and the ID discovered using "sniff".
// Secrets.h is Git-ignored. An empty SSID disables Wi-Fi/Alexa but leaves serial/RF working.
namespace Secrets {
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASS[] = "";
constexpr uint8_t REMOTE_ID[] = {0x00, 0x00, 0x00};
}
