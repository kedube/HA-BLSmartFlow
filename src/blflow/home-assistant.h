#ifndef BLFLOW_HOME_ASSISTANT_H
#define BLFLOW_HOME_ASSISTANT_H

// Home Assistant integration over a second MQTT connection, to the user's broker.
// MQTT Discovery creates sensors for monitoring, controls that mirror the fan
// Settings, fan entities that set the speeds directly under the control mode, and
// the BLLED controller's diagnostics and Restart button. Commands are saved to flash
// before they run, like web page changes. The printer connection is unaffected. The
// connection is plain MQTT: a second TLS session alongside the printer's leaves the
// ESP32 short of memory.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <cmath>
#include <strings.h>

#include "connect-timeout.h"
#include "controller-name.h"
#include "diagnostics.h"
#include "types.h"
#include "fan-config.h"
#include "fans.h"
#include "filesystem.h"
#include "log-serial.h"
#include "mqtt-report.h"
#include "post-print.h"
#include "printer-error.h"

const uint32_t haRetryMinimumMs = 5000;
const uint32_t haRetryMaximumMs = 60000;
const uint32_t haStateIntervalMs = 1000;
const uint32_t haDiagnosticsIntervalMs = 60000;
const char *const haBirthTopic = "homeassistant/status";

constexpr int haModeCurve = 0, haModeStatic = 1, haModeFollow = 2;
const char *const haModeNames[] = {"Temperature Curve", "Static Fan Speed", "Follow Printer Fan"};
// Indexed by PrinterFanSource, with the Settings page's labels.
const char *const haSourceNames[] = {"Part Cooling Fan", "Auxiliary Cooling Fan", "Chamber/Exhaust Fan", "Hotend/Heatbreak Fan"};
// Indexed by ControlMode, with the BLLED controller's names.
const char *const haControlModeNames[] = {"Printer", "HomeAssistant", "Hybrid"};

WiFiClient haClient;
PubSubClient haMqtt(haClient);

String haNodeId;        // blsmartflow_<mac>; also the client ID, so a reboot replaces the old session
String haBaseTopic;     // blsmartflow/<mac>
String haLastState;
int haLastFan2Online = -1;
int haLastControlOnline = -1;
bool haConfigured = false;
bool haAttempted = false;
bool haDiscoveryPending = false;
bool haStatePending = false;
bool haRestartPending = false;
uint32_t haLastAttempt = 0;
uint32_t haRetryMs = 0;  // Wait before the next attempt; grows while attempts fail
uint32_t haLastStateCheck = 0;
uint32_t haLastDiagnostics = 0;

// --- Settings ---------------------------------------------------------------

inline bool haConnectionChanged(const PrinterConfig &a, const PrinterConfig &b) {
    return a.haEnabled != b.haEnabled || strcmp(a.haHost, b.haHost) != 0 || a.haPort != b.haPort ||
        strcmp(a.haUser, b.haUser) != 0 || strcmp(a.haPassword, b.haPassword) != 0;
}

// Whether keeping the saved password would send it to a different broker.
inline bool haPasswordExposed(const PrinterConfig &before, const PrinterConfig &after) {
    return strcasecmp(before.haHost, after.haHost) != 0;
}

inline bool sameFanSettings(PrinterConfig &a, PrinterConfig &b) {
    if (a.fansSynchronized != b.fansSynchronized || a.fanGraph2 != b.fanGraph2 || a.controlMode != b.controlMode ||
        a.haFanSpeed != b.haFanSpeed || a.haFanSpeed2 != b.haFanSpeed2) return false;
    for (int fan = 0; fan < 2; ++fan) {
        const FanSettings x = fanSettings(a, fan), y = fanSettings(b, fan);
        if (x.staticEnabled != y.staticEnabled || x.staticSpeed != y.staticSpeed ||
            x.followPrinterFan != y.followPrinterFan || x.printerFanSource != y.printerFanSource ||
            !samePostPrintSettings(x.postPrint, y.postPrint)) return false;
    }
    return true;
}

