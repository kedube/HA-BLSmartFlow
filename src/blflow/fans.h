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
    globalVariables.fanSpeed = speed1;
    globalVariables.fanSpeed2 = speed2;
    analogWrite(FanPower1, map(speed1, 0, 100, 0, 255));
    analogWrite(FanPower2, map(speed2, 0, 100, 0, 255));
}

// Run Settings that have already been saved to flash, from the web page or Home Assistant.
void applySettings(PrinterConfig &&candidate) {
    cancelChangedPostPrint(printerConfig, candidate);
    printerConfig = std::move(candidate);
    ++globalVariables.settingsRevision;
    fanloop();
}

#endif
