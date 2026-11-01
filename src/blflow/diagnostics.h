#ifndef _BLFLOW_DIAGNOSTICS
#define _BLFLOW_DIAGNOSTICS

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include "types.h"

// Memory that ordinary allocations (String, JSON, TLS buffers) can use. ESP.getFreeHeap() and
// ESP.getMaxAllocHeap() also count the leftover instruction RAM, about 50 kB, which takes only
// 32-bit accesses, so they overstate it.
inline uint32_t freeMemory() { return heap_caps_get_free_size(MALLOC_CAP_8BIT); }
inline uint32_t minFreeMemory() { return heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT); }
inline uint32_t largestFreeBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }

// Wi-Fi transmit powers offered under Debug & Diagnostics, in the radio's quarter-dBm steps.
const int wifiTxPowers[] = {78, 68, 60, 52, 44, 34, 20, 8};

inline bool validWifiTxPower(int power) {
    for (int allowed : wifiTxPowers) {
        if (power == allowed) return true;
    }
    return false;
}

// Needs a started radio, so it runs after connecting and whenever the settings are saved.
void applyWifiRadioSettings() {
    WiFi.setTxPower(static_cast<wifi_power_t>(printerConfig.wifiTxPower));
    WiFi.setSleep(printerConfig.wifiSleep);
}

// Printer reports in the last full minute, for the Runtime Status panel.
struct TrafficCounter {
    uint32_t minuteStart = 0, messages = 0, bytes = 0;
    uint32_t lastMessages = 0, lastBytes = 0, lastReport = 0;
    bool reported = false;

    void roll(uint32_t now) {
        if (now - minuteStart < 60000) return;
        const bool quietMinute = now - minuteStart >= 120000; // No report in the latest full minute
        lastMessages = quietMinute ? 0 : messages;
        lastBytes = quietMinute ? 0 : bytes;
        messages = bytes = 0;
        minuteStart = now;
    }

    void add(uint32_t length, uint32_t now) {
        roll(now);
        ++messages;
        bytes += length;
        lastReport = now;
        reported = true;
    }
};
TrafficCounter printerTraffic;

// Tells a crash, brownout, or watchdog reset apart from a power cycle or a requested restart.
const char *resetReasonName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power on";
        case ESP_RST_EXT: return "reset pin";
        case ESP_RST_SW: return "restart";
        case ESP_RST_PANIC: return "crash";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        default: return "unknown";
    }
}

#endif