inline int haModeIndex(const FanSettings &settings) {
    if (settings.staticEnabled) return haModeStatic;
    return settings.followPrinterFan ? haModeFollow : haModeCurve;
}

// --- Commands ---------------------------------------------------------------

inline bool haReadSwitch(const String &value, bool &on) {
    if (strcasecmp(value.c_str(), "ON") == 0) on = true;
    else if (strcasecmp(value.c_str(), "OFF") == 0) on = false;
    else return false;
    return true;
}

inline bool haReadNumber(const String &value, int minimum, int maximum, int &result) {
    uint32_t number;
    if (!readUnsignedText(value.c_str(), maximum, number) || number < static_cast<uint32_t>(minimum)) return false;
    result = number;
    return true;
}

template <size_t N>
int haOptionIndex(const String &value, const char *const (&options)[N]) {
    for (size_t i = 0; i < N; ++i) {
        if (value == options[i]) return i;
    }
    return -1;
}

// The fan entities: on and off, or a percentage. Home Assistant takes over at the current
// speeds, so the other fan keeps running as it was. Printer mode ignores them.
bool haReadFanSpeed(int fan, bool percentage, const String &value, PrinterConfig &config) {
    if (config.controlMode == ControlMode::Printer) return false;
    if (!homeAssistantControlsFans(config)) holdCurrentSpeeds(config);
    int &speed = fan == 0 ? config.haFanSpeed : config.haFanSpeed2;
    if (percentage) return haReadNumber(value, 0, 100, speed);
    bool on;
    if (!haReadSwitch(value, on)) return false;
    // Turning on a running fan keeps its speed.
    if (!on) speed = 0;
    else if (speed == 0) speed = 100;
    return true;
}

// Stages one command. Rejects unknown keys, invalid values, and Fan 2 while synchronized.
// speedCommand is set for the fan entities, which change no Settings.
bool haReadCommand(const char *key, const String &value, PrinterConfig &config, bool &speedCommand) {
    if (strcmp(key, "synchronized") == 0) {
        if (!haReadSwitch(value, config.fansSynchronized)) return false;
        if (!config.fansSynchronized) initializeSecondFan(config);
        return true;
    }
    if (strcmp(key, "control_mode") == 0) {
        const int mode = haOptionIndex(value, haControlModeNames);
        if (mode < 0) return false;
        setControlMode(config, static_cast<ControlMode>(mode));
        return true;
    }
    int fan;
    if (strncmp(key, "fan1", 4) == 0) fan = 0;
    else if (strncmp(key, "fan2", 4) == 0) fan = 1;
    else return false;
    // As on the Settings page, Fan 2's settings are dormant while synchronized.
    if (fan == 1 && config.fansSynchronized) return false;
    if (key[4] == '\0' || strcmp(key + 4, "_percentage") == 0) {
        speedCommand = true;
        return haReadFanSpeed(fan, key[4] != '\0', value, config);
    }
    if (key[4] != '_') return false;
    const char *setting = key + 5;
    FanSettings settings = fanSettings(config, fan);
    if (strcmp(setting, "mode") == 0) {
        const int mode = haOptionIndex(value, haModeNames);
        if (mode < 0) return false;
        settings.staticEnabled = mode == haModeStatic;
        settings.followPrinterFan = mode == haModeFollow;
        return true;
    }
    if (strcmp(setting, "static_speed") == 0) return haReadNumber(value, 0, 100, settings.staticSpeed);
    if (strcmp(setting, "source") == 0) {
        const int source = haOptionIndex(value, haSourceNames);
        if (source < 0) return readPrinterFanSource(value.c_str(), settings.printerFanSource);
        settings.printerFanSource = static_cast<PrinterFanSource>(source);
        return true;
    }
    if (strcmp(setting, "post_print") == 0) return haReadSwitch(value, settings.postPrint.enabled);
    if (strcmp(setting, "post_print_seconds") == 0) {
        return haReadNumber(value, 1, postPrintMaxSeconds, settings.postPrint.seconds);
    }
    if (strcmp(setting, "post_print_speed") == 0) return haReadNumber(value, 0, 100, settings.postPrint.speed);
    return false;
}

