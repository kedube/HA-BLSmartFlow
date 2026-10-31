#ifndef BLFLOW_HOME_ASSISTANT_H
#define BLFLOW_HOME_ASSISTANT_H

// Home Assistant integration over a second MQTT connection, to the user's broker.
// MQTT Discovery creates sensors for monitoring and controls that mirror the fan
// Settings. Commands are saved to flash before they run, like web page changes.
// The printer connection is unaffected. The connection is plain MQTT: a second
// TLS session alongside the printer's leaves the ESP32 short of memory.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <cmath>
#include <strings.h>

#include "connect-timeout.h"
#include "types.h"
#include "fan-config.h"
#include "fans.h"
#include "filesystem.h"
#include "log-serial.h"
#include "mqtt-report.h"
#include "post-print.h"

const uint32_t haRetryMinimumMs = 5000;
const uint32_t haRetryMaximumMs = 60000;
const uint32_t haStateIntervalMs = 1000;
const char *const haBirthTopic = "homeassistant/status";

constexpr int haModeCurve = 0, haModeStatic = 1, haModeFollow = 2;
const char *const haModeNames[] = {"Temperature Curve", "Static Fan Speed", "Follow Printer Fan"};
// Indexed by PrinterFanSource, with the Settings page's labels.
const char *const haSourceNames[] = {"Part Cooling Fan", "Auxiliary Cooling Fan", "Chamber/Exhaust Fan", "Hotend/Heatbreak Fan"};

WiFiClient haClient;
PubSubClient haMqtt(haClient);

String haNodeId;        // blsmartflow_<mac>; also the client ID, so a reboot replaces the old session
String haBaseTopic;     // blsmartflow/<mac>
String haLastState;
int haLastFan2Online = -1;
bool haConfigured = false;
bool haAttempted = false;
bool haDiscoveryPending = false;
bool haStatePending = false;
uint32_t haLastAttempt = 0;
uint32_t haRetryMs = 0;  // Wait before the next attempt; grows while attempts fail
uint32_t haLastStateCheck = 0;

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
    if (a.fansSynchronized != b.fansSynchronized || a.fanGraph2 != b.fanGraph2) return false;
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

// Stages one command. Rejects unknown keys, invalid values, and Fan 2 while synchronized.
bool haReadCommand(const char *key, const String &value, PrinterConfig &config) {
    if (strcmp(key, "synchronized") == 0) {
        if (!haReadSwitch(value, config.fansSynchronized)) return false;
        if (!config.fansSynchronized) initializeSecondFan(config);
        return true;
    }
    int fan;
    if (strncmp(key, "fan1_", 5) == 0) fan = 0;
    else if (strncmp(key, "fan2_", 5) == 0) fan = 1;
    else return false;
    // As on the Settings page, Fan 2's settings are dormant while synchronized.
    if (fan == 1 && config.fansSynchronized) return false;
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

void haApplyCommand(const char *key, const String &value) {
    PrinterConfig candidate = printerConfig;
    if (!haReadCommand(key, value, candidate)) {
        LogSerial.print(F("[HA] Ignored command "));
        LogSerial.print(key);
        LogSerial.print(F(": "));
        LogSerial.println(value);
        return;
    }
    if (sameFanSettings(printerConfig, candidate)) return;
    if (!saveFileSystem(candidate)) {
        LogSerial.println(F("[HA] Failed to save the command; settings unchanged"));
        return;
    }
    applySettings(std::move(candidate));
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
    device["name"] = "BL Smart Flow+";
    device["manufacturer"] = "DutchDeveloper";
    device["model"] = "BLSmartFlow ESP32";
    device["sw_version"] = globalVariables.FWVersion;
    device["configuration_url"] = String("http://") + WiFi.localIP().toString() + "/";
}

// Adds the fields every entity shares, then publishes its retained discovery config.
void haPublishEntity(const char *component, const String &key, const String &name, JsonDocument &config,
                     bool control, bool fan2Setting) {
    config["name"] = name;
    config["unique_id"] = haNodeId + "_" + key;
    config["state_topic"] = haBaseTopic + "/state";
    config["value_template"] = String("{{ value_json.") + key + " }}";
    if (control) config["command_topic"] = haBaseTopic + "/set/" + key;
    JsonArray availability = config["availability"].to<JsonArray>();
    availability.add<JsonObject>()["topic"] = haBaseTopic + "/status";
    if (fan2Setting) {
        availability.add<JsonObject>()["topic"] = haBaseTopic + "/fan2/availability";
        config["availability_mode"] = "all";
    }
    haAddDevice(config["device"].to<JsonObject>());
    haPublishJson(String("homeassistant/") + component + "/" + haNodeId + "/" + key + "/config", config);
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
        config["icon"] = "mdi:link-variant";
        config["entity_category"] = "config";
        haPublishEntity("switch", "synchronized", "Synchronize fans", config, true, false);
    }
    for (int fan = 0; fan < 2; ++fan) {
        const String key = String("fan") + String(fan + 1) + "_";
        const String label = String("Fan ") + String(fan + 1) + " ";
        const bool fan2Setting = fan == 1;
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

// Publishes the retained state when it has changed; every entity reads its field from it.
void haPublishState() {
    const uint32_t now = millis();
    haLastStateCheck = now;
    haStatePending = false;
    JsonDocument doc;
    doc["temperature"] = std::round(printerVariables.nozzletemp * 10) / 10;
    doc["printer_connected"] = printerVariables.online ? "ON" : "OFF";
    doc["synchronized"] = printerConfig.fansSynchronized ? "ON" : "OFF";
    for (int fan = 0; fan < 2; ++fan) {
        const String key = String("fan") + String(fan + 1) + "_";
        // Until Fan 2 is first configured, its settings match Fan 1's, as on the Settings page.
        const FanSettings settings = fanSettings(printerConfig, fan == 1 && !printerConfig.fanGraph2.empty() ? 1 : 0);
        doc[key + "speed"] = fan == 0 ? globalVariables.fanSpeed : globalVariables.fanSpeed2;
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
    const int fan2Online = printerConfig.fansSynchronized ? 0 : 1;
    if (fan2Online != haLastFan2Online &&
        haPublish(haBaseTopic + "/fan2/availability", fan2Online ? "online" : "offline")) {
        haLastFan2Online = fan2Online;
    }
}

void haPublishAll() {
    haPublishDiscovery();
    haPublish(haBaseTopic + "/status", "online");
    // Republish the state too, in case the broker lost its retained copy.
    haLastState = "";
    haLastFan2Online = -1;
    haPublishState();
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
    if (haDiscoveryPending) {
        haDiscoveryPending = false;
        haPublishAll();
    }
    if (haStatePending || millis() - haLastStateCheck >= haStateIntervalMs) haPublishState();
}

#endif
