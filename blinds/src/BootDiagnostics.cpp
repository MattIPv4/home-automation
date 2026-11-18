#include "BootDiagnostics.h"

#include <Arduino.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "Config.h"

namespace {
constexpr size_t BLOCK_BYTES = 4096;
constexpr size_t MAX_BLOCKS = 64;
constexpr size_t RESERVED_BYTES = 32768;
constexpr uint32_t RAM_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

struct RamCheckTask {
  TaskHandle_t caller = nullptr;
  std::atomic<bool> passed{false};
};

// Static lifetime also keeps the worker's context valid if it times out.
RamCheckTask ramTask;

bool checkPattern(uint32_t* const* blocks, size_t count, uint32_t pattern) {
  for (size_t block = 0; block < count; block++) {
    volatile uint32_t* words = blocks[block];
    for (size_t i = 0; i < BLOCK_BYTES / sizeof(uint32_t); i++) {
      words[i] = pattern;
    }
  }
  for (size_t block = 0; block < count; block++) {
    volatile uint32_t* words = blocks[block];
    for (size_t i = 0; i < BLOCK_BYTES / sizeof(uint32_t); i++) {
      const uint32_t actual = words[i];
      if (actual != pattern) {
        Serial.printf("[ERR] RAM core %u at %p: wrote 0x%08lx, read 0x%08lx.\n", xPortGetCoreID(),
                      static_cast<void*>(&blocks[block][i]), static_cast<unsigned long>(pattern),
                      static_cast<unsigned long>(actual));
        return false;
      }
    }
  }
  // Allow the idle task and watchdog to run between patterns.
  delay(1);
  return true;
}

bool checkRamOnCurrentCore() {
  uint32_t* blocks[MAX_BLOCKS]{};
  size_t count = 0;
  bool passed = true;
  while (count < MAX_BLOCKS && heap_caps_get_free_size(RAM_CAPS) >= RESERVED_BYTES + BLOCK_BYTES &&
         heap_caps_get_largest_free_block(RAM_CAPS) >= BLOCK_BYTES) {
    blocks[count] = static_cast<uint32_t*>(heap_caps_malloc(BLOCK_BYTES, RAM_CAPS));
    if (blocks[count] == nullptr) {
      Serial.println(F("[ERR] Could not allocate RAM check buffer."));
      passed = false;
      break;
    }
    count++;
  }
  if (count == 0) {
    Serial.println(F("[ERR] Not enough free internal RAM to run the boot check."));
    passed = false;
  }
  const uint32_t patterns[] = {0, UINT32_MAX, 0xaaaaaaaa, 0x55555555};
  for (uint32_t pattern : patterns) {
    if (passed) {
      passed = checkPattern(blocks, count, pattern);
    }
  }
  for (unsigned bit = 0; passed && bit < 32; bit++) {
    const uint32_t pattern = uint32_t{1} << bit;
    passed = checkPattern(blocks, count, pattern) && checkPattern(blocks, count, ~pattern);
  }
  for (size_t block = 0; block < count; block++) {
    heap_caps_free(blocks[block]);
  }
  if (passed) {
    Serial.printf("[BOOT] RAM patterns passed on core %u (%u bytes tested).\n", xPortGetCoreID(),
                  static_cast<unsigned>(count * BLOCK_BYTES));
  }
  return passed;
}

void checkRamOnSecondCore(void* context) {
  auto* task = static_cast<RamCheckTask*>(context);
  task->passed = checkRamOnCurrentCore();
  xTaskNotifyGive(task->caller);
  vTaskDelete(nullptr);
}
}

void BootDiagnostics::printResetReason() {
  const esp_reset_reason_t reason = esp_reset_reason();
  const char* name = "unknown";
  switch (reason) {
  case ESP_RST_POWERON:
    name = "power on";
    break;
  case ESP_RST_EXT:
    name = "external reset";
    break;
  case ESP_RST_SW:
    name = "software restart";
    break;
  case ESP_RST_PANIC:
    name = "panic";
    break;
  case ESP_RST_INT_WDT:
  case ESP_RST_TASK_WDT:
  case ESP_RST_WDT:
    name = "watchdog";
    break;
  case ESP_RST_DEEPSLEEP:
    name = "deep sleep";
    break;
  case ESP_RST_BROWNOUT:
    name = "brownout";
    break;
  default:
    break;
  }
  Serial.printf("[BOOT] Reset reason: %s (%d).\n", name, static_cast<int>(reason));
}

bool BootDiagnostics::checkRam() {
  Serial.println(F("[BOOT] Checking free internal RAM on both cores..."));
  if (!heap_caps_check_integrity_all(true) || !checkRamOnCurrentCore()) {
    Serial.println(F("[ERR] Boot RAM check failed."));
    return false;
  }
  // Core 0's buffers are freed before reserving the worker's stack and test buffers.
  ramTask.caller = xTaskGetCurrentTaskHandle();
  ramTask.passed = false;
  if (xTaskCreatePinnedToCore(checkRamOnSecondCore, "Boot RAM", 4096, &ramTask, 1, nullptr, 1) !=
      pdPASS) {
    Serial.println(F("[ERR] Could not create the core 1 RAM check task."));
    return false;
  }
  if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000)) == 0) {
    Serial.println(F("[ERR] Core 1 RAM check timed out."));
    return false;
  }
  if (!ramTask.passed || !heap_caps_check_integrity_all(true)) {
    Serial.println(F("[ERR] Boot RAM check failed."));
    return false;
  }
  return true;
}

[[noreturn]] void BootDiagnostics::halt() {
  Serial.println(F("[ERR] Boot stopped; fix the fault and reset. LED will flash."));
  for (;;) {
    digitalWrite(Config::STATUS_LED_PIN, !digitalRead(Config::STATUS_LED_PIN));
    delay(250);
  }
}
