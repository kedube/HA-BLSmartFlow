#include <set>
#include "src/blflow/serialmanager.h"

const std::string haNode = "blsmartflow_aabbcc0d0e0f";
const std::string haBase = "blsmartflow/aabbcc0d0e0f";

void enableHomeAssistant(std::map<std::string,String> extra = {}) {
    std::map<std::string,String> params = {{"haEnabled","on"},{"haHost","homeassistant.local"},{"haPort","1883"},
        {"haUser","mqtt-user"},{"haPassword","test-broker-password"}};
    for (const auto &[name, value] : extra) params[name] = value;
    assert(options(params) == 200);
}

void startHomeAssistant() {
    setupHomeAssistant();
    haMqtt.connectResult = true;
    homeAssistantLoop();
    assert(haMqtt.connected());
}

void command(const std::string &key, const std::string &value) {
    haMqtt.deliver(haBase + "/set/" + key, value);
    homeAssistantLoop();
}

const Published *lastPublished(const std::string &topic) {
    for (auto it = haMqtt.published.rbegin(); it != haMqtt.published.rend(); ++it) {
        if (it->topic == topic) return &*it;
    }
    return nullptr;
}

JsonDocument haState() {
    const Published *message = lastPublished(haBase + "/state");
    assert(message && message->retained);
    JsonDocument json;
    assert(!deserializeJson(json, message->payload));
    return json;
}

int discoveryCount() {
    int count = 0;
    for (const auto &message : haMqtt.published) count += message.topic.rfind("homeassistant/", 0) == 0;
    return count;
}

// Every discovery config published since the last clear names the same device.
std::string discoveredDeviceName() {
    std::set<std::string> names;
    for (const auto &message : haMqtt.published) {
        if (message.topic.rfind("homeassistant/", 0) != 0) continue;
        assert(message.topic.find("/" + haNode + "/") != std::string::npos);
        JsonDocument config; assert(!deserializeJson(config, message.payload));
        assert(config["device"]["identifiers"][0] == haNode);
        names.insert(config["device"]["name"].as<std::string>());
    }
    assert(names.size() == 1);
    return *names.begin();
}

