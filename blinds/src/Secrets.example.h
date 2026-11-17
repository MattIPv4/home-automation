#pragma once

#include <Arduino.h>

// Copy this file to Secrets.h and set the remote ID discovered using "sniff".
// Secrets.h is Git-ignored; keep device-specific values and future Wi-Fi credentials there.
namespace Secrets {
constexpr uint8_t REMOTE_ID[] = {0x00, 0x00, 0x00};
}
