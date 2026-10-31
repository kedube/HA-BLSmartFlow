// Calls a handler as a request, from the controller's own pages or another site.
int request(void (*handler)(), std::map<std::string,String> params = {}, bool ownPage = false) {
    webServer.params = std::move(params);
    webServer.headers.clear();
    if (ownPage) webServer.headers[ownPageHeader] = "1";
    webServer.responses.clear();
    webServer.responseHeaders.clear();
    handler();
    assert(webServer.responses.size() == 1);
    return webServer.responses[0];
}

// Sends a firmware upload through the upload callback, then the request handler, as the server does.
int upload(bool ownPage, bool aborted = false) {
    webServer.headers.clear();
    if (ownPage) webServer.headers[ownPageHeader] = "1";
    webServer.responses.clear();
    static uint8_t chunk[16] = {};
    HTTPUpload &file = webServer.upload();
    file = HTTPUpload{UPLOAD_FILE_START, "firmware-1.4.13.bin", chunk, 0, 0};
    handleUpdateUpload();
    file.status = UPLOAD_FILE_WRITE;
    file.currentSize = sizeof(chunk);
    handleUpdateUpload();
    handleUpdateUpload();
    file.status = aborted ? UPLOAD_FILE_ABORTED : UPLOAD_FILE_END;
    file.totalSize = 2 * sizeof(chunk);
    handleUpdateUpload();
    handleUpdate();
    assert(webServer.responses.size() == 1);
    return webServer.responses[0];
}

void setLogin() {
    assert(options({{"webUser","admin"},{"webPassword","correct horse"}}) == 200);
    assert(loginRequired());
}

// Every page and read-only endpoint, which any client on the network may load unless a login is set.
const std::vector<void (*)()> pageHandlers = {handleSetup, handleGetOptions, handleGetFanConfig, handleSensorData,
    handleUpdatePage, handleBackupPage, handleLogPage, handleStyle, handleScript, handleFanIcon, handleDiag, handleLog,
    handleWifiPage, handleGetWifiSetup, handlePrinters};

void webSecurityTests() {
    check("without a login, pages are open but changes must come from the controller's own pages", [] {
        putConfig(legacyConfig); reboot();
        for (auto handler : pageHandlers) {
            assert(request(handler) == 200);
        }
        const auto disk = savedConfig();
        assert(request(submitOptions, {{"staticfan","on"},{"staticfanspeed","50"}}) == 403);
        assert(request(submitOptions) == 403);
        assert(request(handleUpdateFanConfig, {{"points", newCurve}}) == 403);
        assert(savedConfig() == disk && !printerConfig.staticFan && webServer.authenticationRequests == 0);
        assert(request(submitOptions, {{"staticfan","on"},{"staticfanspeed","50"}}, true) == 200);
        assert(request(handleUpdateFanConfig, {{"points", newCurve}}, true) == 200);
        assert(printerConfig.staticFan && printerConfig.fanGraph.front().second == 10);
    });
    check("a login protects every page and endpoint, and is never sent to the page", [] {
        putConfig(legacyConfig); reboot(); setLogin();
        const auto disk = savedConfig();
        for (const auto &login : std::vector<std::pair<std::string,std::string>>{
            {"",""}, {"admin","wrong"}, {"Admin","correct horse"}, {"admin",""}}) {
            webServer.credentials = login;
            for (auto handler : pageHandlers) {
                const int challenges = webServer.authenticationRequests;
                assert(request(handler) == 401 && webServer.authenticationRequests == challenges + 1);
            }
            assert(request(submitOptions, {{"staticfan","on"},{"staticfanspeed","50"}}, true) == 401);
            assert(request(handleUpdateFanConfig, {{"points", newCurve}}, true) == 401);
            assert(upload(true) == 401 && !Update.began && Update.written == 0 && ESP.restarts == 0);
            assert(savedConfig() == disk);
        }
        webServer.credentials = {"admin","correct horse"};
        for (auto handler : pageHandlers) {
            assert(request(handler) == 200);
        }
        assert(request(handleGetOptions) == 200);
        auto json = response();
        assert(json["webUser"] == "admin" && json["webPasswordSet"] == true && json["webPassword"].isNull());
        assert(webServer.lastBody.find("correct horse") == std::string::npos);
        // A browser that saved the login still sends it with another site's requests.
        assert(request(submitOptions, {{"staticfan","on"},{"staticfanspeed","50"}}) == 403);
        assert(upload(false) == 403 && !Update.began);
        assert(savedConfig() == disk);
        assert(request(submitOptions, {{"staticfan","on"},{"staticfanspeed","50"}}, true) == 200);
        reboot(); assert(loginRequired() && printerConfig.staticFan); assertCredentials();
    });
    check("login settings are validated; a blank password keeps it and clearing the username removes it", [] {
        putConfig(legacyConfig); reboot();
        const auto disk = savedConfig();
        for (const auto &params : std::vector<std::map<std::string,String>>{
            {{"webPassword","secret"}}, {{"webUser","admin"}}, {{"webUser","ad:min"},{"webPassword","secret"}},
            {{"webUser",std::string(33,'u')},{"webPassword","secret"}},
            {{"webUser","admin"},{"webPassword",std::string(65,'p')}}}) {
            assert(options(params) == 400);
            assert(savedConfig() == disk && !loginRequired());
        }
        assert(options({{"webUser"," admin "},{"webPassword","pass:word"}}) == 200);
        assert(std::string(printerConfig.webUser) == "admin" && std::string(printerConfig.webPassword) == "pass:word");
        webServer.credentials = {"admin","pass:word"};
        assert(options({{"webUser","admin"},{"webPassword",""}}) == 200);
        assert(std::string(printerConfig.webPassword) == "pass:word");
        reboot(); assert(loginRequired()); assertCredentials();
        assert(options({{"webUser",""},{"webPassword",""}}) == 200);
        assert(!loginRequired() && !printerConfig.webPassword[0]);
        webServer.credentials = {};
        assert(request(handleGetOptions) == 200 && response()["webPasswordSet"] == false);
    });
    check("firmware uploads are checked before anything is written, and only a complete update restarts", [] {
        assert(upload(false) == 403);
        assert(!Update.began && Update.written == 0 && ESP.restarts == 0);
        assert(upload(true) == 200);
        assert(Update.began && Update.written == 32 && Update.ended && ESP.restarts == 1);
        Update = UpdateMock{};
        Update.fail = true;
        assert(upload(true) == 500 && webServer.lastBody == "FAIL" && ESP.restarts == 1);
        Update = UpdateMock{};
        assert(upload(true, true) == 400 && Update.aborted && ESP.restarts == 1);
        assert(request(handleUpdate, {}, true) == 400 && ESP.restarts == 1); // No file
    });
    check("setup over USB clears a forgotten login", [] {
        putConfig(legacyConfig); reboot(); setLogin();
        Serial.input = R"({"ssid":"test-network","pass":"test-password"})" "\n";
        serialLoop(); reboot();
        assert(!loginRequired() && !printerConfig.webUser[0]);
        assertCredentials();
    });
}
