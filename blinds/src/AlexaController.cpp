#include "AlexaController.h"

#include <WiFi.h>

static_assert(Config::ALEXA_DEVICE_COUNT <= ESPALEXA_MAXDEVICES,
              "Too many Alexa devices for Espalexa.");

AlexaController::AlexaController(BlindController& controller) : controller_(controller) {}

void AlexaController::begin(const char* ssid, const char* password) {
  if (ssid == nullptr || ssid[0] == '\0' || password == nullptr) {
    Serial.println(F("[WIFI] WIFI_SSID is empty in Secrets.h; Wi-Fi/Alexa disabled. Serial/RF "
                     "remain available."));
    return;
  }
  ssid_ = ssid;
  password_ = password;

  for (size_t i = 0; i < Config::ALEXA_DEVICE_COUNT; i++) {
    const Config::AlexaDevice& device = Config::ALEXA_DEVICES[i];
    if (device.name == nullptr || device.name[0] == '\0' || device.target >= Config::TARGET_COUNT) {
      Serial.println(F("[ERR] Invalid Alexa device name/target in Config.h; Alexa disabled."));
      return;
    }
    for (size_t j = 0; j < i; j++) {
      if (strcmp(device.name, Config::ALEXA_DEVICES[j].name) == 0) {
        Serial.println(F("[ERR] Duplicate Alexa device name in Config.h; Alexa disabled."));
        return;
      }
    }
  }

  WiFi.persistent(false);
  if (!WiFi.mode(WIFI_STA) || !WiFi.setAutoReconnect(false)) {
    Serial.println(F("[ERR] Could not configure Wi-Fi station mode; Wi-Fi/Alexa disabled."));
    return;
  }

  for (size_t i = 0; i < Config::ALEXA_DEVICE_COUNT; i++) {
    const uint8_t deviceId = i;
    const uint8_t count = espalexa_.addDevice(
        Config::ALEXA_DEVICES[i].name,
        [this, deviceId](EspalexaDevice* device) {
          // Espalexa stores Hue brightness plus one; normalize to the existing command mapping.
          const uint8_t value = device->getValue();
          const uint8_t brightness = value > 0 ? value - 1 : 0;
          queueCommand(deviceId, value != 0, brightness);
        },
        EspalexaDeviceType::dimmable);
    if (count != i + 1) {
      Serial.println(F("[ERR] Could not register Alexa device; Wi-Fi/Alexa disabled."));
      return;
    }
    Serial.printf("[ALEXA] Device '%s' -> target %u\n", Config::ALEXA_DEVICES[i].name,
                  Config::ALEXA_DEVICES[i].target);
  }

  server_.onNotFound([this]() {
    if (!espalexa_.handleAlexaApiCall(server_.uri(), server_.arg("plain"))) {
      server_.send(404, "text/plain", "Not Found");
    }
  });

  configured_ = true;
  connectWifi();
}

void AlexaController::startAlexa() {
  lastAlexaStartAttemptMs_ = millis();
  // Reuse the server across reconnects; begin() registers these routes again.
  server_.removeRoute("/espalexa", HTTP_GET);
  server_.removeRoute("/description.xml", HTTP_GET);
  alexaReady_ = espalexa_.begin(&server_);
  if (alexaReady_) {
    Serial.println(F("[ALEXA] Ready on port 80. Discover devices from an Echo on the same LAN."));
  } else {
    Serial.println(F("[WARN] Alexa discovery could not start; will retry."));
  }
}

void AlexaController::connectWifi() {
  Serial.println(F("[WIFI] Connecting..."));
  lastConnectAttemptMs_ = millis();
  if (WiFi.begin(ssid_, password_) == WL_CONNECT_FAILED) {
    Serial.println(F("[WARN] Wi-Fi connection could not start; will retry."));
  }
}