// Hybrid: hand the fans back to the printer without waiting for a print to start.
void haResumePrinterControl() {
    if (!globalVariables.haOverride) return;
    globalVariables.haOverride = false;
    fanloop();
    LogSerial.println(F("[HA] The printer has the fans again"));
}

void haApplyCommand(const char *key, const String &value) {
    if (strcmp(key, "resume") == 0) {
        haResumePrinterControl();
        return;
    }
    if (strcmp(key, "restart") == 0) {
        haRestartPending = true; // After loop() returns, so the offline status can be published
        return;
    }
    PrinterConfig candidate = printerConfig;
    bool speedCommand = false;
    if (!haReadCommand(key, value, candidate, speedCommand)) {
        LogSerial.print(F("[HA] Ignored command "));
        LogSerial.print(key);
        LogSerial.print(F(": "));
        LogSerial.println(value);
        return;
    }
    // In Hybrid, a speed takes the fans over from the printer even when it matches theirs.
    const bool takeOver = speedCommand && !homeAssistantControlsFans();
    const bool changed = !sameFanSettings(printerConfig, candidate);
    if (!changed && !takeOver) return;
    if (changed && !saveFileSystem(candidate)) {
        LogSerial.println(F("[HA] Failed to save the command; settings unchanged"));
        return;
    }
    if (takeOver) {
        globalVariables.haOverride = true;
        LogSerial.println(F("[HA] Home Assistant has the fans until the next print starts"));
    }
    if (speedCommand) {
        // Not a Settings change, so open pages need not reload.
        printerConfig.haFanSpeed = candidate.haFanSpeed;
        printerConfig.haFanSpeed2 = candidate.haFanSpeed2;
        fanloop();
    } else {
        applySettings(std::move(candidate));
    }
    LogSerial.print(F("[HA] Applied "));
    LogSerial.print(key);
    LogSerial.print(F(": "));
    LogSerial.println(value);
}

void haCallback(char *topic, byte *payload, unsigned int length) {
    String value;
    value.reserve(length);
    for (unsigned int i = 0; i < length; ++i) value += static_cast<char>(payload[i]);
    if (strcmp(topic, haBirthTopic) == 0) {
        // Home Assistant restarted; its broker may not have kept the entity configs.
        if (value == "online") haDiscoveryPending = true;
        return;
    }
    const String prefix = haBaseTopic + "/set/";
    if (strncmp(topic, prefix.c_str(), prefix.length()) != 0) return;
    haApplyCommand(topic + prefix.length(), value);
    // Publishing reuses the buffer holding this message, so reply after loop() returns.
    haStatePending = true;
}

// --- Publishing -------------------------------------------------------------

bool haPublish(const String &topic, const String &payload) {
    // Streamed, so discovery configs are not limited by the MQTT buffer size.
    return haMqtt.beginPublish(topic.c_str(), payload.length(), true) &&
        haMqtt.write(reinterpret_cast<const uint8_t *>(payload.c_str()), payload.length()) == payload.length() &&
        haMqtt.endPublish();
}

bool haPublishJson(const String &topic, const JsonDocument &doc) {
    String payload;
    serializeJson(doc, payload);
    return haPublish(topic, payload);
}

void haAddDevice(JsonObject device) {
    device["identifiers"].to<JsonArray>().add(haNodeId);
    device["name"] = controllerName();
    device["manufacturer"] = "DutchDeveloper";
    device["model"] = "BLSmartFlow Controller";
    device["sw_version"] = globalVariables.FWVersion;
    device["configuration_url"] = String("http://") + WiFi.localIP().toString() + "/";
}

