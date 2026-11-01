#ifndef BLFLOW_PRINTER_ERROR_H
#define BLFLOW_PRINTER_ERROR_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "types.h"

// HMS severity, from the code's top 16 bits: 1 Fatal, 2 Serious, 3 Common, 4 Info, otherwise none.
inline int hmsSeverity(uint64_t code) {
    const int severity = static_cast<uint32_t>(code) >> 16;
    return severity >= 1 && severity <= 4 ? severity : 0;
}

inline const char *hmsSeverityName(int severity) {
    switch (severity) {
        case 1: return "Fatal";
        case 2: return "Serious";
        case 3: return "Common";
        case 4: return "Info";
    }
    return "";
}

// The form the Bambu Lab wiki and the BLLED controller's HMS ignore list use.
inline String hmsCodeText(uint64_t code) {
    char text[24];
    snprintf(text, sizeof(text), "HMS_%04X_%04X_%04X_%04X", static_cast<unsigned>(code >> 48),
             static_cast<unsigned>((code >> 32) & 0xFFFF), static_cast<unsigned>((code >> 16) & 0xFFFF),
             static_cast<unsigned>(code & 0xFFFF));
    return text;
}

// Keeps the most severe code of a report's HMS list; an empty list clears it.
inline void recordHms(JsonArrayConst hms) {
    uint64_t worst = 0;
    for (JsonVariantConst entry : hms) {
        if (!entry["attr"].is<uint32_t>() || !entry["code"].is<uint32_t>()) continue;
        const uint64_t code = (static_cast<uint64_t>(entry["attr"].as<uint32_t>()) << 32) | entry["code"].as<uint32_t>();
        const int severity = hmsSeverity(code);
        if (severity && (!worst || severity < hmsSeverity(worst))) worst = code;
    }
    printerVariables.hmsCode = worst;
}

// Print stages the printer pauses in for a fault: filament runout, front cover removed,
// and nozzle or bed temperature failure.
inline bool printerErrorStage(int stage) {
    return stage == 6 || stage == 17 || stage == 20 || stage == 21;
}

// The faults the BLLED controller shows in an error colour: those stages, or a Fatal or Serious HMS code.
inline bool printerError() {
    const int severity = hmsSeverity(printerVariables.hmsCode);
    return printerVariables.online && (printerErrorStage(printerVariables.printStage) || severity == 1 || severity == 2);
}

#endif
