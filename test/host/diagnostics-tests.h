// Debug & Diagnostics settings, the Runtime Status panel, the Controller Log, and the Device Tools endpoints.

// Sends a settings file through the upload callback, then the request handler, as the server does.
int restore(const std::string &contents, bool ownPage = true, bool aborted = false) {
    webServer.headers.clear();
    if (ownPage) webServer.headers[ownPageHeader] = "1";
    webServer.responses.clear();
    std::vector<uint8_t> bytes(contents.begin(), contents.end());
    HTTPUpload &file = webServer.upload();
    file = HTTPUpload{UPLOAD_FILE_START, "blsmartflow-settings.json", bytes.data(), 0, 0};
    handleConfigRestoreUpload();
    file.status = UPLOAD_FILE_WRITE;
    for (size_t offset = 0; offset < bytes.size(); offset += 1000) {
        file.buf = bytes.data() + offset;
        file.currentSize = std::min<size_t>(1000, bytes.size() - offset);
        handleConfigRestoreUpload();
    }
    file.status = aborted ? UPLOAD_FILE_ABORTED : UPLOAD_FILE_END;
    file.totalSize = bytes.size();
    handleConfigRestoreUpload();
    handleConfigRestore();
    assert(webServer.responses.size() == 1);
    return webServer.responses[0];
}

// Feeds a printer report through the MQTT callback, as PubSubClient does.
void deliverPrinterReport(const std::string &json) {
    for (char c : json) stream.write(static_cast<uint8_t>(c));
    mqttCallback(const_cast<char *>("device/TESTSERIAL12345/report"), nullptr, json.size());
}

