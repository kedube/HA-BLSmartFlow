#include <cassert>
#include <iostream>
#include <limits>
#include "src/blflow/web-server.h"
#include "src/blflow/fans.h"
#include "src/blflow/mqtt-report.h"
#include "src/blflow/mqttmanager.h"

const char *legacyConfig = R"({"ssid":"test-network","appw":"test-password","printerIp":"192.0.2.83","accessCode":"TEST1234","serialNumber":"TESTSERIAL12345","bssi":"","debuging":true,"debugingchange":false,"mqttdebug":false,"fanPoints":[{"temp":0,"speed":0},{"temp":200,"speed":80},{"temp":400,"speed":100}]})";
const char *newCurve = R"({"points":[{"temp":400,"speed":90},{"temp":0,"speed":10},{"temp":200,"speed":50}]})";
const char *secondCurve = R"({"points":[{"temp":0,"speed":30},{"temp":400,"speed":70}]})";

void reset() {
    printerConfig = PrinterConfig{};
    globalVariables = GlobalVariables{};
    printerVariables = PrinterVariables{};
    LittleFS.files.clear();
    failOpen = failRename = shortRead = false;
    writeBudget = SIZE_MAX;
    webServer.params.clear();
    webServer.responses.clear();
    pwmValues.clear();
    mockMillis = 0;
    WiFi = WiFiMock{};
    SSDP = SSDPMock{};
    MDNS = MDNSMock{};
    mockResetReason = ESP_RST_POWERON;
    mockUptimeMicros = 0;
    LogSerial = LogSerialClass{};
    printerTraffic = TrafficCounter{};
    stream.flush();
    mqttClient.isConnected = true;
    mqttClient.connectResult = mqttClient.dropDuringLoop = false;
    mqttClient.beforeConnect = {};
    mqttattempt = 0;
    mqttRetryMs = 0;
    haMqtt = PubSubClient(haClient);
    haMqtt.isConnected = false;
    haLastState = haNodeId = haBaseTopic = "";
    haLastFan2Online = -1;
    haConfigured = haAttempted = haDiscoveryPending = haStatePending = false;
    haLastAttempt = haRetryMs = haLastStateCheck = 0;
    webServer.headers.clear();
    webServer.credentials = {};
    webServer.authenticationRequests = 0;
    webServer.upload() = HTTPUpload{};
    webServer.responseHeaders.clear();
    Update = UpdateMock{};
    updateAccepted = false;
    restoreAllowed = false;
    restoreResult = nullptr;
    restoreFile = File();
    restoreBytes = 0;
    ESP.restarts = 0;
}
void putConfig(const std::string &json) {
    LittleFS.files[configPath] = std::make_shared<std::string>(json);
}
std::string savedConfig() { return *LittleFS.files.at(configPath); }
void reboot() {
    printerConfig = PrinterConfig{};
    globalVariables = GlobalVariables{};
    printerVariables = PrinterVariables{};
    mockMillis = 0;
    loadFileSystem();
}
bool report(const char *json) {
    return parsePrinterReport(reinterpret_cast<const uint8_t *>(json), strlen(json));
}
int options(std::map<std::string,String> params) {
    webServer.params = std::move(params);
    webServer.headers[ownPageHeader] = "1";
    webServer.responses.clear();
    submitOptions();
    assert(webServer.responses.size() == 1);
    return webServer.responses[0];
}
int curve(const char *json, const char *fan = nullptr) {
    webServer.params = {{"points", json}};
    webServer.headers[ownPageHeader] = "1";
    if (fan) webServer.params["fan"] = fan;
    webServer.responses.clear();
    handleUpdateFanConfig();
    assert(webServer.responses.size() == 1);
    return webServer.responses[0];
}
JsonDocument response() {
    JsonDocument json;
    assert(!deserializeJson(json, webServer.lastBody));
    return json;
}
void assertCredentials() {
    assert(std::string(printerConfig.printerIP) == "192.0.2.83");
    assert(std::string(printerConfig.accessCode) == "TEST1234");
    assert(std::string(printerConfig.serialNumber) == "TESTSERIAL12345");
    assert(std::string(globalVariables.SSID) == "test-network");
    assert(std::string(globalVariables.APPW) == "test-password");
}
void assertPWM(float temperature, int speed1, int speed2) {
    printerVariables.nozzletemp = temperature;
    fanloop();
    assert(globalVariables.fanSpeed == speed1 && globalVariables.fanSpeed2 == speed2);
    assert(pwmValues.at(17) == speed1 * 255 / 100);
    assert(pwmValues.at(16) == speed2 * 255 / 100);
}

