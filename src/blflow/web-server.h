#ifndef _BLLEDWEB_SERVER
#define _BLLEDWEB_SERVER

#include <Arduino.h>
#include <ArduinoJson.h> 
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "filesystem.h"
#include "fans.h"
#include "home-assistant.h"


WebServer webServer(80);

#include "../www/fanpage.h"
#include "../www/updatePage.h"
#include "../www/favicon.h"

// Requests that change something must carry this header, which the controller's own pages
// send. Another site cannot add a custom header without the browser first asking this server
// (a CORS preflight), which it never approves. So another site cannot change settings or the
// firmware through a visitor's browser, even one that has saved the login.
const char *const ownPageHeader = "X-BLSmartFlow";

inline bool loginRequired() {
    return strlen(printerConfig.webUser) > 0 && strlen(printerConfig.webPassword) > 0;
}

// 401 without the login, 403 for a change from another site, otherwise 0.
int requestRefusal(bool change) {
    if (loginRequired() && !webServer.authenticate(printerConfig.webUser, printerConfig.webPassword)) return 401;
    if (change && !webServer.hasHeader(ownPageHeader)) return 403;
    return 0;
}

// Sends the refusal and returns false when the request may not proceed.
bool allowRequest(bool change = false) {
    const int refusal = requestRefusal(change);
    if (refusal == 401) webServer.requestAuthentication(BASIC_AUTH, "BL Smart Flow+", "Log in to use BL Smart Flow+");
    if (refusal == 403) webServer.send(403, "text/plain", "Refused: make changes from the controller's own pages");
    return refusal == 0;
}

void handleSetup(){
    if (!allowRequest()) return;
    webServer.sendHeader(F("Content-Encoding"), F("gzip"));
    webServer.send_P(200, "text/html", (const char*)fanpage_html_gz, (int)fanpage_html_gz_len);
}

void handleUpdatePage() {
    if (!allowRequest()) return;
    webServer.sendHeader(F("Content-Encoding"), F("gzip"));
    webServer.send_P(200, "text/html", (const char*)updatepage_html_gz, (int)updatepage_html_gz_len);
}

bool updateAccepted = false;

// The upload runs before handleUpdate, so check the request before writing any firmware.
void handleUpdateUpload() {
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
        updateAccepted = requestRefusal(true) == 0;
        if (!updateAccepted) {
            Serial.println(F("Refused firmware upload"));
            return;
        }
        Serial.printf("Update: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (!updateAccepted) {
        return;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        // Otherwise the half-written update blocks the next one until a restart.
        Update.abort();
        updateAccepted = false;
    }
}

void handleUpdate() {
    if (!allowRequest(true)) return;
    if (!updateAccepted) {
        webServer.send(400, "text/plain", "Choose a firmware file to upload");
        return;
    }
    updateAccepted = false;
    const bool updated = !Update.hasError() && Update.isFinished();
    webServer.sendHeader("Connection", "close");
    webServer.send(updated ? 200 : 500, "text/plain", updated ? "OK" : "FAIL");
    if (!updated) return; // Keep running the current firmware
    Serial.println(F("Restarting Device"));
    delay(1000);
    ESP.restart();
}

template <typename T>
String toJson(T val) {
    return String(val);
}

template <>
String toJson<bool>(bool val) {
    return val ? "true" : "false";
}

String obfuscate(const char* value) {
    String result(value);
    for (size_t i = 0; i + 3 < result.length(); ++i) result[i] = '*';
    return result;
}

