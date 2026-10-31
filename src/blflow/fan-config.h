#ifndef BLFLOW_FAN_CONFIG_H
#define BLFLOW_FAN_CONFIG_H

#include <ArduinoJson.h>
#include "types.h"

inline void writeFanCurve(JsonArray array, const FanCurve &curve) {
    for (const auto &point : curve) {
        JsonObject item = array.add<JsonObject>();
        item["temp"] = point.first;
        item["speed"] = point.second;
    }
}

inline bool readFanCurve(JsonVariantConst value, FanCurve &curve) {
    if (!value.is<JsonArrayConst>()) return false;
    FanCurve parsed;
    for (JsonVariantConst point : value.as<JsonArrayConst>()) {
        if (!point["temp"].is<float>() || !point["speed"].is<int>()) return false;
        parsed.emplace_back(point["temp"].as<float>(), point["speed"].as<int>());
    }
    if (!normalizeFanCurve(parsed)) return false;
    curve = std::move(parsed);
    return true;
}

inline void initializeSecondFan(PrinterConfig &config) {
    if (config.fanGraph2.empty()) {
        config.fanGraph2 = config.fanGraph;
        config.staticFan2 = config.staticFan;
        config.staticFanSpeed2 = config.staticFanSpeed;
        config.followPrinterFan2 = config.followPrinterFan;
        config.printerFanSource2 = config.printerFanSource;
        config.postPrint2 = config.postPrint;
    }
}

// One output's mode settings. Fan 2's are dormant while the fans are synchronized.
struct FanSettings {
    bool &staticEnabled;
    int &staticSpeed;
    bool &followPrinterFan;
    PrinterFanSource &printerFanSource;
    PostPrintSettings &postPrint;
};

inline FanSettings fanSettings(PrinterConfig &config, int fan) {
    if (fan == 1) return {config.staticFan2, config.staticFanSpeed2, config.followPrinterFan2,
                          config.printerFanSource2, config.postPrint2};
    return {config.staticFan, config.staticFanSpeed, config.followPrinterFan,
            config.printerFanSource, config.postPrint};
}

inline void readPostPrintSettings(JsonVariantConst enabled, JsonVariantConst seconds,
                                  JsonVariantConst speed, PostPrintSettings &settings) {
    // Missing fields in older files keep the feature off. Invalid values cannot
    // activate a timer or produce an unsafe duration after a firmware upgrade.
    const bool validSeconds = seconds.is<int>() && seconds.as<int>() >= 1 && seconds.as<int>() <= postPrintMaxSeconds;
    const bool validSpeed = speed.is<int>() && speed.as<int>() >= 0 && speed.as<int>() <= 100;
    if (validSeconds) settings.seconds = seconds.as<int>();
    if (validSpeed) settings.speed = speed.as<int>();
    settings.enabled = enabled.is<bool>() && enabled.as<bool>() && validSeconds && validSpeed;
}

#endif