void diagnosticsTests() {
    check("Debug & Diagnostics settings are validated, saved, and applied only when they change", [] {
        putConfig(legacyConfig); reboot();
        assert(printerConfig.wifiTxPower == 78 && printerConfig.wifiSleep);
        assert(printerConfig.printerConnection && printerConfig.ssdpEnabled);
        handleGetOptions(); auto json = response();
        assert(json["wifiTxPower"] == 78 && json["wifiSleep"] == true);
        assert(json["printerConnection"] == true && json["ssdpEnabled"] == true);
        const auto disk = savedConfig();
        for (const auto &params : std::vector<std::map<std::string,String>>{
            {{"wifiTxPower","50"}}, {{"wifiTxPower","-4"}}, {{"wifiTxPower","19.5"}}, {{"wifiTxPower",""}},
            {{"wifiSleep","maybe"}}, {{"printerConnection","1"}}, {{"ssdpEnabled","yes"}}}) {
            assert(options(params) == 400);
            assert(savedConfig() == disk && WiFi.radioChanges == 0);
        }
        assert(options({{"wifiTxPower","34"},{"wifiSleep","off"},{"printerConnection","on"},{"ssdpEnabled","off"}}) == 200);
        assert(WiFi.txPower == 34 && !WiFi.sleep && SSDP.ends == 1 && SSDP.begins == 0);
        const int radioChanges = WiFi.radioChanges;
        assert(options({{"wifiTxPower","34"},{"wifiSleep","off"},{"ssdpEnabled","off"}}) == 200);
        assert(WiFi.radioChanges == radioChanges && SSDP.ends == 1);
        assert(options({{"ssdpEnabled","on"}}) == 200 && SSDP.begins == 1);
        reboot();
        assert(printerConfig.wifiTxPower == 34 && !printerConfig.wifiSleep && printerConfig.ssdpEnabled);
        assertCredentials();
        putConfig(R"({"ssid":"test-network","appw":"test-password","printerIp":"192.0.2.83","wifiTxPower":51})");
        reboot(); assert(printerConfig.wifiTxPower == 78);
    });
    check("turning the printer connection off disconnects it until it is turned back on", [] {
        putConfig(legacyConfig); reboot();
        mqttClient.isConnected = true;
        assert(options({{"printerConnection","off"}}) == 200);
        const int attempts = mqttClient.connects;
        for (int i = 0; i < 3; ++i) {
            mockMillis += 30000; mqttloop();
            assert(!mqttClient.connected() && !printerVariables.online && mqttClient.connects == attempts);
        }
        handleSensorData(); auto json = response();
        assert(json["printerConnection"] == false && json["printerConnected"] == false && json["rssi"] == -61);
        assert(options({{"printerConnection","on"}}) == 200);
        mqttClient.connectResult = true;
        mockMillis += 30000; mqttloop();
        assert(mqttClient.connects == attempts + 1 && mqttClient.connected() && printerVariables.online);
    });
    check("the runtime status reports connectivity, traffic, memory, and the reset reason without secrets", [] {
        putConfig(legacyConfig); reboot();
        assert(options({{"haEnabled","on"},{"haHost","broker.local"},{"haUser","fans"},{"haPassword","broker-secret"}}) == 200);
        mockUptimeMicros = (2 * 86400 + 5) * 1000000LL;
        mockResetReason = ESP_RST_BROWNOUT;
        const std::string report = R"({"print":{"nozzle_temper":210}})";
        mockMillis = 1000;
        for (int i = 0; i < 3; ++i) deliverPrinterReport(report);
        assert(printerVariables.nozzletemp == 210);
        mockMillis = 61000;
        assert(request(handleDiag) == 200);
        auto json = response();
        assert(json["printerReportsPerMinute"] == 3 && json["printerBytesPerMinute"] == 3 * report.size());
        assert(json["lastPrinterReport"] == 60 && json["printerConnection"] == true);
        assert(json["uptime"] == 2 * 86400 + 5 && json["resetReason"] == "brownout");
        assert(json["ssid"] == "test-network" && json["rssi"] == -61 && json["channel"] == 6);
        assert(json["wifiTxPower"] == 78 && json["wifiSleep"] == true && json["ssdpEnabled"] == true);
        assert(json["freeHeap"] == 150000 && json["minFreeHeap"] == 90000 && json["maxAllocHeap"] == 65524);
        assert(json["homeAssistant"] == "connecting" && json["firmware"].as<std::string>().find("host-test") != std::string::npos);
        for (const char *secret : {"broker-secret", "TEST1234", "test-password"}) {
            assert(webServer.lastBody.find(secret) == std::string::npos);
        }
        mockMillis = 200000; // A minute without reports
        assert(request(handleDiag) == 200 && response()["printerReportsPerMinute"] == 0);
        mockResetReason = ESP_RST_PANIC;
        assert(request(handleDiag) == 200 && response()["resetReason"] == "crash");
    });
    check("the controller log returns text after a position, marks overwritten text, and logs nothing itself", [] {
        LogSerial.print("first line\n");
        const uint32_t mark = LogSerial.total();
        LogSerial.println("second");
        LogSerial.printf("value %d\n", 42);
        assert(request(handleLog, {{"from", String(mark)}}) == 200);
        assert(webServer.lastBody == "second\r\nvalue 42\n");
        assert(webServer.responseHeaders["X-Log-Start"] == String(mark));
        assert(webServer.responseHeaders["X-Log-Next"] == String(LogSerial.total()));
        const uint32_t total = LogSerial.total();
        assert(request(handleLog) == 200 && webServer.lastBody == "first line\nsecond\r\nvalue 42\n");
        assert(LogSerial.total() == total);
        // Text the page has not read yet is overwritten once the buffer fills.
        LogSerial.print(std::string(LogSerialClass::capacity + 100, 'x'));
        assert(request(handleLog, {{"from", String(mark)}}) == 200);
        assert(webServer.lastBody == std::string(LogSerialClass::capacity, 'x'));
        assert(webServer.responseHeaders["X-Log-Start"] == String(LogSerial.total() - LogSerialClass::capacity));
        // A page still holding a position from before a restart gets everything kept.
        assert(request(handleLog, {{"from", String(LogSerial.total() + 500)}}) == 200);
        assert(webServer.lastBody.size() == LogSerialClass::capacity);
    });
    check("backups need the login and return the saved file", [] {
        putConfig(legacyConfig); reboot();
        assert(request(handleDownloadConfig) == 403);
        assert(webServer.lastBody.find("TEST1234") == std::string::npos);
        setLogin();
        assert(request(handleDownloadConfig) == 401);
        webServer.credentials = {"admin","correct horse"};
        assert(request(handleDownloadConfig) == 200 && webServer.lastBody == savedConfig());
        assert(webServer.responseHeaders["Content-Disposition"].find("attachment") != std::string::npos);
        LittleFS.files.clear();
        assert(request(handleDownloadConfig) == 404);
    });
    check("a restore replaces the settings only with a complete settings file from the controller's own pages", [] {
        putConfig(legacyConfig); reboot();
        const auto disk = savedConfig();
        std::string restored = legacyConfig;
        restored.replace(restored.find("192.0.2.83"), 10, "192.0.2.99");
        for (const auto &[contents, ownPage, aborted, status] : std::vector<std::tuple<std::string,bool,bool,int>>{
            {restored, false, false, 403}, {restored, true, true, 400}, {"{", true, false, 400},
            {R"({"printerIp":"192.0.2.99"})", true, false, 400}, {"[]", true, false, 400}, {"", true, false, 400},
            {std::string(settingsFileMaxBytes + 1, ' '), true, false, 400}}) {
            assert(restore(contents, ownPage, aborted) == status);
            assert(savedConfig() == disk && ESP.restarts == 0 && !LittleFS.exists(restorePath));
        }
        assert(request(handleConfigRestore, {}, true) == 400); // No file
        assert(restore(restored) == 200 && ESP.restarts == 1 && savedConfig() == restored);
        reboot();
        assert(std::string(printerConfig.printerIP) == "192.0.2.99" && std::string(printerConfig.accessCode) == "TEST1234");
        setLogin();
        const auto protectedDisk = savedConfig();
        assert(restore(legacyConfig) == 401 && savedConfig() == protectedDisk && ESP.restarts == 1);
    });
    check("restarts must come from the controller's own pages", [] {
        assert(request(handleRestart) == 403 && ESP.restarts == 0);
        assert(request(handleRestart, {}, true) == 200 && ESP.restarts == 1);
        setLogin();
        assert(request(handleRestart, {}, true) == 401 && ESP.restarts == 1);
    });
}
