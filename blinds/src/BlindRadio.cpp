#include "BlindRadio.h"

#include <SPI.h>

#include "Secrets.h"

static_assert(configNUM_CORES == 2 && ARDUINO_RUNNING_CORE == 0,
              "RF transmission requires a dual-core ESP32 with the Arduino loop on core 0.");

BlindRadio::BlindRadio(const TimingSettings& timing)
    : timing_(timing), radio_(Config::RADIO_CE_PIN, Config::RADIO_CSN_PIN) {}

bool BlindRadio::begin() {
  if (!radio_.begin()) {
    return false;
  }

  radio_.setAutoAck(false);
  radio_.setRetries(0, 0);
  radio_.setAddressWidth(sizeof(Config::RADIO_ADDRESS));
  radio_.setPayloadSize(5);
  radio_.disableDynamicPayloads();
  radio_.setCRCLength(RF24_CRC_16);
  radio_.setDataRate(RF24_2MBPS);
  radio_.setPALevel(RF24_PA_MAX);
  radio_.openReadingPipe(1, Config::RADIO_ADDRESS);
  radio_.setChannel(Config::RADIO_CHANNELS[receiveChannel_]);
  radio_.startListening();
  lastHopMs_ = millis();

  if (xTaskCreatePinnedToCore(transmitTask, "Blind RF", 4096, this, 1, &txTask_, 1) != pdPASS) {
    Serial.println(F("[ERR] Could not create the RF transmitter task."));
    return false;
  }
  return true;
}

bool BlindRadio::isTransmitting() const { return txState_.load() == TxState::Transmitting; }

bool BlindRadio::takeTxResult(TxResult& result) {
  const TxState state = txState_.load();
  if (state != TxState::Succeeded && state != TxState::Failed) {
    return false;
  }
  result = {state == TxState::Succeeded, txFinishedAtMs_};
  txState_.store(TxState::Idle);
  return true;
}

void BlindRadio::transmitTask(void* context) {
  // FreeRTOS starts a plain function; context points back to this BlindRadio instance.
  static_cast<BlindRadio*>(context)->runTransmitter();
}

void BlindRadio::runTransmitter() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!isTransmitting()) {
      Serial.println(F("[ERR] RF transmitter woke without a pending press."));
      continue;
    }

    const TxRequest request = txRequest_;
    const bool succeeded = transmitPress(request);
    txFinishedAtMs_ = millis();
    if (!succeeded) {
      Serial.printf("[ERR] RF transmission failed for mask 0x%02X; position is uncertain.\n",
                    request.mask);
    }
    // Publish completion only after all RF24 access and RX restoration have finished.
    txState_.store(succeeded ? TxState::Succeeded : TxState::Failed);
  }
}

bool BlindRadio::transmitPacket(const uint8_t* packet, uint32_t durationMs) {
  const uint32_t startedAtMs = millis();
  uint8_t channel = 0;

  while (millis() - startedAtMs < durationMs) {
    radio_.setChannel(Config::RADIO_CHANNELS[channel]);
    bool written = true;
    for (uint8_t i = 0; i < Config::PACKETS_PER_BURST && written; i++) {
      written = radio_.writeFast(packet, 5, true);
    }
    const bool drained = radio_.txStandBy();
    if (!written || !drained) {
      radio_.stopListening();
      radio_.flush_tx();
      return false;
    }
    channel = (channel + 1) % Config::RADIO_CHANNEL_COUNT;
  }
  return true;
}