void AlexaController::queueCommand(uint8_t deviceId, bool on, uint8_t brightness) {
  if (deviceId >= Config::ALEXA_DEVICE_COUNT) {
    Serial.println(F("[ERR] Unknown Alexa device ID; command ignored."));
    return;
  }

  const uint8_t target = Config::ALEXA_DEVICES[deviceId].target;
  // Even an unsupported percentage must cancel an earlier provisional power-on request.
  removePendingCommand(target);

  // Hue/Alexa can encode 0% as 0/1, 50% as 127/128, and 100% as 254/255.
  BlindState state;
  if (!on || brightness <= 1) {
    state = BlindState::Closed;
  } else if (brightness == 127 || brightness == 128) {
    state = BlindState::Day;
  } else if (brightness >= 254) {
    state = BlindState::Open;
  } else {
    Serial.printf(
        "[ERR] Unsupported Alexa brightness %u; use OFF/0%%, 50%% or 100%%. Command ignored.\n",
        brightness);
    return;
  }

  // One entry per target bounds the queue; replacing a command moves it to the back.
  pendingCommands_[pendingCommandCount_++] = {target, state, millis()};
  Serial.printf("[ALEXA] Pending target %u (on=%u, brightness=%u)\n", target, on, brightness);
}

bool AlexaController::hasPendingCommand(uint8_t target) const {
  for (size_t i = 0; i < pendingCommandCount_; i++) {
    if (target == 0 || pendingCommands_[i].target == 0 || pendingCommands_[i].target == target) {
      return true;
    }
  }
  return false;
}

void AlexaController::removePendingCommand(uint8_t target) {
  for (size_t i = 0; i < pendingCommandCount_; i++) {
    if (pendingCommands_[i].target != target) {
      continue;
    }

    for (size_t j = i + 1; j < pendingCommandCount_; j++) {
      pendingCommands_[j - 1] = pendingCommands_[j];
    }
    pendingCommandCount_--;
    return;
  }
}

void AlexaController::dispatchPendingCommand() {
  if (pendingCommandCount_ > 0 &&
      millis() - pendingCommands_[0].receivedAtMs >= Config::ALEXA_COMMAND_SETTLE_MS) {
    const Command command = pendingCommands_[0];
    removePendingCommand(command.target);
    // Keep radio work outside the HTTP callback, and service HTTP again before the next move.
    controller_.setState(command.target, command.state);
  }
}

void AlexaController::syncStates() {
  for (size_t i = 0; i < Config::ALEXA_DEVICE_COUNT; i++) {
    const uint8_t target = Config::ALEXA_DEVICES[i].target;
    // Preserve remembered brightness between Alexa's separate power/brightness requests.
    if (hasPendingCommand(target)) {
      continue;
    }

    uint8_t value;
    switch (controller_.getState(target)) {
    case BlindState::Closed:
      value = 0;
      break;
    case BlindState::Day:
      value = 128; // Hue brightness 127 (50%) plus Espalexa's offset.
      break;
    case BlindState::Open:
      value = 255;
      break;
    case BlindState::Unknown:
    case BlindState::None:
      // Hue lights cannot represent unknown or mixed blind states; keep the previous value.
      continue;
    default:
      Serial.println(F("[ERR] Invalid blind state while synchronizing Alexa."));
      continue;
    }

    EspalexaDevice* device = espalexa_.getDevice(i);
    if (device == nullptr) {
      Serial.println(F("[ERR] Alexa device missing while synchronizing state."));
      continue;
    }
    if (device->getValue() != value) {
      device->setValue(value);
    }
  }
}

void AlexaController::updateNetwork() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != connected_) {
    connected_ = connected;
    alexaReady_ = false;
    if (connected) {
      Serial.printf("[WIFI] Connected: %s\n", WiFi.localIP().toString().c_str());
      startAlexa();
    } else {
      server_.stop();
      Serial.println(
          F("[WARN] Wi-Fi disconnected; Alexa unavailable. Serial/RF remain available."));
    }
  }

  if (connected) {
    if (!alexaReady_ && millis() - lastAlexaStartAttemptMs_ >= Config::WIFI_RECONNECT_INTERVAL_MS) {
      startAlexa();
    }
    if (alexaReady_) {
      espalexa_.loop();
    }
  } else if (millis() - lastConnectAttemptMs_ >= Config::WIFI_RECONNECT_INTERVAL_MS) {
    Serial.printf("[WARN] Wi-Fi not connected (status %d); retrying.\n", WiFi.status());
    if (!WiFi.disconnect(false, false, 0)) {
      Serial.println(F("[WARN] Wi-Fi disconnect failed; attempting to connect again."));
    }
    connectWifi();
  }
}

void AlexaController::update() {
  if (!configured_) {
    return;
  }

  syncStates();
  updateNetwork();
  dispatchPendingCommand();
}
