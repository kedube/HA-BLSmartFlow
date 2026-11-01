#ifndef _BLLEDWEB_SERVER
#define _BLLEDWEB_SERVER

#include <Arduino.h>
#include <ArduinoJson.h> 
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <algorithm>
#include <vector>
#include "controller-name.h"
#include "diagnostics.h"
#include "filesystem.h"
#include "fans.h"
#include "home-assistant.h"
#include "log-serial.h"
#include "mqttmanager.h"
#include "printer-discovery.h"
#include "setup-access-point.h"
#include "ssdp.h"


WebServer webServer(80);

#include "../www/fanpage.h"
#include "../www/networkSetup.h"
#include "../www/updatePage.h"
#include "../www/backupRestore.h"
#include "../www/webSerialPage.h"
#include "../www/style.h"
#include "../www/particles.h"
#include "../www/favicon.h"
#include "../www/fanIcon.h"

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

// Pages and their shared style and script are stored gzipped.
void sendCompressed(const char *type, const uint8_t *data, unsigned int length) {
    if (!allowRequest()) return;
    webServer.sendHeader(F("Content-Encoding"), F("gzip"));
    webServer.send_P(200, type, (const char*)data, (int)length);
}

// While the setup network is open, every address leads to the setup page, which is what makes
// phones offer it as the network's sign-in page.
void redirectToSetup() {
    webServer.sendHeader(F("Location"), String("http://") + setupApAddress.toString() + "/wifi");
    webServer.send(302, "text/plain", "");
}

void handleNotFound() {
    if (setupApActive) redirectToSetup();
    else webServer.send(404, "text/plain", "Not found");
}

void handleSetup() {
    if (setupApActive) redirectToSetup();
    else sendCompressed("text/html", fanpage_html_gz, fanpage_html_gz_len);
}
void handleWifiPage() { sendCompressed("text/html", networkSetup_html_gz, networkSetup_html_gz_len); }
void handleUpdatePage() { sendCompressed("text/html", updatepage_html_gz, updatepage_html_gz_len); }
void handleBackupPage() { sendCompressed("text/html", backupRestore_html_gz, backupRestore_html_gz_len); }
void handleLogPage() { sendCompressed("text/html", webSerialPage_html_gz, webSerialPage_html_gz_len); }
void handleStyle() { sendCompressed("text/css", style_css_gz, style_css_gz_len); }
void handleScript() { sendCompressed("text/javascript", particles_js_gz, particles_js_gz_len); }
void handleFanIcon() { sendCompressed("image/svg+xml", fan_svg_gz, fan_svg_gz_len); }

bool updateAccepted = false;