// Topics an entity needs online besides the controller's status: Fan 2's, offline while the
// fans are synchronized, and the fan entities', offline in Printer mode.
constexpr int haFan2Availability = 1, haControlAvailability = 2;

// Adds the fields every entity shares, then publishes its retained discovery config.
void haPublishConfig(const char *component, const String &key, const String &name, JsonDocument &config,
                     int availabilities = 0) {
    config["name"] = name;
    config["unique_id"] = haNodeId + "_" + key;
    JsonArray availability = config["availability"].to<JsonArray>();
    availability.add<JsonObject>()["topic"] = haBaseTopic + "/status";
    if (availabilities & haFan2Availability) availability.add<JsonObject>()["topic"] = haBaseTopic + "/fan2/availability";
    if (availabilities & haControlAvailability) availability.add<JsonObject>()["topic"] = haBaseTopic + "/control/availability";
    if (availabilities) config["availability_mode"] = "all";
    haAddDevice(config["device"].to<JsonObject>());
    haPublishJson(String("homeassistant/") + component + "/" + haNodeId + "/" + key + "/config", config);
}

// An entity that reads its field from the state.
void haPublishEntity(const char *component, const String &key, const String &name, JsonDocument &config,
                     bool control, bool fan2Setting) {
    config["state_topic"] = haBaseTopic + "/state";
    config["value_template"] = String("{{ value_json.") + key + " }}";
    if (control) config["command_topic"] = haBaseTopic + "/set/" + key;
    haPublishConfig(component, key, name, config, fan2Setting ? haFan2Availability : 0);
}

// Wi-Fi and memory change constantly, so they have their own topic, sent once a minute.
void haPublishDiagnostic(const char *key, const char *name, JsonDocument &config) {
    config["state_class"] = "measurement";
    config["entity_category"] = "diagnostic";
    config["state_topic"] = haBaseTopic + "/diagnostics";
    config["value_template"] = String("{{ value_json.") + key + " }}";
    haPublishConfig("sensor", key, name, config);
}

void haPublishButton(const char *key, const char *name, JsonDocument &config) {
    config["command_topic"] = haBaseTopic + "/set/" + key;
    haPublishConfig("button", key, name, config);
}

template <size_t N>
void haAddOptions(JsonDocument &config, const char *const (&options)[N]) {
    JsonArray list = config["options"].to<JsonArray>();
    for (const char *option : options) list.add(option);
}

void haAddRange(JsonDocument &config, int minimum, int maximum, const char *unit, const char *mode) {
    config["min"] = minimum;
    config["max"] = maximum;
    config["step"] = 1;
    config["unit_of_measurement"] = unit;
    config["mode"] = mode;
}