bool BlindRadio::sendPress(uint8_t mask, Direction direction, const char* reason,
                           uint32_t waitedMs) {
  if (txTask_ == nullptr) {
    Serial.println(F("[ERR] RF transmitter is not initialized; command ignored."));
    return false;
  }
  if (txState_.load() != TxState::Idle) {
    Serial.println(F("[ERR] RF transmitter busy; pulse not sent. Please retry."));
    return false;
  }

  const bool up = direction == Direction::Up;
  if (waitedMs == 0) {
    Serial.printf("[TX] Mask 0x%02X -> %s | %s (immediate)\n", mask, up ? "UP" : "DOWN", reason);
  } else {
    Serial.printf("[TX] Mask 0x%02X -> %s | %s (after %lu ms wait)\n", mask, up ? "UP" : "DOWN",
                  reason, waitedMs);
  }

  // The main loop may change timings while TX runs; each press uses its own snapshot.
  txRequest_ = {mask, direction, timing_.activeHoldMs, timing_.releaseHoldMs};
  txState_.store(TxState::Transmitting);
  xTaskNotifyGive(txTask_);
  return true;
}

bool BlindRadio::transmitPress(const TxRequest& request) {
  const bool up = request.direction == Direction::Up;
  const uint8_t pressPacket[] = {Secrets::REMOTE_ID[0], Secrets::REMOTE_ID[1],
                                 Secrets::REMOTE_ID[2], request.mask, uint8_t(up ? 0xA1 : 0xA2)};
  const uint8_t releasePacket[] = {Secrets::REMOTE_ID[0], Secrets::REMOTE_ID[1],
                                   Secrets::REMOTE_ID[2], request.mask, uint8_t(up ? 0x21 : 0x22)};

  digitalWrite(Config::STATUS_LED_PIN, HIGH);
  radio_.stopListening();
  radio_.openWritingPipe(Config::RADIO_ADDRESS);
  const bool pressed = transmitPacket(pressPacket, request.activeHoldMs);
  // Attempt release even if the active phase failed.
  const bool released = transmitPacket(releasePacket, request.releaseHoldMs);
  radio_.openReadingPipe(1, Config::RADIO_ADDRESS);
  radio_.setChannel(Config::RADIO_CHANNELS[receiveChannel_]);
  radio_.startListening();
  lastHopMs_ = millis();
  digitalWrite(Config::STATUS_LED_PIN, LOW);
  return pressed && released;
}

void BlindRadio::toggleSniffMode() {
  sniffMode_ = sniffMode_ == SniffMode::Filtered ? SniffMode::Promiscuous : SniffMode::Filtered;
  printSniffMode();
}

void BlindRadio::printSniffMode() const {
  Serial.printf("Sniff mode: %s\n", sniffMode_ == SniffMode::Filtered
                                        ? "FILTERED (configured remote only)"
                                        : "PROMISCUOUS (all remote IDs)");
}

void BlindRadio::poll() {
  // Only the main loop starts TX, and the worker returns ownership after restoring RX.
  if (isTransmitting()) {
    return;
  }

  while (radio_.available()) {
    uint8_t packet[5];
    radio_.read(packet, sizeof(packet));
    const bool knownRemote = packet[0] == Secrets::REMOTE_ID[0] &&
                             packet[1] == Secrets::REMOTE_ID[1] &&
                             packet[2] == Secrets::REMOTE_ID[2];
    if (sniffMode_ == SniffMode::Promiscuous) {
      Serial.printf("[RX %lu] Ch:%u | ID:%02X:%02X:%02X | Mask:0x%02X | Cmd:0x%02X (%s)\n",
                    millis(), Config::RADIO_CHANNELS[receiveChannel_], packet[0], packet[1],
                    packet[2], packet[3], packet[4], knownRemote ? "KNOWN" : "NEW");
    } else if (knownRemote) {
      Serial.printf("[RX %lu] Ch:%u | Mask:0x%02X | Cmd:0x%02X\n", millis(),
                    Config::RADIO_CHANNELS[receiveChannel_], packet[3], packet[4]);
    }
  }

  // Drain packets before hopping so their logged channel is the one that received them.
  if (millis() - lastHopMs_ >= Config::RECEIVE_HOP_INTERVAL_MS) {
    receiveChannel_ = (receiveChannel_ + 1) % Config::RADIO_CHANNEL_COUNT;
    radio_.stopListening();
    radio_.setChannel(Config::RADIO_CHANNELS[receiveChannel_]);
    radio_.startListening();
    lastHopMs_ = millis();
  }
}
