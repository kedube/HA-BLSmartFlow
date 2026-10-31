#ifndef _BLLEDFILESYSTEM
#define _BLLEDFILESYSTEM

#include <WiFi.h>
#include "FS.h"

#include <Arduino.h>
#include <LittleFS.h>
#include "types.h"
#include "fan-config.h"
#include <memory>

const char *configPath = "/blledconfig.json";

bool saveFileSystem(const PrinterConfig &config = printerConfig){
    Serial.println(F("Saving config"));
    
    JsonDocument json;
    json["ssid"] = globalVariables.SSID;
    json["appw"] = globalVariables.APPW;
    json["printerIp"] = config.printerIP;
    json["accessCode"] = config.accessCode;
    json["serialNumber"] = config.serialNumber;
    
    json["bssi"] = config.BSSID;
    // Debugging
    json["debuging"] = config.debuging;
    json["debugingchange"] = config.debugingchange;
    json["mqttdebug"] = config.mqttdebug;
    json["fansSynchronized"] = config.fansSynchronized;
    json["staticFan"] = config.staticFan;
    json["staticFanSpeed"] = config.staticFanSpeed;
    json["followPrinterFan"] = config.followPrinterFan;
    json["printerFanSource"] = printerFanSourceName(config.printerFanSource);
    json["postPrint"] = config.postPrint.enabled;
    json["postPrintSeconds"] = config.postPrint.seconds;
    json["postPrintSpeed"] = config.postPrint.speed;
    writeFanCurve(json["fanPoints"].to<JsonArray>(), config.fanGraph);
    if (!config.fanGraph2.empty()) {
        writeFanCurve(json["fanPoints2"].to<JsonArray>(), config.fanGraph2);
        json["staticFan2"] = config.staticFan2;
        json["staticFanSpeed2"] = config.staticFanSpeed2;
        json["followPrinterFan2"] = config.followPrinterFan2;
        json["printerFanSource2"] = printerFanSourceName(config.printerFanSource2);
        json["postPrint2"] = config.postPrint2.enabled;
        json["postPrintSeconds2"] = config.postPrint2.seconds;
        json["postPrintSpeed2"] = config.postPrint2.speed;
    }
    json["haEnabled"] = config.haEnabled;
    json["haHost"] = config.haHost;
    json["haPort"] = config.haPort;
    json["haUser"] = config.haUser;
    json["haPassword"] = config.haPassword;
    json["webUser"] = config.webUser;
    json["webPassword"] = config.webPassword;
    if (json.overflowed()) {
        Serial.println(F("Not enough memory to save config"));
        return false;
    }

    // Replace the saved config only after the complete new file is written.
    const char *tempPath = "/blledconfig.json.tmp";
    File configFile = LittleFS.open(tempPath, "w");
    if (!configFile) {
        Serial.println(F("Failed to save config"));
        return false;
    }
    const size_t bytesWritten = serializeJson(json, configFile);
    configFile.close();
    if (bytesWritten != measureJson(json)) {
        LittleFS.remove(tempPath);
        Serial.println(F("Failed to write complete config"));
        return false;
    }
    if (!LittleFS.rename(tempPath, configPath)) {
        LittleFS.remove(tempPath);
        Serial.println(F("Failed to replace config"));
        return false;
    }
    Serial.println(F("Config Saved"));
    return true;
}