// The upload runs before handleUpdate, so check the request before writing any firmware.
void handleUpdateUpload() {
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
        updateAccepted = requestRefusal(true) == 0;
        if (!updateAccepted) {
            LogSerial.println(F("Refused firmware upload"));
            return;
        }
        LogSerial.printf("Update: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(LogSerial);
        }
    } else if (!updateAccepted) {
        return;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(LogSerial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            LogSerial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
            Update.printError(LogSerial);
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
    LogSerial.println(F("Restarting Device"));
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

    doc["controllerName"] = printerConfig.controllerName;
    doc["haEnabled"] = printerConfig.haEnabled;
    doc["haHost"] = printerConfig.haHost;
    doc["haPort"] = printerConfig.haPort;
    doc["haUser"] = printerConfig.haUser;
    doc["haPasswordSet"] = strlen(printerConfig.haPassword) > 0; // Never send the password itself
    doc["controlMode"] = controlModeName(printerConfig.controlMode);
    doc["webUser"] = printerConfig.webUser;
    doc["webPasswordSet"] = strlen(printerConfig.webPassword) > 0;
    doc["settingsRevision"] = globalVariables.settingsRevision;
    doc["wifiTxPower"] = printerConfig.wifiTxPower;
    doc["wifiSleep"] = printerConfig.wifiSleep;
    doc["printerConnection"] = printerConfig.printerConnection;
    doc["ssdpEnabled"] = printerConfig.ssdpEnabled;

    // Debugging
    doc["debuging"] = printerConfig.debuging;
    doc["debugingchange"] = printerConfig.debugingchange;
    doc["mqttdebug"] = printerConfig.mqttdebug;


    String jsonString;
    serializeJson(doc, jsonString);
    webServer.send(200, "application/json", jsonString);

    LogSerial.println(F("Packet sent to setuppage"));
}

// Keep controlling the fans without the .local name rather than halting.
void startMdns() {
    const String host = controllerHostname();
    if (!MDNS.begin(host.c_str())) {
        LogSerial.println(F("Error setting up MDNS responder; use the IP address instead"));
        return;
    }
    LogSerial.print(F("Web pages also at http://"));
    LogSerial.print(host);
    LogSerial.println(F(".local/"));
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

void upperCaseSerialNumber(PrinterConfig &config) {
    for (char *c = config.serialNumber; *c; ++c) *c = toupper(static_cast<unsigned char>(*c));
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
    if (webServer.hasArg("controlMode")) {
        ControlMode mode;
        if (!readControlMode(webServer.arg("controlMode").c_str(), mode)) {
            error = "Choose Hybrid, Home Assistant, or Printer control";
            return false;
        }
        setControlMode(candidate, mode);
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
    upperCaseSerialNumber(candidate);
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
    if (!readTextArgument("controllerName", candidate.controllerName, true)) {
        webServer.send(400, "text/plain", "The controller name must be 32 characters or fewer");
        return;
    }
    const char *error = nullptr;
    if (!readHomeAssistantArguments(candidate, printerConfig, error) || !readLoginArguments(candidate, error)) {
        webServer.send(400, "text/plain", error);
        return;
    }
    if (!readIntegerArgument("wifiTxPower", candidate.wifiTxPower, 0, 84) || !validWifiTxPower(candidate.wifiTxPower) ||
        !readBoolArgument("wifiSleep", candidate.wifiSleep) ||
        !readBoolArgument("printerConnection", candidate.printerConnection) ||
        !readBoolArgument("ssdpEnabled", candidate.ssdpEnabled)) {
        webServer.send(400, "text/plain", "Invalid Debug & Diagnostics settings; choose a listed transmit power");
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
    const bool nameChanged = strcmp(candidate.controllerName, printerConfig.controllerName) != 0;
    const bool hostnameChanged = controllerHostname(candidate) != controllerHostname();
    const bool radioChanged = candidate.wifiTxPower != printerConfig.wifiTxPower || candidate.wifiSleep != printerConfig.wifiSleep;
    const bool ssdpChanged = candidate.ssdpEnabled != printerConfig.ssdpEnabled;
    applySettings(std::move(candidate));
    if (haChanged) haSettingsChanged();
    else if (nameChanged) haNameChanged();
    if (hostnameChanged) {
        MDNS.end();
        startMdns();
    }
    if (radioChanged) applyWifiRadioSettings();
    if (ssdpChanged) {
        if (printerConfig.ssdpEnabled) start_ssdp();
        else SSDP.end();
    }
    // mqttloop opens or closes the printer connection on its next pass.
    webServer.send(200, "text/plain", "Settings saved");
}

// Like the other passwords, the Wi-Fi password is never sent to the page.
void handleGetWifiSetup() {
    if (!allowRequest()) return;
    JsonDocument doc;
    doc["ssid"] = globalVariables.SSID;
    doc["passwordSet"] = globalVariables.APPW[0] != '\0';
    doc["controllerName"] = printerConfig.controllerName;
    doc["ip"] = printerConfig.printerIP;
    doc["code"] = obfuscate(printerConfig.accessCode);
    doc["id"] = obfuscate(printerConfig.serialNumber);
    doc["accessPoint"] = setupApActive;
    doc["address"] = String("http://") + controllerHostname() + ".local/";
    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
}

// Scans in the background, so the fans and pages keep running; the page asks again until the
// list is ready. start begins a new scan, because an earlier scan's results may be out of date.
void handleWifiScan() {
    if (!allowRequest()) return;
    if (webServer.hasArg("start")) {
        WiFi.scanDelete();
        if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
            webServer.send(503, "text/plain", "Unable to scan for networks; try again");
            return;
        }
        webServer.send(202, "application/json", "{\"scanning\":true}");
        return;
    }
    const int found = WiFi.scanComplete();
    if (found == WIFI_SCAN_RUNNING) {
        webServer.send(202, "application/json", "{\"scanning\":true}");
        return;
    }
    if (found < 0) {
        webServer.send(503, "text/plain", "The network scan failed; try again");
        return;
    }
    // Strongest first, each name once. Hidden networks have no name to choose.
    std::vector<int> order;
    for (int i = 0; i < found; ++i) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();
    std::vector<String> names;
    for (const int i : order) {
        const String ssid = WiFi.SSID(i);
        if (ssid.length() == 0 || std::find(names.begin(), names.end(), ssid) != names.end()) continue;
        names.push_back(ssid);
        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = ssid;
        network["rssi"] = WiFi.RSSI(i);
        network["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
}

void handlePrinters() {
    if (!allowRequest()) return;
    requestPrinterDiscovery();
    JsonDocument doc;
    doc["listening"] = discoveryListening;
    JsonArray printers = doc["printers"].to<JsonArray>();
    for (size_t i = 0; i < discoveredPrinterCount; ++i) {
        JsonObject printer = printers.add<JsonObject>();
        printer["ip"] = discoveredPrinters[i].ip;
        printer["serial"] = discoveredPrinters[i].serial;
        printer["name"] = discoveredPrinters[i].name;
        printer["model"] = discoveredPrinters[i].model;
    }
    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
}

// Saves the network, controller name and printer, then restarts to join them; the .local address
// and the printer connection are also set up at start. A blank password keeps the saved one for
// the same network. Another network needs its password, unless the page found it open.
void handleSubmitWifiSetup() {
    if (!allowRequest(true)) return;
    const String ssid = webServer.arg("ssid");
    const String password = webServer.arg("password");
    const bool sameNetwork = ssid == globalVariables.SSID;
    if (ssid.length() == 0 || ssid.length() >= sizeof(globalVariables.SSID)) {
        webServer.send(400, "text/plain", "Enter a Wi-Fi network name of 32 characters or fewer");
        return;
    }
    if (password.length() > 0 && (password.length() < 8 || password.length() >= sizeof(globalVariables.APPW))) {
        webServer.send(400, "text/plain", "Wi-Fi passwords have 8 to 63 characters");
        return;
    }
    if (password.length() == 0 && !sameNetwork && webServer.arg("open") != "on") {
        webServer.send(400, "text/plain", "Enter the password for this Wi-Fi network");
        return;
    }
    PrinterConfig candidate = printerConfig;
    if (!readStringArgument("ip", candidate.printerIP) ||
        !readStringArgument("code", candidate.accessCode) ||
        !readStringArgument("serial", candidate.serialNumber)) {
        webServer.send(400, "text/plain", "Printer field is too long");
        return;
    }
    if (!readTextArgument("controllerName", candidate.controllerName, true)) {
        webServer.send(400, "text/plain", "The controller name must be 32 characters or fewer");
        return;
    }
    upperCaseSerialNumber(candidate);
    // A saved access point belongs to the old network.
    if (!sameNetwork) candidate.BSSID[0] = '\0';
    GlobalVariables previous = globalVariables;
    strcpy(globalVariables.SSID, ssid.c_str());
    if (password.length() > 0 || !sameNetwork) strcpy(globalVariables.APPW, password.c_str());
    if (!saveFileSystem(candidate)) {
        strcpy(globalVariables.SSID, previous.SSID);
        strcpy(globalVariables.APPW, previous.APPW);
        webServer.send(500, "text/plain", "Failed to save settings to flash");
        return;
    }
    LogSerial.print(F("[Setup] Saved; restarting to join "));
    LogSerial.println(globalVariables.SSID);
    webServer.sendHeader("Connection", "close");
    webServer.send(200, "text/plain", "Saved. The controller is restarting.");
    delay(1000);
    ESP.restart();
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
    doc["homeAssistantControl"] = homeAssistantControlsFans(); // Home Assistant is setting the speeds
    doc["rssi"] = WiFi.RSSI();
    doc["printerConnection"] = printerConfig.printerConnection;
    doc["printerConnected"] = mqttClient.connected();
    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
}

// The Runtime Status panel under Debug & Diagnostics.
void handleDiag() {
    if (!allowRequest()) return;
    const uint32_t now = millis();
    printerTraffic.roll(now);
    JsonDocument doc;
    doc["firmware"] = globalVariables.FWVersion;
    doc["uptime"] = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
    doc["resetReason"] = resetReasonName();
    doc["ssid"] = WiFi.SSID();
    doc["bssid"] = WiFi.BSSIDstr();
    doc["channel"] = WiFi.channel();
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["wifiTxPower"] = static_cast<int>(WiFi.getTxPower());
    doc["wifiSleep"] = WiFi.getSleep() != WIFI_PS_NONE;
    doc["printerConnection"] = printerConfig.printerConnection;
    doc["printerConnected"] = mqttClient.connected();
    doc["printerState"] = mqttClient.state();
    doc["printerReportsPerMinute"] = printerTraffic.lastMessages;
    doc["printerBytesPerMinute"] = printerTraffic.lastBytes;
    if (printerTraffic.reported) doc["lastPrinterReport"] = (now - printerTraffic.lastReport) / 1000;
    else doc["lastPrinterReport"] = nullptr;
    doc["homeAssistant"] = homeAssistantStatus();
    doc["homeAssistantState"] = haMqtt.state();
    doc["ssdpEnabled"] = printerConfig.ssdpEnabled;
    doc["freeHeap"] = freeMemory();
    doc["minFreeHeap"] = minFreeMemory();
    doc["maxAllocHeap"] = largestFreeBlock();
    doc["logBytes"] = LogSerial.total();
    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
}

// The Controller Log page polls for the text logged after the position it last saw. This
// handler logs nothing, so polling does not fill the log.
void handleLog() {
    if (!allowRequest()) return;
    const uint32_t from = webServer.hasArg("from") ? strtoul(webServer.arg("from").c_str(), nullptr, 10) : 0;
    String text;
    const uint32_t start = LogSerial.read(from, text);
    webServer.sendHeader(F("X-Log-Start"), String(start));
    webServer.sendHeader(F("X-Log-Next"), String(LogSerial.total()));
    webServer.sendHeader(F("Cache-Control"), F("no-store"));
    webServer.send(200, "text/plain; charset=utf-8", text);
}

// The file holds the Wi-Fi password, the printer access code, and the other passwords in plain
// text, so it is offered only behind the login.
void handleDownloadConfig() {
    if (!allowRequest()) return;
    if (!loginRequired()) {
        webServer.send(403, "text/plain", "Set a Web Page Login before downloading a backup, because the backup contains your passwords");
        return;
    }
    File file = LittleFS.open(configPath, "r");
    if (!file) {
        webServer.send(404, "text/plain", "No settings have been saved yet");
        return;
    }
    const size_t size = file.size();
    std::unique_ptr<char[]> buf(new char[size + 1]);
    buf[file.readBytes(buf.get(), size)] = '\0';
    file.close();
    webServer.sendHeader(F("Content-Disposition"), F("attachment; filename=\"blsmartflow-settings.json\""));
    webServer.sendHeader(F("Cache-Control"), F("no-store"));
    webServer.send(200, "application/json", buf.get());
}

// A restore is written to a separate file and replaces the settings only once it reads as
// settings, so a wrong or cut-off file changes nothing.
const char *restorePath = "/blledconfig.json.upload";
bool restoreAllowed = false;
const char *restoreResult = nullptr; // nullptr until decided, "" once restored, otherwise why it was refused
File restoreFile;
size_t restoreBytes = 0;

void refuseRestore(const char *why) {
    if (restoreFile) restoreFile.close();
    LittleFS.remove(restorePath);
    restoreResult = why;
    LogSerial.print(F("Settings restore refused: "));
    LogSerial.println(why);
}

// The upload runs before handleConfigRestore, so check the request before writing anything.
void handleConfigRestoreUpload() {
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
        restoreResult = nullptr;
        restoreBytes = 0;
        restoreAllowed = requestRefusal(true) == 0;
        if (!restoreAllowed) return;
        restoreFile = LittleFS.open(restorePath, "w");
        if (!restoreFile) refuseRestore("Could not write to the controller's storage; nothing was changed");
    } else if (!restoreAllowed || restoreResult) {
        return;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        restoreBytes += upload.currentSize;
        if (restoreBytes > settingsFileMaxBytes) {
            refuseRestore("The file is too large to be BL Smart Flow+ settings; nothing was changed");
        } else if (restoreFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
            refuseRestore("Could not write to the controller's storage; nothing was changed");
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        restoreFile.close();
        if (!isSettingsFile(restorePath)) {
            refuseRestore("This is not a BL Smart Flow+ settings file; nothing was changed");
        } else if (!LittleFS.rename(restorePath, configPath)) {
            refuseRestore("Could not replace the saved settings; nothing was changed");
        } else {
            restoreResult = "";
            LogSerial.println(F("Settings restored from a backup"));
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        refuseRestore("The upload was interrupted; nothing was changed");
    }
}

void handleConfigRestore() {
    if (!allowRequest(true)) return;
    const char *result = restoreAllowed ? restoreResult : nullptr;
    restoreAllowed = false;
    restoreResult = nullptr;
    if (!result) {
        webServer.send(400, "text/plain", "Choose a settings file to restore");
        return;
    }
    if (*result) {
        webServer.send(400, "text/plain", result);
        return;
    }
    webServer.sendHeader("Connection", "close");
    webServer.send(200, "text/plain", "Settings restored. The controller is restarting.");
    delay(1000);
    ESP.restart();
}

void handleRestart() {
    if (!allowRequest(true)) return;
    LogSerial.println(F("Restart requested from the web page"));
    webServer.sendHeader("Connection", "close");
    webServer.send(200, "text/plain", "The controller is restarting.");
    delay(1000);
    ESP.restart();
}

void setupWebserver(){
    if (!setupApActive) startMdns();
    LogSerial.println(F("Setting up webserver"));

    // The server keeps only the request headers it is told to; it always keeps Authorization.
    const char *headers[] = {ownPageHeader};
    webServer.collectHeaders(headers, 1);

    webServer.on("/", handleSetup);
    webServer.on("/favicon.ico", HTTP_GET, []() {
        webServer.sendHeader(F("Content-Encoding"), F("gzip"));
        webServer.send_P(200, "image/x-icon", (const char*)favicon_ico_gz, (int)favicon_ico_gz_len);
    });
    webServer.on("/style.css", HTTP_GET, handleStyle);
    webServer.on("/particles.js", HTTP_GET, handleScript);
    webServer.on("/fan.svg", HTTP_GET, handleFanIcon);
    webServer.on("/submitOptions",HTTP_POST,submitOptions);
    webServer.on("/getOptions", handleGetOptions);

    webServer.on("/update", HTTP_GET, handleUpdatePage);
    webServer.on("/update", HTTP_POST, handleUpdate, handleUpdateUpload); //OTA

    webServer.on("/getFanConfig", HTTP_GET, handleGetFanConfig);
    webServer.on("/updateFanConfig", HTTP_POST, handleUpdateFanConfig);
    webServer.on("/sensorData", HTTP_GET, handleSensorData);

    // Debug & Diagnostics, and Device Tools
    webServer.on("/diag", HTTP_GET, handleDiag);
    webServer.on("/log", HTTP_GET, handleLog);
    webServer.on("/webserial", HTTP_GET, handleLogPage);
    webServer.on("/backuprestore", HTTP_GET, handleBackupPage);
    webServer.on("/configfile.json", HTTP_GET, handleDownloadConfig);
    webServer.on("/configrestore", HTTP_POST, handleConfigRestore, handleConfigRestoreUpload);
    webServer.on("/restart", HTTP_POST, handleRestart);
    webServer.on("/wifi", HTTP_GET, handleWifiPage);
    webServer.on("/getWifiSetup", HTTP_GET, handleGetWifiSetup);
    webServer.on("/wifiScan", HTTP_GET, handleWifiScan);
    webServer.on("/printers", HTTP_GET, handlePrinters);
    webServer.on("/submitWifiSetup", HTTP_POST, handleSubmitWifiSetup);
    webServer.onNotFound(handleNotFound);

    webServer.begin();

    LogSerial.println(F("Webserver started"));
    LogSerial.println();
}

void webserverloop(){
    webServer.handleClient();
    printerDiscoveryLoop();
    delay(10);
}

#endif