void homeAssistantTests() {
    check("Home Assistant settings persist and the broker password is never sent to the page", [] {
        putConfig(legacyConfig); reboot();
        assert(!printerConfig.haEnabled && printerConfig.haPort == 1883);
        enableHomeAssistant(); reboot(); assertCredentials();
        assert(printerConfig.haEnabled && std::string(printerConfig.haHost) == "homeassistant.local");
        assert(std::string(printerConfig.haUser) == "mqtt-user");
        assert(std::string(printerConfig.haPassword) == "test-broker-password");
        handleGetOptions(); auto json = response();
        assert(json["haEnabled"] == true && json["haHost"] == "homeassistant.local" && json["haPort"] == 1883);
        assert(json["haUser"] == "mqtt-user" && json["haPasswordSet"] == true && json["haPassword"].isNull());
        assert(webServer.lastBody.find("test-broker-password") == std::string::npos);
        // A blank password keeps the saved one; clearing the username removes both.
        assert(options({{"haPassword",""},{"haPort","1884"}}) == 200);
        assert(std::string(printerConfig.haPassword) == "test-broker-password" && printerConfig.haPort == 1884);
        assert(options({{"haUser",""},{"haPassword",""}}) == 200);
        assert(!printerConfig.haPassword[0]);
        handleGetOptions(); assert(response()["haPasswordSet"] == false);
        reboot(); assert(!printerConfig.haPassword[0]);
    });
    check("saved Home Assistant settings with no host or an invalid port load safely", [] {
        JsonDocument json; deserializeJson(json, legacyConfig);
        json["haEnabled"] = true; json["haHost"] = ""; json["haPort"] = 70000;
        std::string payload; serializeJson(json, payload); putConfig(payload); reboot();
        assert(!printerConfig.haEnabled && printerConfig.haPort == 1883);
        assertCredentials(); assert(savedConfig() == payload);
    });
    check("keeping the broker password requires the same broker", [] {
        enableHomeAssistant();
        const auto disk = savedConfig();
        for (const auto &params : std::vector<std::map<std::string,String>>{
            {{"haHost","attacker.example"}}, {{"haHost","attacker.example"},{"haPassword",""}}}) {
            assert(options(params) == 400);
            assert(savedConfig() == disk && std::string(printerConfig.haHost) == "homeassistant.local");
        }
        assert(options({{"haHost","HomeAssistant.LOCAL"},{"haPort","1884"}}) == 200);
        assert(std::string(printerConfig.haPassword) == "test-broker-password" && printerConfig.haPort == 1884);
        assert(options({{"haHost","broker.example"},{"haPassword","new-password"}}) == 200);
        assert(std::string(printerConfig.haHost) == "broker.example");
        assert(std::string(printerConfig.haPassword) == "new-password");
    });
    check("invalid Home Assistant settings are rejected without changing configuration", [] {
        assert(options({{"haEnabled","on"},{"haHost",""}}) == 400);
        assert(options({{"haEnabled","on"},{"haHost","   "}}) == 400);
        assert(!printerConfig.haEnabled);
        enableHomeAssistant();
        const auto disk = savedConfig();
        for (const auto &params : std::vector<std::map<std::string,String>>{
            {{"haPort","0"}}, {{"haPort","65536"}}, {{"haPort","abc"}}, {{"haPort",""}}, {{"haPort","18.83"}},
            {{"haEnabled","maybe"}}, {{"haHost",std::string(65,'h')}},
            {{"haUser",std::string(65,'u')}}, {{"haPassword",std::string(65,'p')}}, {{"haHost",""}}}) {
            assert(options(params) == 400);
            assert(savedConfig() == disk && printerConfig.haPort == 1883);
        }
        failRename = true;
        assert(options({{"haPort","1884"}}) == 500);
        assert(printerConfig.haPort == 1883 && savedConfig() == disk);
    });
    check("a disabled integration never connects", [] {
        enableHomeAssistant({{"haEnabled","off"}});
        setupHomeAssistant();
        haMqtt.connectResult = true;
        for (int i = 0; i < 3; ++i) { mockMillis += 60000; homeAssistantLoop(); }
        assert(haMqtt.connects == 0 && homeAssistantStatus() == std::string("disabled"));
        handleSensorData(); assert(response()["homeAssistant"] == "disabled");
    });
    check("connecting publishes retained discovery for every entity, then availability and state", [] {
        enableHomeAssistant(); startHomeAssistant();
        assert(haMqtt.host == "homeassistant.local" && haMqtt.port == 1883);
        assert(haMqtt.clientId == haNode && haMqtt.user == "mqtt-user" && haMqtt.password == "test-broker-password");
        assert(haMqtt.willTopic == haBase + "/status" && haMqtt.willMessage == "offline" && haMqtt.willRetain);
        assert(haMqtt.client == &haClient && haClient.connectTimeout == 3000);
        const std::set<std::string> subscriptions(haMqtt.subscriptions.begin(), haMqtt.subscriptions.end());
        assert(subscriptions == std::set<std::string>({haBase + "/set/+", "homeassistant/status"}));
        const JsonDocument state = haState();
        std::set<std::string> keys;
        size_t lastDiscovery = 0;
        for (size_t i = 0; i < haMqtt.published.size(); ++i) {
            const Published &message = haMqtt.published[i];
            assert(message.retained);
            if (message.topic.rfind("homeassistant/", 0) != 0) continue;
            lastDiscovery = i;
            JsonDocument config; assert(!deserializeJson(config, message.payload));
            const std::string uniqueId = config["unique_id"];
            assert(uniqueId.rfind(haNode + "_", 0) == 0);
            const std::string key = uniqueId.substr(haNode.size() + 1);
            assert(keys.insert(key).second);
            assert(message.topic.size() > key.size() + 8 &&
                   message.topic.compare(message.topic.size() - key.size() - 8, std::string::npos,
                                         "/" + key + "/config") == 0);
            assert(message.topic.find("/" + haNode + "/") != std::string::npos);
            assert(config["state_topic"] == haBase + "/state");
            assert(config["value_template"] == "{{ value_json." + key + " }}");
            assert(!state[key].isNull()); // Each entity reads a field that the state provides.
            assert(config["device"]["identifiers"][0] == haNode);
            assert(config["device"]["configuration_url"] == "http://192.0.2.10/");
            const bool control = !config["command_topic"].isNull();
            if (control) assert(config["command_topic"] == haBase + "/set/" + key);
            const bool fan2Setting = control && key.rfind("fan2_", 0) == 0;
            assert(config["availability"].size() == (fan2Setting ? 2u : 1u));
            assert(config["availability"][0]["topic"] == haBase + "/status");
            if (fan2Setting) {
                assert(config["availability"][1]["topic"] == haBase + "/fan2/availability");
                assert(config["availability_mode"] == "all");
            }
        }
        assert(keys.size() == 19);
        // Discovery precedes "online", so Home Assistant knows the entities first.
        const Published *online = lastPublished(haBase + "/status");
        assert(online && online->payload == "online" && online->retained);
        assert(online > &haMqtt.published[lastDiscovery]);
        assert(state["temperature"] == 0 && state["printer_connected"] == "OFF" && state["synchronized"] == "ON");
        assert(state["fan1_mode"] == "Temperature Curve" && state["fan1_source"] == "Chamber/Exhaust Fan");
        assert(state["fan1_post_print"] == "OFF" && state["fan1_post_print_seconds"] == 600);
        assert(state["fan2_post_print_speed"] == 100 && state["fan2_static_speed"] == 0);
        assert(lastPublished(haBase + "/fan2/availability")->payload == "offline");
        handleSensorData(); auto json = response();
        assert(json["homeAssistant"] == "connected" && json["homeAssistantState"] == 0);
    });
    check("Home Assistant commands are saved, drive the outputs, and are reported", [] {
        enableHomeAssistant(); startHomeAssistant();
        const auto revision = globalVariables.settingsRevision;
        command("fan1_static_speed", "42");
        command("fan1_mode", "Static Fan Speed");
        assert(globalVariables.settingsRevision == revision + 2);
        assertPWM(200, 42, 42);
        auto state = haState();
        assert(state["fan1_mode"] == "Static Fan Speed" && state["fan1_static_speed"] == 42 && state["fan1_speed"] == 42);
        command("fan1_mode", "Follow Printer Fan");
        assert(printerConfig.followPrinterFan && !printerConfig.staticFan);
        command("fan1_source", "Hotend/Heatbreak Fan");
        assert(printerConfig.printerFanSource == PrinterFanSource::Heatbreak);
        command("fan1_source", "part");
        assert(printerConfig.printerFanSource == PrinterFanSource::Part);
        report(R"({"print":{"cooling_fan_speed":"9"}})"); assertPWM(200, 60, 60);
        command("fan1_mode", "Temperature Curve"); assertPWM(200, 59, 59);
        command("fan1_post_print", "ON");
        command("fan1_post_print_seconds", "30");
        command("fan1_post_print_speed", "90");
        finishPrint(); assertPWM(200, 90, 90);
        mockMillis += 1000; homeAssistantLoop();
        state = haState();
        assert(state["fan1_post_print_remaining"] == 29 && state["fan2_post_print_remaining"] == 29);
        assert(state["fan1_speed"] == 90 && state["printer_connected"] == "ON");
        reboot();
        assert(!printerConfig.staticFan && !printerConfig.followPrinterFan && printerConfig.staticFanSpeed == 42);
        assert(printerConfig.printerFanSource == PrinterFanSource::Part);
        assert(printerConfig.postPrint.enabled && printerConfig.postPrint.seconds == 30 && printerConfig.postPrint.speed == 90);
    });
    check("synchronization from Home Assistant controls Fan 2 availability and preserves its settings", [] {
        enableHomeAssistant(); startHomeAssistant();
        const auto disk = savedConfig();
        command("fan2_static_speed", "55"); // Dormant while synchronized
        assert(savedConfig() == disk && printerConfig.fanGraph2.empty());
        command("fan1_mode", "Static Fan Speed");
        command("fan1_static_speed", "30");
        command("synchronized", "OFF");
        assert(!printerConfig.fansSynchronized && printerConfig.staticFan2 && printerConfig.staticFanSpeed2 == 30);
        assert(lastPublished(haBase + "/fan2/availability")->payload == "online");
        command("fan2_static_speed", "55"); assertPWM(200, 30, 55);
        assert(haState()["fan2_static_speed"] == 55);
        command("synchronized", "on"); assertPWM(200, 30, 30);
        assert(lastPublished(haBase + "/fan2/availability")->payload == "offline");
        assert(printerConfig.staticFanSpeed2 == 55 && haState()["fan2_speed"] == 30);
        reboot(); assert(printerConfig.fansSynchronized && printerConfig.staticFanSpeed2 == 55);
    });
    check("invalid commands, unchanged values, and failed writes leave settings and flash alone", [] {
        enableHomeAssistant(); startHomeAssistant();
        const auto disk = savedConfig();
        const auto revision = globalVariables.settingsRevision;
        for (const auto &[key, value] : std::vector<std::pair<std::string,std::string>>{
            {"fan1_static_speed","101"}, {"fan1_static_speed","-1"}, {"fan1_static_speed","4.5"},
            {"fan1_static_speed",""}, {"fan1_static_speed","99999999999"}, {"fan1_mode","Turbo"},
            {"fan1_mode","static fan speed"}, {"fan1_source","Exhaust"}, {"fan1_post_print","maybe"},
            {"fan1_post_print_seconds","0"}, {"fan1_post_print_seconds","86401"}, {"fan1_post_print_speed","101"},
            {"synchronized","yes"}, {"fan3_mode","Static Fan Speed"}, {"fan1_unknown","1"}, {"temperature","20"}}) {
            command(key, value);
            assert(savedConfig() == disk && globalVariables.settingsRevision == revision);
        }
        haMqtt.deliver("blsmartflow/other/set/fan1_mode", "Static Fan Speed");
        haMqtt.deliver(haBase + "/state", "Static Fan Speed");
        assert(!printerConfig.staticFan && savedConfig() == disk);
        failRename = true;
        command("fan1_mode", "Static Fan Speed");
        assert(!printerConfig.staticFan && savedConfig() == disk && globalVariables.settingsRevision == revision);
        assert(haState()["fan1_mode"] == "Temperature Curve");
        failRename = false;
        LittleFS.files.erase(configPath);
        command("fan1_mode", "Temperature Curve"); // Already set: nothing is written
        assert(!LittleFS.exists(configPath) && globalVariables.settingsRevision == revision);
    });
    check("retries back off, Home Assistant restarts republish discovery, and broker changes reconnect", [] {
        enableHomeAssistant(); setupHomeAssistant();
        haMqtt.failureState = 4;
        mockMillis = 1000; homeAssistantLoop();
        assert(haMqtt.connects == 1 && homeAssistantStatus() == std::string("disconnected"));
        handleSensorData(); auto json = response();
        assert(json["homeAssistant"] == "disconnected" && json["homeAssistantState"] == 4);
        for (uint32_t wait : {5000u, 10000u, 20000u, 40000u, 60000u, 60000u}) {
            const int attempts = haMqtt.connects;
            mockMillis += wait - 1; homeAssistantLoop(); assert(haMqtt.connects == attempts);
            mockMillis += 1; homeAssistantLoop(); assert(haMqtt.connects == attempts + 1);
        }
        haMqtt.connectResult = true;
        mockMillis += 60000; homeAssistantLoop(); assert(haMqtt.connected());
        // A dropped connection is retried at once.
        haMqtt.dropDuringLoop = true; homeAssistantLoop(); assert(!haMqtt.connected());
        haMqtt.dropDuringLoop = false;
        int attempts = haMqtt.connects;
        homeAssistantLoop(); assert(haMqtt.connects == attempts + 1 && haMqtt.connected());
        haMqtt.published.clear();
        haMqtt.deliver("homeassistant/status", "offline"); homeAssistantLoop();
        assert(discoveryCount() == 0);
        haMqtt.deliver("homeassistant/status", "online"); homeAssistantLoop();
        assert(discoveryCount() == 19 && lastPublished(haBase + "/status")->payload == "online");
        assert(lastPublished(haBase + "/state") && lastPublished(haBase + "/fan2/availability"));
        // Changing the broker marks the entities unavailable on the old one first.
        haMqtt.published.clear();
        assert(options({{"haHost","broker.example"},{"haPassword","another-password"}}) == 200);
        assert(haMqtt.published.size() == 1 && haMqtt.published[0].topic == haBase + "/status");
        assert(haMqtt.published[0].payload == "offline" && haMqtt.published[0].retained);
        assert(!haMqtt.connected() && homeAssistantStatus() == std::string("connecting"));
        homeAssistantLoop();
        assert(haMqtt.connected() && haMqtt.host == "broker.example" && haMqtt.password == "another-password");
        attempts = haMqtt.connects;
        assert(options({{"staticfan","on"},{"staticfanspeed","10"}}) == 200);
        homeAssistantLoop(); assert(haMqtt.connects == attempts && haMqtt.connected());
        haMqtt.published.clear();
        assert(options({{"haEnabled","off"}}) == 200);
        assert(haMqtt.published.back().payload == "offline" && !haMqtt.connected());
        mockMillis += 120000; homeAssistantLoop();
        assert(haMqtt.connects == attempts && homeAssistantStatus() == std::string("disabled"));
    });
    check("web changes reach Home Assistant within a second and open pages see a new settings revision", [] {
        enableHomeAssistant(); startHomeAssistant();
        handleGetOptions(); const int revision = response()["settingsRevision"];
        haMqtt.published.clear();
        assert(options({{"staticfan","on"},{"staticfanspeed","64"}}) == 200);
        handleSensorData(); assert(response()["settingsRevision"] == revision + 1);
        mockMillis += 999; homeAssistantLoop(); assert(!lastPublished(haBase + "/state"));
        mockMillis += 1; homeAssistantLoop();
        auto state = haState();
        assert(state["fan1_mode"] == "Static Fan Speed" && state["fan1_static_speed"] == 64 && state["fan1_speed"] == 64);
        haMqtt.published.clear();
        mockMillis += 5000; homeAssistantLoop(); assert(haMqtt.published.empty()); // Unchanged state is not resent
        assert(curve(newCurve) == 200);
        handleSensorData(); assert(response()["settingsRevision"] == revision + 1);
        // Rounded to tenths, as published.
        printerVariables.nozzletemp = 210.04;
        mockMillis += 1000; homeAssistantLoop();
        assert(lastPublished(haBase + "/state")->payload.find("\"temperature\":210,") != std::string::npos);
        printerVariables.nozzletemp = 210.06;
        mockMillis += 1000; homeAssistantLoop();
        assert(lastPublished(haBase + "/state")->payload.find("\"temperature\":210.1,") != std::string::npos);
    });
    check("the controller name labels the Home Assistant device and sets the .local address", [] {
        putConfig(legacyConfig); reboot();
        enableHomeAssistant(); startMdns(); startHomeAssistant();
        assert(discoveredDeviceName() == "BL Smart Flow+" && MDNS.hostname == "blflow");
        handleGetOptions(); assert(response()["controllerName"] == "");
        // Renaming republishes discovery on the same connection, under the same identifiers.
        const int connects = haMqtt.connects;
        haMqtt.published.clear();
        assert(options({{"controllerName","  Garage P1S  "}}) == 200);
        assert(std::string(printerConfig.controllerName) == "Garage P1S" && MDNS.hostname == "garage-p1s");
        homeAssistantLoop();
        assert(haMqtt.connects == connects && discoveryCount() == 19 && discoveredDeviceName() == "Garage P1S");
        assert(lastPublished(haBase + "/status")->payload == "online");
        reboot(); assertCredentials();
        assert(std::string(printerConfig.controllerName) == "Garage P1S" && controllerHostname() == "garage-p1s");
        handleGetOptions(); assert(response()["controllerName"] == "Garage P1S");
        // Other settings, or a name with the same address, leave the .local name alone.
        const int starts = MDNS.starts;
        haMqtt.published.clear();
        assert(options({{"staticfan","on"},{"staticfanspeed","10"}}) == 200);
        homeAssistantLoop(); assert(discoveryCount() == 0 && MDNS.starts == starts);
        assert(options({{"controllerName","garage p1s"}}) == 200);
        homeAssistantLoop(); assert(discoveredDeviceName() == "garage p1s" && MDNS.starts == starts);
        // Only letters and digits reach the address; without any, it keeps the default.
        assert(options({{"controllerName","Café #2 — X1C"}}) == 200 && MDNS.hostname == "caf-2-x1c");
        assert(options({{"controllerName","!!!"}}) == 200 && MDNS.hostname == "blflow");
        haMqtt.published.clear();
        assert(options({{"controllerName",""}}) == 200 && MDNS.hostname == "blflow");
        homeAssistantLoop(); assert(discoveredDeviceName() == "BL Smart Flow+");
        const auto disk = savedConfig();
        assert(options({{"controllerName",std::string(33,'n')}}) == 400);
        assert(savedConfig() == disk && !printerConfig.controllerName[0]);
        assert(options({{"controllerName",std::string(32,'n')}}) == 200 && MDNS.hostname == std::string(32,'n'));
    });
    check("a rename while disconnected reaches Home Assistant once on connecting", [] {
        enableHomeAssistant({{"controllerName","Workshop"}}); setupHomeAssistant();
        haMqtt.connectResult = true;
        assert(options({{"controllerName","Workshop A1"}}) == 200);
        homeAssistantLoop(); homeAssistantLoop();
        assert(discoveryCount() == 19 && discoveredDeviceName() == "Workshop A1");
    });
    check("printer reconnects back off from 3 to 30 seconds and bound the TLS connect time", [] {
        mqttClient.isConnected = false;
        setupMqtt();
        assert(wifiSecureClient.connectTimeout == 3000 && wifiSecureClient.handshakeTimeout == 10);
        const int first = mqttClient.connects;
        assert(first >= 1);
        for (uint32_t wait : {3000u, 6000u, 12000u, 24000u, 30000u, 30000u}) {
            const int attempts = mqttClient.connects;
            mockMillis += wait - 1; mqttloop(); assert(mqttClient.connects == attempts);
            mockMillis += 1; mqttloop(); assert(mqttClient.connects == attempts + 1);
        }
        mqttClient.connectResult = true;
        mockMillis += 30000; mqttloop();
        assert(mqttClient.connected() && printerVariables.online);
        mqttClient.connectResult = false;
        mqttClient.isConnected = false;
        const int attempts = mqttClient.connects;
        mqttloop(); assert(mqttClient.connects == attempts + 1); // First retry after a drop is immediate
    });
    check("serial provisioning stores full-length values and rejects oversized ones without saving", [] {
        putConfig(legacyConfig); reboot();
        const std::string password(63, 'p');
        Serial.input = R"({"ssid":"provisioned-network","pass":")" + password +
            R"(","printerip":"192.0.2.99","printercode":"CODE1234","printerserial":"SERIAL123456789"})" "\n";
        serialLoop();
        assert(std::string(globalVariables.APPW) == password);
        reboot();
        assert(std::string(globalVariables.SSID) == "provisioned-network" && std::string(globalVariables.APPW) == password);
        assert(std::string(printerConfig.printerIP) == "192.0.2.99" && std::string(printerConfig.accessCode) == "CODE1234");
        const auto disk = savedConfig();
        for (const std::string &bad : {
            R"({"ssid":"network","pass":")" + std::string(64, 'p') + R"("})",
            std::string(R"({"ssid":"network","pass":"password","printercode":"TOO-LONG-CODE"})"),
            std::string(R"({"ssid":"network","pass":"password","printerip":"192.0.2.100.200.300"})")}) {
            Serial.input = bad + "\n";
            serialLoop();
            assert(savedConfig() == disk && std::string(globalVariables.SSID) == "provisioned-network");
            assert(std::string(printerConfig.printerIP) == "192.0.2.99");
        }
        // Missing printer fields keep their values instead of crashing.
        Serial.input = R"({"ssid":"other-network","pass":"password"})" "\n";
        serialLoop(); reboot();
        assert(std::string(globalVariables.SSID) == "other-network" && std::string(printerConfig.printerIP) == "192.0.2.99");
    });
}