void loadFileSystem(){
    Serial.println(F("Loading config"));
    
    File configFile;
    int attempts = 0;
    while (attempts < 2) {
        configFile = LittleFS.open(configPath, "r");
        if (configFile) {
            break;
        }
        attempts++;
        Serial.println(F("Failed to open config file, retrying.."));
        delay(2000);
    }
    if (!configFile) {
        Serial.print(F("Failed to open config file after "));
        Serial.print(attempts);
        Serial.println(F(" retries"));
        
        Serial.println(F("Keeping defaults; config file left unchanged"));
        return;
    }
    size_t size = configFile.size();
    std::unique_ptr<char[]> buf(new char[size]);
    if (configFile.readBytes(buf.get(), size) != size) {
        Serial.println(F("Incomplete config read; file left unchanged"));
        configFile.close();
        return;
    }

    JsonDocument json;
    auto deserializeError = deserializeJson(json, buf.get(), size);

    if (!deserializeError) {
        PrinterConfig loaded{};
        snprintf(globalVariables.SSID, sizeof(globalVariables.SSID), "%s", json["ssid"] | "");
        snprintf(globalVariables.APPW, sizeof(globalVariables.APPW), "%s", json["appw"] | "");
        snprintf(loaded.printerIP, sizeof(loaded.printerIP), "%s", json["printerIp"] | "");
        snprintf(loaded.accessCode, sizeof(loaded.accessCode), "%s", json["accessCode"] | "");
        snprintf(loaded.serialNumber, sizeof(loaded.serialNumber), "%s", json["serialNumber"] | "");
        snprintf(loaded.BSSID, sizeof(loaded.BSSID), "%s", json["bssi"] | "");
        loaded.debuging = json["debuging"] | loaded.debuging;
        loaded.debugingchange = json["debugingchange"] | loaded.debugingchange;
        loaded.mqttdebug = json["mqttdebug"] | false;
        loaded.fansSynchronized = json["fansSynchronized"] | true;
        loaded.staticFan = json["staticFan"] | false;
        loaded.staticFanSpeed = clampFanSpeed(json["staticFanSpeed"] | 0);
        loaded.followPrinterFan = (json["followPrinterFan"] | false) && !loaded.staticFan;
        readPostPrintSettings(json["postPrint"], json["postPrintSeconds"], json["postPrintSpeed"], loaded.postPrint);
        if (!readPrinterFanSource(json["printerFanSource"], loaded.printerFanSource)) {
            loaded.followPrinterFan = false;
        }
        if (!readFanCurve(json["fanPoints"], loaded.fanGraph)) {
            Serial.println(F("Missing or invalid Fan 1 curve; using default curve"));
        }
        if (!json["fanPoints2"].isNull()) {
            if (!readFanCurve(json["fanPoints2"], loaded.fanGraph2)) {
                loaded.fanGraph2 = defaultFanCurve();
                Serial.println(F("Invalid Fan 2 curve; using default curve"));
            }
            loaded.staticFan2 = json["staticFan2"] | false;
            loaded.staticFanSpeed2 = clampFanSpeed(json["staticFanSpeed2"] | 0);
            loaded.followPrinterFan2 = (json["followPrinterFan2"] | false) && !loaded.staticFan2;
            readPostPrintSettings(json["postPrint2"], json["postPrintSeconds2"], json["postPrintSpeed2"], loaded.postPrint2);
            if (!readPrinterFanSource(json["printerFanSource2"], loaded.printerFanSource2)) {
                loaded.followPrinterFan2 = false;
            }
        }
        if (!loaded.fansSynchronized) initializeSecondFan(loaded);
        snprintf(loaded.haHost, sizeof(loaded.haHost), "%s", json["haHost"] | "");
        snprintf(loaded.haUser, sizeof(loaded.haUser), "%s", json["haUser"] | "");
        snprintf(loaded.haPassword, sizeof(loaded.haPassword), "%s", json["haPassword"] | "");
        const int haPort = json["haPort"] | loaded.haPort;
        if (haPort >= 1 && haPort <= 65535) loaded.haPort = haPort;
        loaded.haEnabled = (json["haEnabled"] | false) && strlen(loaded.haHost) > 0;
        snprintf(loaded.webUser, sizeof(loaded.webUser), "%s", json["webUser"] | "");
        snprintf(loaded.webPassword, sizeof(loaded.webPassword), "%s", json["webPassword"] | "");
        printerConfig = std::move(loaded);

        Serial.println(F("Loaded config"));
    } else {
        Serial.println(F("Failed loading config"));
        Serial.println(F("Keeping defaults; config file left unchanged"));
    }

    configFile.close();
}

void deleteFileSystem(){
    Serial.println(F("Deleting LittleFS"));
    LittleFS.remove(configPath);
}

bool hasFileSystem(){
    return LittleFS.exists(configPath);
}

void setupFileSystem(){
    Serial.println(F("Mounting LittleFS"));
    if (!LittleFS.begin()) {
        Serial.println(F("Failed to mount LittleFS"));
        LittleFS.format();
        Serial.println(F("Formatting LittleFS"));
        Serial.println(F("Restarting Device"));
        delay(1000);
        ESP.restart();
    }
    Serial.println(F("Mounted LittleFS"));
};

#endif
