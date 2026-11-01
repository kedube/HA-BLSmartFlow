#ifndef BLFLOW_CONTROL_MODE_H
#define BLFLOW_CONTROL_MODE_H

#include <cstring>

// Who drives the fans while Home Assistant is connected, as on the BLLED controller.
// Printer: the fans follow their Settings. HomeAssistant: they run at the speeds Home
// Assistant sets. Hybrid: they follow their Settings until Home Assistant sets a speed,
// which holds until the next print starts or Home Assistant resumes printer control.
enum class ControlMode { Printer, HomeAssistant, Hybrid };
constexpr int controlModeCount = 3;

// The settings file and web pages' names.
inline const char *controlModeName(ControlMode mode) {
    switch (mode) {
        case ControlMode::Printer: return "printer";
        case ControlMode::HomeAssistant: return "homeassistant";
        case ControlMode::Hybrid: return "hybrid";
    }
    return "hybrid";
}

inline bool readControlMode(const char *name, ControlMode &mode) {
    if (!name) return false;
    for (int i = 0; i < controlModeCount; ++i) {
        const auto candidate = static_cast<ControlMode>(i);
        if (strcmp(name, controlModeName(candidate)) == 0) {
            mode = candidate;
            return true;
        }
    }
    return false;
}

#endif
