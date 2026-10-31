#ifndef _BLLEDSERIAL_MANAGER
#define _BLLEDSERIAL_MANAGER

#include <Arduino.h>
#include <ArduinoJson.h> 

#include "filesystem.h"
#include "types.h"

void setupSerial(){
   while (!Serial);
}

// Copies a provisioning field, rejecting values that do not fit instead of overflowing.
// A missing field keeps its current value.
template <size_t N>
bool readSerialField(JsonVariantConst value, char (&destination)[N]) {
    if (value.isNull()) return true;
    const char *text = value | "";
    if (strlen(text) >= N) return false;
    strcpy(destination, text);
    return true;
}

void serialLoop(){
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        JsonDocument doc;
        deserializeJson(doc, input);
        if (doc["ssid"].is<const char *>() && doc["pass"].is<const char *>()) {
            // The setup page shows this log, so echo identifiers but not secrets.
            Serial.print(F("SSID "));
            Serial.println(doc["ssid"].as<String>());
            Serial.println(doc["printerip"].as<String>());
            Serial.println(doc["printerserial"].as<String>());

            PrinterConfig candidate = printerConfig;
            char ssid[sizeof(globalVariables.SSID)];
            char password[sizeof(globalVariables.APPW)];
            if (!readSerialField(doc["ssid"], ssid) || !readSerialField(doc["pass"], password) ||
                !readSerialField(doc["printerip"], candidate.printerIP) ||
                !readSerialField(doc["printercode"], candidate.accessCode) ||
                !readSerialField(doc["printerserial"], candidate.serialNumber)) {
                Serial.println(F("A setting is too long; nothing was saved."));
                return;
            }
            // Setup over USB needs the device in hand, so it also clears a forgotten web login.
            candidate.webUser[0] = candidate.webPassword[0] = '\0';
            strcpy(globalVariables.SSID, ssid);
            strcpy(globalVariables.APPW, password);
            printerConfig = std::move(candidate);

            saveFileSystem();
            Serial.println(F("Restarting Device"));
            delay(1000);
            ESP.restart();
       }
      
    };
    delay(10);
}

#endif