void haPublishDiscovery() {
    {
        JsonDocument config;
        config["device_class"] = "temperature";
        config["unit_of_measurement"] = "°C";
        config["state_class"] = "measurement";
        config["suggested_display_precision"] = 1;
        haPublishEntity("sensor", "temperature", "Nozzle temperature", config, false, false);
    }
    {
        JsonDocument config;
        config["device_class"] = "connectivity";
        config["entity_category"] = "diagnostic";
        haPublishEntity("binary_sensor", "printer_connected", "Printer connection", config, false, false);
    }
    {
        JsonDocument config;
        config["device_class"] = "problem";
        config["json_attributes_topic"] = haBaseTopic + "/state";
        config["json_attributes_template"] = "{{ {'hms_code': value_json.hms_code, 'hms_severity': value_json.hms_severity} | tojson }}";
        haPublishEntity("binary_sensor", "printer_error", "Printer error", config, false, false);
    }
    {
        JsonDocument config;
        haAddOptions(config, haControlModeNames);
        config["icon"] = "mdi:cog-transfer";
        haPublishEntity("select", "control_mode", "Control mode", config, true, false);
    }
    {
        JsonDocument config;
        config["icon"] = "mdi:home-assistant";
        haPublishEntity("binary_sensor", "override", "Home Assistant override", config, false, false);
    }
    {
        JsonDocument config;
        config["icon"] = "mdi:printer-3d";
        haPublishButton("resume", "Resume printer control", config);
    }
    {
        JsonDocument config;
        config["device_class"] = "restart";
        config["entity_category"] = "config";
        haPublishButton("restart", "Restart", config);
    }
    {
        JsonDocument config;
        config["device_class"] = "signal_strength";
        config["unit_of_measurement"] = "dBm";
        haPublishDiagnostic("rssi", "WiFi signal", config);
    }
    const char *const memorySensors[][2] = {{"free_memory", "Free memory"}, {"largest_block", "Largest free memory block"}};
    for (const auto &sensor : memorySensors) {
        JsonDocument config;
        config["device_class"] = "data_size";
        config["unit_of_measurement"] = "B";
        config["icon"] = "mdi:memory";
        haPublishDiagnostic(sensor[0], sensor[1], config);
    }
    {
        JsonDocument config;
        config["icon"] = "mdi:link-variant";
        config["entity_category"] = "config";
        haPublishEntity("switch", "synchronized", "Synchronize fans", config, true, false);
    }
    for (int fan = 0; fan < 2; ++fan) {
        const String key = String("fan") + String(fan + 1) + "_";
        const String label = String("Fan ") + String(fan + 1) + " ";
        const bool fan2Setting = fan == 1;
        {
            // Shows the output, whoever drives it, and sets the speed while Home Assistant may.
            const String entity = String("fan") + String(fan + 1);
            JsonDocument config;
            config["state_topic"] = haBaseTopic + "/state";
            config["state_value_template"] = String("{{ value_json.") + entity + " }}";
            config["command_topic"] = haBaseTopic + "/set/" + entity;
            config["percentage_state_topic"] = haBaseTopic + "/state";
            config["percentage_value_template"] = String("{{ value_json.") + key + "speed }}";
            config["percentage_command_topic"] = haBaseTopic + "/set/" + key + "percentage";
            config["speed_range_min"] = 1;
            config["speed_range_max"] = 100;
            haPublishConfig("fan", entity, String("Fan ") + String(fan + 1), config,
                            haControlAvailability | (fan2Setting ? haFan2Availability : 0));
        }
        {
            JsonDocument config;
            config["unit_of_measurement"] = "%";
            config["state_class"] = "measurement";
            config["icon"] = "mdi:fan";
            haPublishEntity("sensor", key + "speed", label + "speed", config, false, false);
        }
        {
            JsonDocument config;
            config["device_class"] = "duration";
            config["unit_of_measurement"] = "s";
            config["icon"] = "mdi:timer-outline";
            haPublishEntity("sensor", key + "post_print_remaining", label + "Post Print remaining", config, false, false);
        }
        {
            JsonDocument config;
            haAddOptions(config, haModeNames);
            config["icon"] = "mdi:fan-auto";
            haPublishEntity("select", key + "mode", label + "mode", config, true, fan2Setting);
        }
        {
            JsonDocument config;
            haAddRange(config, 0, 100, "%", "slider");
            config["icon"] = "mdi:speedometer";
            haPublishEntity("number", key + "static_speed", label + "static speed", config, true, fan2Setting);
        }
        {
            JsonDocument config;
            haAddOptions(config, haSourceNames);
            config["icon"] = "mdi:printer-3d-nozzle";
            config["entity_category"] = "config";
            haPublishEntity("select", key + "source", label + "printer fan", config, true, fan2Setting);
        }
        {
            JsonDocument config;
            config["icon"] = "mdi:fan-clock";
            config["entity_category"] = "config";
            haPublishEntity("switch", key + "post_print", label + "Post Print", config, true, fan2Setting);
        }
        {
            JsonDocument config;
            haAddRange(config, 1, postPrintMaxSeconds, "s", "box");
            config["device_class"] = "duration";
            config["entity_category"] = "config";
            haPublishEntity("number", key + "post_print_seconds", label + "Post Print duration", config, true, fan2Setting);
        }
        {
            JsonDocument config;
            haAddRange(config, 0, 100, "%", "slider");
            config["icon"] = "mdi:speedometer";
            config["entity_category"] = "config";
            haPublishEntity("number", key + "post_print_speed", label + "Post Print speed", config, true, fan2Setting);
        }
    }
    LogSerial.println(F("[HA] Published discovery configs"));
}