template<class T> void check(const char *name, T test) {
    reset(); test(); std::cout << "PASS: " << name << '\n';
}

#include "post-print-tests.h"
#include "home-assistant-tests.h"
#include "web-security-tests.h"
#include "diagnostics-tests.h"

int main() {
    postPrintTests();
    homeAssistantTests();
    webSecurityTests();
    diagnosticsTests();
    check("legacy settings leave following disabled", [] {
        putConfig(legacyConfig); reboot();
        assert(!printerConfig.followPrinterFan && !printerConfig.followPrinterFan2);
        handleGetOptions(); auto json = response();
        assert(json["followPrinterFan"] == false && json["followPrinterFan2"] == false);
        assert(json["printerFanSource"] == "chamber");
        handleSensorData(); json = response();
        assert(json["printerFans"]["chamber"].isNull());
    });
    check("all four printer fans decode numeric and string speeds and drive outputs", [] {
        assert(report(R"({"print":{"cooling_fan_speed":"3","big_fan1_speed":6,"big_fan2_speed":"12","heatbreak_fan_speed":15,"nozzle_temper":210}})"));
        assert(printerVariables.nozzletemp == 210);
        const char *sources[] = {"part", "auxiliary", "chamber", "heatbreak"};
        const int expected[] = {20, 40, 80, 100};
        for (int fan = 0; fan < 4; ++fan) {
            assert(options({{"followPrinterFan", "on"}, {"printerFanSource", sources[fan]}}) == 200);
            assertPWM(200, expected[fan], expected[fan]);
        }
        handleSensorData(); auto json = response();
        for (int fan = 0; fan < 4; ++fan) assert(json["printerFans"][sources[fan]] == expected[fan]);
    });
    check("packed 8-bit PWM takes precedence and heatbreak remains independent", [] {
        JsonDocument json; json["print"]["fan_gear"] = 0x009933ff;
        json["print"]["cooling_fan_speed"] = "0";
        json["print"]["heatbreak_fan_speed"] = "6";
        std::string payload; serializeJson(json, payload); assert(report(payload.c_str()));
        assert(printerVariables.fans[0].percent == 100);
        assert(printerVariables.fans[1].percent == 20);
        assert(printerVariables.fans[2].percent == 60);
        assert(printerVariables.fans[3].percent == 40);
        assert(report(R"({"print":{"fan_gear":"0"}})"));
        assert(printerVariables.fans[0].percent == 0 && printerVariables.fans[2].percent == 0);
        assert(printerVariables.fans[3].percent == 40);
    });
    check("following starts at zero, retains delta readings and holds through disconnects", [] {
        assert(options({{"followPrinterFan", "on"}, {"printerFanSource", "chamber"}}) == 200);
        assertPWM(300, 0, 0);
        assert(report(R"({"print":{"big_fan2_speed":"9"}})"));
        assertPWM(300, 60, 60);
        assert(report(R"({"print":{"nozzle_temper":50,"cooling_fan_speed":15}})"));
        assertPWM(50, 60, 60);
        printerVariables.online = false; assertPWM(300, 60, 60);
        assert(report(R"({"print":{"big_fan2_speed":0}})")); assertPWM(300, 0, 0);
        reboot(); assert(printerConfig.followPrinterFan); assertPWM(300, 0, 0);
    });
    check("following a 70 percent chamber fan drives and reports 70 rather than 71", [] {
        assert(options({{"followPrinterFan","on"},{"printerFanSource","chamber"}}) == 200);
        // Captures the reported mismatch: packed PWM 180 rounded to 71% before.
        for (int pwm : {178, 179, 180}) {
            JsonDocument json; json["print"]["fan_gear"] = pwm << 16;
            std::string payload; serializeJson(json,payload); assert(report(payload.c_str()));
            assertPWM(200,70,70);
            handleSensorData(); json = response();
            assert(json["speed"] == 70 && json["printerFans"]["chamber"] == 70);
        }
        assert(report(R"({"print":{"big_fan2_speed":"11"}})")); assertPWM(200,70,70);
        assert(options({{"staticfan","on"},{"staticfanspeed","71"}}) == 200);
        assertPWM(200,71,71);
        assert(options({{"staticfan","off"}}) == 200);
        assert(curve(R"({"points":[{"temp":0,"speed":71}]})") == 200);
        assertPWM(200,71,71);
    });
    check("packed fan steps and legacy fan steps match Studio while heatbreak stays separate", [] {
        for (const auto &item : std::vector<std::pair<int,int>>{
            {0,0},{25,10},{26,10},{51,20},{76,30},{102,40},{127,50},{128,50},
            {153,60},{178,70},{180,70},{204,80},{229,90},{230,90},{255,100}}) {
            JsonDocument json;
            json["print"]["fan_gear"] = item.first * 0x010101;
            std::string payload; serializeJson(json,payload); assert(report(payload.c_str()));
            for (int fan=0; fan<3; ++fan) assert(printerVariables.fans[fan].percent == item.second);
        }
        assert(report(R"({"print":{"cooling_fan_speed":"5","big_fan1_speed":"8","big_fan2_speed":"11","heatbreak_fan_speed":"10"}})"));
        assert(printerVariables.fans[0].percent == 30);
        assert(printerVariables.fans[1].percent == 50);
        assert(printerVariables.fans[2].percent == 70);
        assert(printerVariables.fans[3].percent == 67);
    });
    check("malformed MQTT values cannot overwrite last valid speeds", [] {
        report(R"({"print":{"cooling_fan_speed":6,"big_fan2_speed":12}})");
        for (const char *bad : {"null", "true", "-1", "16", "1.5", "\"no\"", "\"\"", "\"15x\"", "\"999999999999999999999\"", "[]", "{}"}) {
            const auto payload = std::string("{\"print\":{\"big_fan2_speed\":") + bad + "}}";
            assert(!report(payload.c_str()));
            assert(printerVariables.fans[2].percent == 80);
        }
        assert(!report("{\"print\":{\"big_fan2_speed\":0"));
        assert(!report(R"({"print":{"fan_gear":"4294967296"}})"));
        assert(printerVariables.fans[0].percent == 40 && printerVariables.fans[2].percent == 80);
        assert(report(R"({"print":{"fan_gear":"invalid","big_fan2_speed":"3"}})"));
        assert(printerVariables.fans[2].percent == 20);
    });
    check("independent following persists and synchronization preserves Fan 2", [] {
        assert(options({{"fansSynchronized","off"},{"followPrinterFan","on"},{"printerFanSource","chamber"},
                        {"followPrinterFan2","on"},{"printerFanSource2","part"}}) == 200);
        reboot();
        report(R"({"print":{"big_fan2_speed":12,"cooling_fan_speed":3}})"); assertPWM(200,80,20);
        assert(options({{"fansSynchronized","on"},{"followPrinterFan2","off"},{"printerFanSource2","heatbreak"}}) == 200);
        assertPWM(200,80,80);
        assert(printerConfig.followPrinterFan2 && printerConfig.printerFanSource2 == PrinterFanSource::Part);
        assert(options({{"fansSynchronized","off"}}) == 200); assertPWM(200,80,20);
        handleGetOptions(); auto json=response();
        assert(json["followPrinterFan2"] == true && json["printerFanSource2"] == "part");
    });
    check("first independent save clones the current following setting", [] {
        assert(options({{"followPrinterFan","on"},{"printerFanSource","auxiliary"}}) == 200);
        assert(printerConfig.fanGraph2.empty());
        assert(options({{"fansSynchronized","off"}}) == 200); reboot();
        assert(printerConfig.followPrinterFan2 && printerConfig.printerFanSource2 == PrinterFanSource::Auxiliary);
    });
    check("override switches exclude each other and both off restore the curve", [] {
        assert(options({{"fansSynchronized","off"},{"staticfan","on"},{"staticfanspeed","23"},
                        {"staticfan2","on"},{"staticfanspeed2","37"}}) == 200);
        assert(options({{"followPrinterFan","on"},{"followPrinterFan2","on"}}) == 200);
        assert(!printerConfig.staticFan && !printerConfig.staticFan2);
        report(R"({"print":{"big_fan2_speed":12}})"); assertPWM(180,80,80);
        assert(options({{"staticfan","on"},{"staticfan2","on"}}) == 200);
        assert(!printerConfig.followPrinterFan && !printerConfig.followPrinterFan2); assertPWM(180,23,37);
        assert(options({{"staticfan","off"},{"followPrinterFan","off"},{"staticfan2","off"},{"followPrinterFan2","off"}}) == 200);
        assertPWM(180,50,50);
    });
    check("invalid or conflicting follow settings do not change saved configuration", [] {
        assert(options({{"fansSynchronized","off"},{"followPrinterFan","on"}}) == 200);
        const auto disk=savedConfig();
        for (const auto &params : std::vector<std::map<std::string,String>>{
            {{"staticfan","on"},{"followPrinterFan","on"}}, {{"staticfan2","on"},{"followPrinterFan2","on"}},
            {{"printerFanSource","unknown"}}, {{"printerFanSource2",""}}, {{"followPrinterFan","invalid"}}}) {
            assert(options(params)==400); assert(savedConfig()==disk); assert(printerConfig.followPrinterFan);
        }
        failRename=true;
        assert(options({{"followPrinterFan","off"},{"followPrinterFan2","on"},{"printerFanSource2","heatbreak"}})==500);
        assert(printerConfig.followPrinterFan && printerConfig.printerFanSource2 == PrinterFanSource::Chamber);
        assert(savedConfig()==disk);
    });
    check("invalid saved sources and conflicting overrides normalize without erasing configuration", [] {
        JsonDocument json; deserializeJson(json,legacyConfig);
        json["followPrinterFan"]=true; json["printerFanSource"]="invalid";
        std::string payload; serializeJson(json,payload); putConfig(payload); reboot();
        assert(!printerConfig.followPrinterFan); assert(savedConfig()==payload); assertCredentials();
        json["printerFanSource"]="part"; json["staticFan"]=true; json["staticFanSpeed"]=27;
        payload.clear(); serializeJson(json,payload); putConfig(payload); reboot();
        assert(printerConfig.staticFan && !printerConfig.followPrinterFan); assertPWM(200,27,27);
        assert(savedConfig()==payload);
    });
    check("legacy configuration retains credentials and curve, defaults to synchronized", [] {
        putConfig(legacyConfig); reboot(); assertCredentials();
        assert(printerConfig.fansSynchronized);
        assert(!printerConfig.staticFan && printerConfig.staticFanSpeed == 0);
        assert(printerConfig.fanGraph.size() == 3 && printerConfig.fanGraph2.empty());
        assert(savedConfig() == legacyConfig);
        assertPWM(100, 40, 40);
        handleGetOptions(); auto json = response();
        assert(json["fansSynchronized"] == true && json["fan2Configured"] == false);
        assert(json["code"] == "*****234");
        handleGetFanConfig(); json = response();
        assert(json["points"][1]["speed"] == 80 && json["points2"][1]["speed"] == 80);
        assert(printerConfig.fanGraph2.empty());
    });
    check("first independent save copies the current Fan 1 curve and static settings", [] {
        putConfig(legacyConfig); reboot();
        assert(curve(newCurve) == 200);
        assert(options({{"fansSynchronized","on"},{"staticfan","on"},{"staticfanspeed","37"}}) == 200);
        reboot(); assert(printerConfig.fanGraph2.empty());
        assert(options({{"fansSynchronized","off"},{"staticfan","on"},{"staticfanspeed","37"}}) == 200);
        assert(printerConfig.fanGraph == printerConfig.fanGraph2);
        assert(printerConfig.staticFan2 && printerConfig.staticFanSpeed2 == 37);
        reboot(); assertCredentials();
        assert(!printerConfig.fansSynchronized && printerConfig.staticFan2);
        assertPWM(100, 37, 37);
    });
    check("separate curves, static overrides, and sensor output drive separate GPIOs", [] {
        assert(options({{"fansSynchronized","off"}}) == 200);
        assert(curve(newCurve, "1") == 200 && curve(secondCurve, "2") == 200);
        assertPWM(100, 30, 40);
        assertPWM(-20, 10, 30);
        assertPWM(500, 90, 70);
        assert(options({{"staticfan","on"},{"staticfanspeed","12"},{"staticfan2","off"}}) == 200);
        assertPWM(100, 12, 40);
        assert(options({{"staticfan","off"},{"staticfan2","on"},{"staticfanspeed2","83"}}) == 200);
        assertPWM(100, 30, 83);
        handleSensorData(); auto json = response();
        assert(json["speed"] == 30 && json["speed2"] == 83);
        reboot(); assertPWM(100, 30, 83);
    });
    check("sync cycles and synchronized edits preserve dormant Fan 2 settings", [] {
        putConfig(legacyConfig); reboot();
        assert(options({{"fansSynchronized","off"},{"staticfan2","on"},{"staticfanspeed2","61"}}) == 200);
        assert(curve(secondCurve,"2") == 200);
        const auto savedSecond = printerConfig.fanGraph2;
        assert(options({{"fansSynchronized","on"},{"staticfan","on"},{"staticfanspeed","42"},
                        {"staticfan2","off"},{"staticfanspeed2","0"}}) == 200);
        assert(curve(newCurve) == 200); reboot();
        assertPWM(100, 42, 42);
        assert(printerConfig.fanGraph2 == savedSecond && printerConfig.staticFanSpeed2 == 61);
        assert(options({{"fansSynchronized","off"},{"staticfan","off"}}) == 200);
        reboot(); assertCredentials(); assertPWM(100, 30, 61);
    });
    check("empty, duplicate, malformed, and out-of-range curves never replace valid curves", [] {
        assert(curve(newCurve) == 200);
        const auto previous = printerConfig.fanGraph;
        const auto disk = savedConfig();
        for (const char *bad : {"{", R"({"points":[]})", R"({"points":{}})",
             R"({"points":[{"temp":20,"speed":10},{"temp":20,"speed":40}]})",
             R"({"points":[{"temp":-1,"speed":10}]})", R"({"points":[{"temp":401,"speed":10}]})",
             R"({"points":[{"temp":20,"speed":101}]})", R"({"points":[{"temp":20,"speed":-1}]})",
             R"({"points":[{"temp":20,"speed":12.5}]})", R"({"points":[{"temp":"20","speed":10}]})",
             R"({"points":[{"temp":20}]})", R"({"points":[null]})"}) {
            assert(curve(bad,"1") == 400);
            assert(curve(bad,"2") == 400);
            assert(printerConfig.fanGraph == previous && printerConfig.fanGraph2.empty());
            assert(savedConfig() == disk);
        }
        assert(curve(newCurve,"3") == 400);
    });
    check("single-point curves stay constant and malformed runtime input is safe", [] {
        assert(curve(R"({"points":[{"temp":100,"speed":34}]})") == 200);
        assertPWM(0,34,34); assertPWM(200,34,34);
        assert(calculateFanSpeed({}, 50) == 0);
        assert(calculateFanSpeed(defaultFanCurve(), std::numeric_limits<float>::quiet_NaN()) == 0);
        assert(calculateFanSpeed({{0, 120}},100) == 100);
    });
    check("invalid static values and modes do not change config", [] {
        putConfig(legacyConfig); reboot();
        for (const char *value : {"", "-1", "101", "NaN", "20.5", "999999999999"}) {
            assert(options({{"staticfanspeed",value}}) == 400);
            assert(options({{"fansSynchronized","off"},{"staticfanspeed2",value}}) == 400);
            assert(printerConfig.fansSynchronized && printerConfig.fanGraph2.empty());
        }
        assert(options({{"fansSynchronized","invalid"}}) == 400);
        assert(options({{"staticfan","invalid"}}) == 400);
        assert(options({{"code","TOO-LONG-CODE"}}) == 400);
        assertCredentials(); assert(savedConfig() == legacyConfig);
    });
    for (int failure = 0; failure < 3; ++failure) {
        check("failed settings and curve writes retain active and saved config", [failure] {
            putConfig(legacyConfig); reboot();
            const auto before = printerConfig.fanGraph;
            if (failure == 0) failOpen = true;
            if (failure == 1) writeBudget = 10;
            if (failure == 2) failRename = true;
            assert(options({{"fansSynchronized","off"},{"code","NEW12345"}}) == 500);
            assert(curve(secondCurve,"2") == 500);
            assert(printerConfig.fansSynchronized && printerConfig.fanGraph2.empty());
            assert(printerConfig.fanGraph == before);
            assertCredentials(); assert(savedConfig() == legacyConfig);
            assert(!LittleFS.exists("/blledconfig.json.tmp"));
        });
    }
    check("invalid stored curves fall back without erasing config or credentials", [] {
        JsonDocument json; deserializeJson(json,legacyConfig);
        json["fanPoints"].to<JsonArray>();
        json["fanPoints2"].to<JsonArray>();
        json["fansSynchronized"] = false;
        std::string invalid; serializeJson(json,invalid); putConfig(invalid); reboot();
        assertCredentials(); assert(!printerConfig.fansSynchronized);
        assert(printerConfig.fanGraph == defaultFanCurve() && printerConfig.fanGraph2 == defaultFanCurve());
        assert(savedConfig() == invalid);
    });
    check("missing or unreadable config is not overwritten", [] {
        loadFileSystem(); assert(!LittleFS.exists(configPath));
        putConfig("{"); reboot(); assert(savedConfig() == "{");
        putConfig(legacyConfig); shortRead=true; reboot();
        assert(savedConfig() == legacyConfig && printerConfig.fansSynchronized);
    });
    check("independent config missing Fan 2 data falls back to Fan 1", [] {
        JsonDocument json; deserializeJson(json,legacyConfig); json["fansSynchronized"]=false;
        json["staticFan"]=true; json["staticFanSpeed"]=19;
        std::string incomplete; serializeJson(json,incomplete); putConfig(incomplete); reboot();
        assert(printerConfig.fanGraph == printerConfig.fanGraph2);
        assertPWM(100,19,19);
    });
    check("legacy forms retain sync mode and masked credentials", [] {
        putConfig(legacyConfig); reboot();
        assert(options({{"code","*****234"},{"serial","***********345"},{"staticfan","on"},{"staticfanspeed","25"}}) == 200);
        assertCredentials(); reboot(); assertCredentials(); assertPWM(100,25,25);
        assert(options({}) == 204);
    });
}
