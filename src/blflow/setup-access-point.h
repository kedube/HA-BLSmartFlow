#ifndef BLFLOW_SETUP_ACCESS_POINT_H
#define BLFLOW_SETUP_ACCESS_POINT_H

// Without a working Wi-Fi network, the controller opens its own open network, BLSmartFlow-XXXX,
// so Wi-Fi can be set up from a phone instead of over USB. Its DNS server answers every name
// with the controller, so phones offer the setup page as the network's sign-in page. The saved
// network is retried only while nobody is connected, because joining it can move the radio to
// another channel and drop the phone.

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include "log-serial.h"
#include "types.h"

const IPAddress setupApAddress(192, 168, 4, 1);
const uint32_t setupRetryIntervalMs = 60000;
const uint32_t setupRetryTimeoutMs = 15000;

DNSServer setupDns;
bool setupApActive = false;
bool setupRetrying = false;
uint32_t setupRetryStarted = 0;

inline bool wifiConfigured() {
    return globalVariables.SSID[0] != '\0';
}

// Ends with the last two bytes of the MAC address, so neighbouring controllers differ.
String setupApName() {
    const String mac = WiFi.macAddress();
    String name = "BLSmartFlow-";
    for (size_t i = mac.length() >= 5 ? mac.length() - 5 : 0; i < mac.length(); ++i) {
        if (mac[i] != ':') name += mac[i];
    }
    return name;
}

void startSetupAccessPoint() {
    WiFi.disconnect();
    // Only the retries below may join the saved network, so they can wait while a phone is connected.
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_AP_STA);
    const String name = setupApName();
    WiFi.softAP(name.c_str());
    WiFi.softAPConfig(setupApAddress, setupApAddress, IPAddress(255, 255, 255, 0));
    setupDns.start(53, "*", setupApAddress);
    setupApActive = true;
    setupRetrying = false;
    setupRetryStarted = millis();
    LogSerial.print(F("[Setup] Join the Wi-Fi network "));
    LogSerial.print(name);
    LogSerial.print(F(" and open http://"));
    LogSerial.print(setupApAddress.toString());
    LogSerial.println(F("/ to set up Wi-Fi"));
}

// Returns true once the saved network has been joined; the caller then restarts normally.
bool setupAccessPointLoop() {
    setupDns.processNextRequest();
    if (!wifiConfigured()) return false;
    if (WiFi.status() == WL_CONNECTED) return true;
    const uint32_t now = millis();
    const bool phoneConnected = WiFi.softAPgetStationNum() > 0;
    if (setupRetrying) {
        if (phoneConnected || now - setupRetryStarted >= setupRetryTimeoutMs) {
            WiFi.disconnect();
            setupRetrying = false;
        }
        return false;
    }
    if (phoneConnected || now - setupRetryStarted < setupRetryIntervalMs) return false;
    LogSerial.print(F("[Setup] Trying "));
    LogSerial.print(globalVariables.SSID);
    LogSerial.println(F(" again"));
    WiFi.begin(globalVariables.SSID, globalVariables.APPW);
    setupRetrying = true;
    setupRetryStarted = now;
    return false;
}

#endif