// Retained, and sent only when it changes.
void haPublishAvailability(const char *topic, bool online, int &last) {
    if (online != last && haPublish(haBaseTopic + topic, online ? "online" : "offline")) last = online;
}

// Publishes the retained state when it has changed; every entity but the diagnostics reads its field from it.
void haPublishState() {
    const uint32_t now = millis();
    haLastStateCheck = now;
    haStatePending = false;
    JsonDocument doc;
    doc["temperature"] = std::round(printerVariables.nozzletemp * 10) / 10;
    doc["printer_connected"] = printerVariables.online ? "ON" : "OFF";
    doc["printer_error"] = printerError() ? "ON" : "OFF";
    if (printerVariables.hmsCode) {
        doc["hms_code"] = hmsCodeText(printerVariables.hmsCode);
        doc["hms_severity"] = hmsSeverityName(hmsSeverity(printerVariables.hmsCode));
    } else {
        doc["hms_code"] = nullptr;
        doc["hms_severity"] = nullptr;
    }
    doc["control_mode"] = haControlModeNames[static_cast<int>(printerConfig.controlMode)];
    doc["override"] = globalVariables.haOverride ? "ON" : "OFF";
    doc["synchronized"] = printerConfig.fansSynchronized ? "ON" : "OFF";
    for (int fan = 0; fan < 2; ++fan) {
        const String key = String("fan") + String(fan + 1) + "_";
        // Until Fan 2 is first configured, its settings match Fan 1's, as on the Settings page.
        const FanSettings settings = fanSettings(printerConfig, fan == 1 && !printerConfig.fanGraph2.empty() ? 1 : 0);
        const int speed = fan == 0 ? globalVariables.fanSpeed : globalVariables.fanSpeed2;
        doc[String("fan") + String(fan + 1)] = speed > 0 ? "ON" : "OFF";
        doc[key + "speed"] = speed;
        doc[key + "post_print_remaining"] = postPrintSecondsRemaining(fan, now);
        doc[key + "mode"] = haModeNames[haModeIndex(settings)];
        doc[key + "static_speed"] = settings.staticSpeed;
        doc[key + "source"] = haSourceNames[static_cast<int>(settings.printerFanSource)];
        doc[key + "post_print"] = settings.postPrint.enabled ? "ON" : "OFF";
        doc[key + "post_print_seconds"] = settings.postPrint.seconds;
        doc[key + "post_print_speed"] = settings.postPrint.speed;
    }
    String payload;
    serializeJson(doc, payload);
    if (payload != haLastState && haPublish(haBaseTopic + "/state", payload)) haLastState = payload;
    haPublishAvailability("/fan2/availability", !printerConfig.fansSynchronized, haLastFan2Online);
    haPublishAvailability("/control/availability", printerConfig.controlMode != ControlMode::Printer, haLastControlOnline);
}

void haPublishDiagnostics() {
    haLastDiagnostics = millis();
    JsonDocument doc;
    doc["rssi"] = WiFi.RSSI();
    doc["free_memory"] = freeMemory();
    doc["largest_block"] = largestFreeBlock();
    haPublishJson(haBaseTopic + "/diagnostics", doc);
}

void haPublishAll() {
    haDiscoveryPending = false;
    haPublishDiscovery();
    haPublish(haBaseTopic + "/status", "online");
    // Republish the state too, in case the broker lost its retained copy.
    haLastState = "";
    haLastFan2Online = haLastControlOnline = -1;
    haPublishState();
    haPublishDiagnostics();
}

