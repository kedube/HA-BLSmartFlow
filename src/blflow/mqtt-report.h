#ifndef BLFLOW_MQTT_REPORT_H
#define BLFLOW_MQTT_REPORT_H

#include <ArduinoJson.h>
#include "log-serial.h"
#include "types.h"
#include "post-print.h"
#include "printer-error.h"

inline bool readUnsignedText(const char *text, uint32_t maximum, uint32_t &result) {
    if (!text || !*text) return false;
    uint32_t number = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        const uint32_t digit = *text - '0';
        if (number > (maximum - digit) / 10) return false;
        number = number * 10 + digit;
    }
    if (number > maximum) return false;
    result = number;
    return true;
}

// MQTT uses both JSON numbers and decimal strings. Reject malformed values
// rather than converting them to zero and unexpectedly stopping an output.
inline bool readUnsignedReport(JsonVariantConst value, uint32_t maximum, uint32_t &result) {
    if (value.is<uint32_t>()) {
        const uint32_t number = value.as<uint32_t>();
        if (number > maximum) return false;
        result = number;
        return true;
    }
    return readUnsignedText(value.as<const char *>(), maximum, result);
}

inline void recordPrinterFan(int fan, uint32_t raw, uint32_t scale) {
    int percent;
    if (scale == 255) {
        // Match Bambu Studio's round(PWM / 25.5) * 10 display.
        // E.g. PWM 180 represents 70% in the printer UI, not 71%.
        percent = ((raw * 10 + 127) / 255) * 10;
    } else if (fan != static_cast<int>(PrinterFanSource::Heatbreak)) {
        // DevFan::ParseV1_0 maps legacy 0-15 fields using floor(raw / 1.5).
        percent = (raw * 2 / 3) * 10;
    } else {
        // Heatbreak has a separate 0-15 reading and no Studio 10-step control.
        percent = (raw * 100 + scale / 2) / scale;
    }
    printerVariables.fans[fan].percent = percent;
    printerVariables.fans[fan].known = true;
}

inline bool parsePrinterReport(const uint8_t *payload, size_t length) {
    JsonDocument filter;
    filter["print"]["fan_gear"] = true;
    filter["print"]["nozzle_temper"] = true;
    filter["print"]["gcode_state"] = true;
    filter["print"]["stg_cur"] = true;
    filter["print"]["hms"] = true;
    const char *fields[] = {"cooling_fan_speed", "big_fan1_speed", "big_fan2_speed", "heatbreak_fan_speed"};
    for (const char *field : fields) filter["print"][field] = true;
    JsonDocument message;
    if (deserializeJson(message, payload, length, DeserializationOption::Filter(filter))) {
        LogSerial.println(F("Deserialize error while parsing MQTT"));
        return false;
    }
    if (printerConfig.mqttdebug) {
        LogSerial.print(F("(Filtered) MQTT payload: "));
        serializeJson(message, LogSerial);
        LogSerial.println();
    }

    JsonObjectConst report = message["print"].as<JsonObjectConst>();
    bool changed = recordPrintState(report["gcode_state"].as<const char *>());
    uint32_t gear;
    // Bambu Studio's DevFan::ParseV1_0 uses 8-bit PWM values in fan_gear.
    // Prefer packed readings, following the same precedence as Bambu Studio.
    const bool hasGear = readUnsignedReport(report["fan_gear"], UINT32_MAX, gear);
    if (hasGear) {
        for (int fan = 0; fan < 3; ++fan) recordPrinterFan(fan, (gear >> (8 * fan)) & 0xff, 255);
        changed = true;
    }
    for (int fan = 0; fan < printerFanCount; ++fan) {
        if (hasGear && fan < 3) continue;
        uint32_t raw;
        if (readUnsignedReport(report[fields[fan]], 15, raw)) {
            recordPrinterFan(fan, raw, 15);
            changed = true;
        }
    }
    if (report["nozzle_temper"].is<double>()) {
        const double temperature = report["nozzle_temper"].as<double>();
        if (std::isfinite(temperature)) {
            printerVariables.nozzletemp = temperature;
            changed = true;
        }
    }
    // For Home Assistant's Printer error sensor. Delta updates without these keep the last values.
    const bool wasError = printerError();
    const uint64_t previousHms = printerVariables.hmsCode;
    if (report["stg_cur"].is<int>()) printerVariables.printStage = report["stg_cur"].as<int>();
    if (report["hms"].is<JsonArrayConst>()) recordHms(report["hms"].as<JsonArrayConst>());
    if (printerError() != wasError || printerVariables.hmsCode != previousHms) {
        LogSerial.print(F("Printer error: "));
        LogSerial.print(printerError() ? F("yes") : F("no"));
        if (printerVariables.hmsCode) {
            LogSerial.print(F(", "));
            LogSerial.print(hmsSeverityName(hmsSeverity(printerVariables.hmsCode)));
            LogSerial.print(F(" "));
            LogSerial.print(hmsCodeText(printerVariables.hmsCode));
        }
        LogSerial.print(F(", stage "));
        LogSerial.println(printerVariables.printStage);
        changed = true;
    }
    // Missing fields are delta updates; retain each fan's last valid reading,
    // including through disconnects. Readings start at 0% after boot.
    return changed;
}

#endif