void handleGetOptions(){
    if (!allowRequest()) return;

    JsonDocument doc;
    const char* firmwareVersionChar = globalVariables.FWVersion.c_str();
    doc["firmwareversion"] = firmwareVersionChar;

    doc["ip"] = printerConfig.printerIP;
    doc["code"] = obfuscate(printerConfig.accessCode);
    doc["id"] = obfuscate(printerConfig.serialNumber);

    doc["staticfans"] = printerConfig.staticFan;
    doc["staticfanspeed"] = printerConfig.staticFanSpeed;
    doc["followPrinterFan"] = printerConfig.followPrinterFan;
    doc["printerFanSource"] = printerFanSourceName(printerConfig.printerFanSource);
    doc["postPrint"] = printerConfig.postPrint.enabled;
    doc["postPrintSeconds"] = printerConfig.postPrint.seconds;
    doc["postPrintSpeed"] = printerConfig.postPrint.speed;
    doc["fansSynchronized"] = printerConfig.fansSynchronized;
    const bool fan2Configured = !printerConfig.fanGraph2.empty();
    doc["fan2Configured"] = fan2Configured;
    doc["staticfans2"] = fan2Configured ? printerConfig.staticFan2 : printerConfig.staticFan;
    doc["staticfanspeed2"] = fan2Configured ? printerConfig.staticFanSpeed2 : printerConfig.staticFanSpeed;
    doc["followPrinterFan2"] = fan2Configured ? printerConfig.followPrinterFan2 : printerConfig.followPrinterFan;
    doc["printerFanSource2"] = printerFanSourceName(fan2Configured ? printerConfig.printerFanSource2 : printerConfig.printerFanSource);
    const auto &postPrint2 = fan2Configured ? printerConfig.postPrint2 : printerConfig.postPrint;
    doc["postPrint2"] = postPrint2.enabled;
    doc["postPrintSeconds2"] = postPrint2.seconds;
    doc["postPrintSpeed2"] = postPrint2.speed;

    doc["haEnabled"] = printerConfig.haEnabled;
    doc["haHost"] = printerConfig.haHost;
    doc["haPort"] = printerConfig.haPort;
    doc["haUser"] = printerConfig.haUser;
    doc["haPasswordSet"] = strlen(printerConfig.haPassword) > 0; // Never send the password itself
    doc["webUser"] = printerConfig.webUser;
    doc["webPasswordSet"] = strlen(printerConfig.webPassword) > 0;
    doc["settingsRevision"] = globalVariables.settingsRevision;

    // Debugging
    doc["debuging"] = printerConfig.debuging;
    doc["debugingchange"] = printerConfig.debugingchange;
    doc["mqttdebug"] = printerConfig.mqttdebug;


    String jsonString;
    serializeJson(doc, jsonString);
    webServer.send(200, "application/json", jsonString);

    Serial.println(F("Packet sent to setuppage"));
}

