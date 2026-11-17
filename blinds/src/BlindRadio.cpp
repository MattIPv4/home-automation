#include "BlindRadio.h"

#include <SPI.h>

#include "Secrets.h"

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
  return true;
}

void BlindRadio::transmitPacket(const uint8_t* packet, uint32_t durationMs) {
  const uint32_t startedAtMs = millis();
  uint8_t channel = 0;

  while (millis() - startedAtMs < durationMs) {
    radio_.setChannel(Config::RADIO_CHANNELS[channel]);
    for (uint8_t i = 0; i < Config::PACKETS_PER_BURST; i++) {
      radio_.writeFast(packet, 5, true);
    }
    radio_.txStandBy();
    channel = (channel + 1) % Config::RADIO_CHANNEL_COUNT;
  }
}

void BlindRadio::sendPress(uint8_t mask, Direction direction, const char* reason,
                           uint32_t waitedMs) {
  const bool up = direction == Direction::Up;
  const uint8_t pressPacket[] = {Secrets::REMOTE_ID[0], Secrets::REMOTE_ID[1],
                                 Secrets::REMOTE_ID[2], mask, uint8_t(up ? 0xA1 : 0xA2)};
  const uint8_t releasePacket[] = {Secrets::REMOTE_ID[0], Secrets::REMOTE_ID[1],
                                   Secrets::REMOTE_ID[2], mask, uint8_t(up ? 0x21 : 0x22)};

  digitalWrite(Config::STATUS_LED_PIN, HIGH);
  if (waitedMs == 0) {
    Serial.printf("[TX] Mask 0x%02X -> %s | %s (immediate)\n", mask, up ? "UP" : "DOWN", reason);
  } else {
    Serial.printf("[TX] Mask 0x%02X -> %s | %s (after %lu ms wait)\n", mask, up ? "UP" : "DOWN",
                  reason, waitedMs);
  }

  radio_.stopListening();
  radio_.openWritingPipe(Config::RADIO_ADDRESS);
  transmitPacket(pressPacket, timing_.activeHoldMs);
  transmitPacket(releasePacket, timing_.releaseHoldMs);
  radio_.openReadingPipe(1, Config::RADIO_ADDRESS);
  radio_.setChannel(Config::RADIO_CHANNELS[receiveChannel_]);
  radio_.startListening();
  lastHopMs_ = millis();
  digitalWrite(Config::STATUS_LED_PIN, LOW);
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