// --- Connection -------------------------------------------------------------

bool haConnect() {
    haMqtt.setServer(printerConfig.haHost, printerConfig.haPort);
    LogSerial.print(F("[HA] Connecting to "));
    LogSerial.print(printerConfig.haHost);
    LogSerial.print(F(":"));
    LogSerial.println(printerConfig.haPort);
    const char *user = strlen(printerConfig.haUser) > 0 ? printerConfig.haUser : nullptr;
    const char *password = strlen(printerConfig.haPassword) > 0 ? printerConfig.haPassword : nullptr;
    const String availability = haBaseTopic + "/status";
    const bool connected = haMqtt.connect(haNodeId.c_str(), user, password, availability.c_str(), 1, true, "offline");
    haAttempted = true;
    haLastAttempt = millis();
    if (!connected) {
        haRetryMs = std::min(std::max(haRetryMs * 2, haRetryMinimumMs), haRetryMaximumMs);
        LogSerial.print(F("[HA] Connection failed with state "));
        LogSerial.println(haMqtt.state());
        return false;
    }
    haRetryMs = 0;
    LogSerial.println(F("[HA] Connected"));
    haMqtt.subscribe((haBaseTopic + "/set/+").c_str());
    haMqtt.subscribe(haBirthTopic);
    haPublishAll();
    return true;
}

// Drops the connection after its settings change; the loop reconnects with the new ones.
void haSettingsChanged() {
    if (haMqtt.connected()) {
        // A clean disconnect does not send the will, so mark the entities unavailable first.
        haPublish(haBaseTopic + "/status", "offline");
        haMqtt.disconnect();
    }
    haAttempted = false;
    haRetryMs = 0;
}

// Republishes discovery so Home Assistant shows the new device name, unless it was renamed there.
// A connection made later publishes it anyway.
void haNameChanged() {
    haDiscoveryPending = true;
}

const char *homeAssistantStatus() {
    if (!printerConfig.haEnabled) return "disabled";
    if (haMqtt.connected()) return "connected";
    return haAttempted ? "disconnected" : "connecting";
}

void setupHomeAssistant() {
    const String mac = WiFi.macAddress();
    String id;
    for (size_t i = 0; i < mac.length(); ++i) {
        if (mac[i] != ':') id += static_cast<char>(tolower(static_cast<unsigned char>(mac[i])));
    }
    haNodeId = String("blsmartflow_") + id;
    haBaseTopic = String("blsmartflow/") + id;
    setConnectTimeout(haClient, 3000);
    haMqtt.setBufferSize(512);
    haMqtt.setKeepAlive(30);    // Tolerates the loop pausing for a printer reconnect
    haMqtt.setSocketTimeout(5);
    haMqtt.setCallback(haCallback);
    haConfigured = true;
    LogSerial.print(F("[HA] Node ID: "));
    LogSerial.println(haNodeId);
}

void homeAssistantLoop() {
    if (!haConfigured || !printerConfig.haEnabled || WiFi.status() != WL_CONNECTED) return;
    if (!haMqtt.connected()) {
        if (millis() - haLastAttempt >= haRetryMs) haConnect();
        return;
    }
    haMqtt.loop();
    if (!haMqtt.connected()) return;
    if (haRestartPending) {
        haRestartPending = false;
        LogSerial.println(F("[HA] Restart requested from Home Assistant"));
        // A clean disconnect does not send the will, so mark the entities unavailable first.
        haPublish(haBaseTopic + "/status", "offline");
        haMqtt.disconnect();
        delay(500);
        ESP.restart();
        return;
    }
    if (haDiscoveryPending) haPublishAll();
    if (haStatePending || millis() - haLastStateCheck >= haStateIntervalMs) haPublishState();
    if (millis() - haLastDiagnostics >= haDiagnosticsIntervalMs) haPublishDiagnostics();
}

#endif
