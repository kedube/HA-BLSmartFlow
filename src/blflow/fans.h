#ifndef _FAN
#define _FAN

#include "types.h"
#include "post-print.h"

const int FanPower1 = 17;
const int FanPower2 = 16;

void fansetup() {
    pinMode(FanPower1, OUTPUT);
    pinMode(FanPower2, OUTPUT);
}

int outputFanSpeed(bool staticEnabled, int staticSpeed, bool followEnabled,
                   PrinterFanSource source, const FanCurve &curve, float temperature) {
    if (staticEnabled) return clampFanSpeed(staticSpeed);
    if (followEnabled) return clampFanSpeed(printerVariables.fans[static_cast<int>(source)].percent);
    return calculateFanSpeed(curve, temperature);
}

// Home Assistant drives the fans in its own mode, and in Hybrid once it has set a speed.
// Turning off the connection returns them to the printer.
bool homeAssistantControlsFans(const PrinterConfig &config = printerConfig) {
    if (!config.haEnabled) return false;
    return config.controlMode == ControlMode::HomeAssistant ||
        (config.controlMode == ControlMode::Hybrid && globalVariables.haOverride);
}

// Home Assistant takes over at the current speeds, so a fan it has not set keeps running as it was.
void holdCurrentSpeeds(PrinterConfig &config) {
    config.haFanSpeed = globalVariables.fanSpeed;
    config.haFanSpeed2 = globalVariables.fanSpeed2;
}

void setControlMode(PrinterConfig &config, ControlMode mode) {
    if (mode == ControlMode::HomeAssistant && !homeAssistantControlsFans()) holdCurrentSpeeds(config);
    config.controlMode = mode;
}

void fanloop(uint32_t now = millis()) {
    updatePostPrint(now);
    const float temperature = printerVariables.nozzletemp;
    int speed1 = outputFanSpeed(printerConfig.staticFan, printerConfig.staticFanSpeed,
        printerConfig.followPrinterFan, printerConfig.printerFanSource, printerConfig.fanGraph, temperature);
    if (printerVariables.postPrint[0].active) speed1 = printerVariables.postPrint[0].speed;
    int speed2 = speed1;
    if (!printerConfig.fansSynchronized) {
        speed2 = outputFanSpeed(printerConfig.staticFan2, printerConfig.staticFanSpeed2,
            printerConfig.followPrinterFan2, printerConfig.printerFanSource2, printerConfig.fanGraph2, temperature);
        if (printerVariables.postPrint[1].active) speed2 = printerVariables.postPrint[1].speed;
    }
    // Takes priority over Post Print too; synchronized fans both use Fan 1's speed.
    if (homeAssistantControlsFans()) {
        speed1 = clampFanSpeed(printerConfig.haFanSpeed);
        speed2 = printerConfig.fansSynchronized ? speed1 : clampFanSpeed(printerConfig.haFanSpeed2);
    }
    globalVariables.fanSpeed = speed1;
    globalVariables.fanSpeed2 = speed2;
    analogWrite(FanPower1, map(speed1, 0, 100, 0, 255));
    analogWrite(FanPower2, map(speed2, 0, 100, 0, 255));
}

// Run Settings that have already been saved to flash, from the web page or Home Assistant.
void applySettings(PrinterConfig &&candidate) {
    cancelChangedPostPrint(printerConfig, candidate);
    // A Hybrid override ends when the mode changes, so returning to Hybrid hands the fans to the printer.
    if (candidate.controlMode != printerConfig.controlMode || !candidate.haEnabled) globalVariables.haOverride = false;
    printerConfig = std::move(candidate);
    ++globalVariables.settingsRevision;
    fanloop();
}

#endif