// All web updates are staged, persisted, then applied to the running controller.
bool readIntegerArgument(const char *name, int &value, int minimum, int maximum) {
    if (!webServer.hasArg(name)) return true;
    const String arg = webServer.arg(name);
    if (arg.length() == 0 || arg.length() > 10) return false;
    int parsed = 0;
    for (size_t i = 0; i < arg.length(); ++i) {
        if (arg[i] < '0' || arg[i] > '9') return false;
        const int digit = arg[i] - '0';
        if (parsed > (maximum - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    if (parsed < minimum || parsed > maximum) return false;
    value = parsed;
    return true;
}

bool readSpeedArgument(const char *name, int &speed) {
    return readIntegerArgument(name, speed, 0, 100);
}

bool readBoolArgument(const char *name, bool &value) {
    if (!webServer.hasArg(name)) return true;
    const String arg = webServer.arg(name);
    if (arg == "on" || arg == "true") value = true;
    else if (arg == "off" || arg == "false") value = false;
    else return false;
    return true;
}

bool readPostPrintArguments(const char *enabledName, const char *secondsName, const char *speedName,
                            PostPrintSettings &settings) {
    return readBoolArgument(enabledName, settings.enabled) &&
        readIntegerArgument(secondsName, settings.seconds, 1, postPrintMaxSeconds) &&
        readSpeedArgument(speedName, settings.speed);
}

bool readFanModeArguments(const char *staticName, const char *followName, const char *sourceName,
                          bool &staticEnabled, bool &followEnabled, PrinterFanSource &source) {
    if (!readBoolArgument(staticName, staticEnabled) || !readBoolArgument(followName, followEnabled)) return false;
    if (webServer.hasArg(sourceName) && !readPrinterFanSource(webServer.arg(sourceName).c_str(), source)) return false;
    const bool enableStatic = webServer.hasArg(staticName) && staticEnabled;
    const bool enableFollow = webServer.hasArg(followName) && followEnabled;
    if (enableStatic && enableFollow) return false;
    if (enableStatic) followEnabled = false;
    if (enableFollow) staticEnabled = false;
    return true;
}

template <size_t N>
bool readStringArgument(const char *name, char (&value)[N]) {
    if (!webServer.hasArg(name)) return true;
    const String arg = webServer.arg(name);
    if (arg.indexOf('*') != -1) return true;
    if (arg.length() >= N) return false;
    strcpy(value, arg.c_str());
    return true;
}

// Unlike readStringArgument, '*' is literal text rather than a masked value.
template <size_t N>
bool readTextArgument(const char *name, char (&value)[N], bool trim = false) {
    if (!webServer.hasArg(name)) return true;
    String arg = webServer.arg(name);
    if (trim) arg.trim();
    if (arg.length() >= N) return false;
    strcpy(value, arg.c_str());
    return true;
}

// The broker password is never sent to the page. A blank field keeps it, unless that
// would send it to a different broker.
bool readHomeAssistantArguments(PrinterConfig &candidate, const PrinterConfig &current, const char *&error) {
    if (!readBoolArgument("haEnabled", candidate.haEnabled) ||
        !readIntegerArgument("haPort", candidate.haPort, 1, 65535)) {
        error = "Invalid Home Assistant settings; use a broker port from 1 to 65535";
        return false;
    }
    const String password = webServer.arg("haPassword");
    if (!readTextArgument("haHost", candidate.haHost, true) || !readTextArgument("haUser", candidate.haUser) ||
        password.length() >= sizeof(candidate.haPassword)) {
        error = "The Home Assistant broker host, username, and password must each be 64 characters or fewer";
        return false;
    }
    if (strlen(candidate.haUser) == 0) {
        candidate.haPassword[0] = '\0'; // Clearing the username removes the password
    } else if (password.length() > 0) {
        strcpy(candidate.haPassword, password.c_str());
    } else if (strlen(current.haPassword) > 0 && haPasswordExposed(current, candidate)) {
        error = "Enter the broker password again after changing the broker host";
        return false;
    }
    if (candidate.haEnabled && strlen(candidate.haHost) == 0) {
        error = "Enter the Home Assistant broker host";
        return false;
    }
    return true;
}

// Like the broker password, the login password is never sent to the page: a blank field
// keeps it, and clearing the username removes the login.
bool readLoginArguments(PrinterConfig &candidate, const char *&error) {
    const String password = webServer.arg("webPassword");
    if (!readTextArgument("webUser", candidate.webUser, true) || password.length() >= sizeof(candidate.webPassword)) {
        error = "The login username must be 32 characters or fewer, and the password 64 or fewer";
        return false;
    }
    if (strchr(candidate.webUser, ':')) {
        error = "The login username cannot contain a colon";
        return false;
    }
    if (strlen(candidate.webUser) == 0) {
        if (password.length() > 0) {
            error = "Enter a username to set a login";
            return false;
        }
        candidate.webPassword[0] = '\0';
    } else if (password.length() > 0) {
        strcpy(candidate.webPassword, password.c_str());
    } else if (strlen(candidate.webPassword) == 0) {
        error = "Enter a password for the login";
        return false;
    }
    return true;
}

void submitOptions() {
    if (!allowRequest(true)) return;
    if (webServer.args() == 0) {
        webServer.send(204);
        return;
    }
    PrinterConfig candidate = printerConfig;
    if (!readStringArgument("ip", candidate.printerIP) ||
        !readStringArgument("code", candidate.accessCode) ||
        !readStringArgument("serial", candidate.serialNumber)) {
        webServer.send(400, "text/plain", "Printer field is too long");
        return;
    }
    for (size_t i = 0; i < strlen(candidate.serialNumber); ++i) {
        candidate.serialNumber[i] = toupper(static_cast<unsigned char>(candidate.serialNumber[i]));
    }
    // Legacy forms omit unchecked Fan 1 checkboxes.
    candidate.staticFan = false;
    if (!readBoolArgument("fansSynchronized", candidate.fansSynchronized) ||
        !readFanModeArguments("staticfan", "followPrinterFan", "printerFanSource",
            candidate.staticFan, candidate.followPrinterFan, candidate.printerFanSource) ||
        !readSpeedArgument("staticfanspeed", candidate.staticFanSpeed)) {
        webServer.send(400, "text/plain", "Invalid Fan 1 settings; choose one override, a valid printer fan, and an integer speed from 0 to 100");
        return;
    }
    if (!readPostPrintArguments("postPrint", "postPrintSeconds", "postPrintSpeed", candidate.postPrint)) {
        webServer.send(400, "text/plain", "Invalid Fan 1 Post Print settings; use 1 to 86400 whole seconds and a speed from 0 to 100");
        return;
    }
    if (!candidate.fansSynchronized) {
        initializeSecondFan(candidate);
        if (!readFanModeArguments("staticfan2", "followPrinterFan2", "printerFanSource2",
                candidate.staticFan2, candidate.followPrinterFan2, candidate.printerFanSource2) ||
            !readSpeedArgument("staticfanspeed2", candidate.staticFanSpeed2)) {
            webServer.send(400, "text/plain", "Invalid Fan 2 settings; choose one override, a valid printer fan, and an integer speed from 0 to 100");
            return;
        }
        if (!readPostPrintArguments("postPrint2", "postPrintSeconds2", "postPrintSpeed2", candidate.postPrint2)) {
            webServer.send(400, "text/plain", "Invalid Fan 2 Post Print settings; use 1 to 86400 whole seconds and a speed from 0 to 100");
            return;
        }
    }
    const char *error = nullptr;
    if (!readHomeAssistantArguments(candidate, printerConfig, error) || !readLoginArguments(candidate, error)) {
        webServer.send(400, "text/plain", error);
        return;
    }
    candidate.debuging = (webServer.arg("debuging") == "on");
    candidate.debugingchange = (webServer.arg("debugingchange") == "on");
    candidate.mqttdebug = (webServer.arg("mqttdebug") == "on");
    if (!saveFileSystem(candidate)) {
        webServer.send(500, "text/plain", "Failed to save settings to flash");
        return;
    }
    const bool haChanged = haConnectionChanged(printerConfig, candidate);
    applySettings(std::move(candidate));
    if (haChanged) haSettingsChanged();
    webServer.send(200, "text/plain", "Settings saved");
}

void handleGetFanConfig() {
    if (!allowRequest()) return;
    JsonDocument doc;
    doc["fansSynchronized"] = printerConfig.fansSynchronized;
    writeFanCurve(doc["points"].to<JsonArray>(), printerConfig.fanGraph);
    writeFanCurve(doc["points2"].to<JsonArray>(), printerConfig.fanGraph2.empty()
                 ? printerConfig.fanGraph : printerConfig.fanGraph2);
    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
}

void handleUpdateFanConfig() {
    if (!allowRequest(true)) return;
    const String fan = webServer.hasArg("fan") ? webServer.arg("fan") : String("1");
    if ((fan != "1" && fan != "2") || !webServer.hasArg("points")) {
        webServer.send(400, "text/plain", "Supply points and a fan number of 1 or 2");
        return;
    }
    JsonDocument doc;
    FanCurve curve;
    if (deserializeJson(doc, webServer.arg("points")) || !readFanCurve(doc["points"], curve)) {
        webServer.send(400, "text/plain", "Use unique temperatures from 0 to 400 and integer speeds from 0 to 100; at least one point is required");
        return;
    }
    PrinterConfig candidate = printerConfig;
    if (fan == "2") {
        initializeSecondFan(candidate);
        candidate.fanGraph2 = std::move(curve);
    } else {
        candidate.fanGraph = std::move(curve);
    }
    if (!saveFileSystem(candidate)) {
        webServer.send(500, "text/plain", "Failed to save fan curve to flash");
        return;
    }
    printerConfig = std::move(candidate);
    webServer.send(200, "text/plain", "Fan curve saved");
}

void handleSensorData() {
    if (!allowRequest()) return;
    const uint32_t now = millis();
    fanloop(now);
    JsonDocument doc;
    doc["temp"] = printerVariables.nozzletemp;
    doc["speed"] = globalVariables.fanSpeed;
    doc["speed2"] = globalVariables.fanSpeed2;
    doc["postPrintRemaining"] = postPrintSecondsRemaining(0, now);
    doc["postPrintRemaining2"] = postPrintSecondsRemaining(1, now);
    for (int fan = 0; fan < printerFanCount; ++fan) {
        const char *name = printerFanSourceName(static_cast<PrinterFanSource>(fan));
        if (printerVariables.fans[fan].known) doc["printerFans"][name] = printerVariables.fans[fan].percent;
        else doc["printerFans"][name] = nullptr;
    }
    doc["settingsRevision"] = globalVariables.settingsRevision;
    doc["homeAssistant"] = homeAssistantStatus();
    doc["homeAssistantState"] = haMqtt.state();
    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
}

void setupWebserver(){
    // Keep controlling the fans without the .local name rather than halting.
    if (!MDNS.begin(globalVariables.Host.c_str())) {
        Serial.println(F("Error setting up MDNS responder; use the IP address instead"));
    }
    
    Serial.println(F("Setting up webserver"));

    // The server keeps only the request headers it is told to; it always keeps Authorization.
    const char *headers[] = {ownPageHeader};
    webServer.collectHeaders(headers, 1);

    webServer.on("/", handleSetup);
    webServer.on("/favicon.ico", HTTP_GET, []() {
        webServer.sendHeader(F("Content-Encoding"), F("gzip"));
        webServer.send_P(200, "image/x-icon", (const char*)favicon_ico_gz, (int)favicon_ico_gz_len);
    });
    webServer.on("/submitOptions",HTTP_POST,submitOptions);
    webServer.on("/getOptions", handleGetOptions);

    webServer.on("/update", HTTP_GET, handleUpdatePage);
    webServer.on("/update", HTTP_POST, handleUpdate, handleUpdateUpload); //OTA

    webServer.on("/getFanConfig", HTTP_GET, handleGetFanConfig);
    webServer.on("/updateFanConfig", HTTP_POST, handleUpdateFanConfig);
    webServer.on("/sensorData", HTTP_GET, handleSensorData);

    webServer.begin();

    Serial.println(F("Webserver started"));
    Serial.println();
}

void webserverloop(){
    webServer.handleClient();
    delay(10);
}

#endif
