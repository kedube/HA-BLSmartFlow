int wifiSetup(std::map<std::string,String> params) {
    return request(handleSubmitWifiSetup, std::move(params), true);
}

void assertWifi(const char *ssid, const char *password) {
    assert(std::string(globalVariables.SSID) == ssid && std::string(globalVariables.APPW) == password);
}

const std::string printerAnnouncement =
    "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1990\r\nServer: UPnP/1.0\r\nLocation: 192.0.2.50\r\n"
    "NT: urn:bambulab-com:device:3dprinter:1\r\nUSN: 01P00A123456789\r\nCache-Control: max-age=1800\r\n"
    "DevModel.bambu.com: C12\r\nDevName.bambu.com: Garage P1S\r\nDevConnect.bambu.com: lan\r\n\r\n";

void wifiSetupTests() {
    check("without Wi-Fi, the setup network opens and every address leads to the setup page", [] {
        startSetupAccessPoint();
        assert(setupApActive && WiFi.wifiMode == WIFI_AP_STA && WiFi.apName == "BLSmartFlow-0E0F");
        assert(WiFi.apAddress.toString() == "192.168.4.1" && !WiFi.autoReconnect);
        assert(setupDns.started && setupDns.domain == "*" && setupDns.address.toString() == "192.168.4.1");
        // Settings, and the addresses phones load to detect a sign-in page.
        for (auto handler : {handleSetup, handleNotFound}) {
            assert(request(handler) == 302 && webServer.responseHeaders["Location"] == "http://192.168.4.1/wifi");
        }
        assert(request(handleWifiPage) == 200);
        handleGetWifiSetup(); assert(response()["accessPoint"] == true && response()["ssid"] == "");
        setupApActive = false;
        assert(request(handleNotFound) == 404 && request(handleSetup) == 200);
    });
    check("an unconfigured controller waits on the setup network without trying to join one", [] {
        startSetupAccessPoint();
        for (int i = 0; i < 5; ++i) {
            mockMillis += 60000;
            assert(!setupAccessPointLoop());
        }
        assert(WiFi.joins == 0 && setupDns.requests == 5);
    });
    check("the saved network is retried every minute while no phone is connected, and joining it restarts", [] {
        putConfig(legacyConfig); reboot();
        WiFi.connectionStatus = 0;
        mockMillis = 1000; startSetupAccessPoint();
        mockMillis += 59999; setupAccessPointLoop(); assert(WiFi.joins == 0);
        mockMillis += 1; setupAccessPointLoop();
        assert(WiFi.joins == 1 && WiFi.joinedSsid == "test-network" && WiFi.joinedPassword == "test-password");
        // An attempt stops after 15 seconds, so the radio returns to the setup network's channel.
        const int disconnects = WiFi.disconnects;
        mockMillis += 14999; setupAccessPointLoop(); assert(WiFi.disconnects == disconnects);
        mockMillis += 1; setupAccessPointLoop(); assert(WiFi.disconnects == disconnects + 1);
        mockMillis += 45000; setupAccessPointLoop(); assert(WiFi.joins == 2);
        // A phone joining stops an attempt at once and holds off the next, however long it stays.
        WiFi.stations = 1;
        setupAccessPointLoop(); assert(WiFi.disconnects == disconnects + 2);
        mockMillis += 600000; setupAccessPointLoop(); assert(WiFi.joins == 2);
        WiFi.stations = 0;
        setupAccessPointLoop(); assert(WiFi.joins == 3);
        WiFi.connectionStatus = WL_CONNECTED;
        assert(setupAccessPointLoop());
    });
    check("Wi-Fi setup shows the saved network and printer but never the passwords", [] {
        putConfig(legacyConfig); reboot();
        assert(request(handleGetWifiSetup) == 200);
        auto json = response();
        assert(json["ssid"] == "test-network" && json["passwordSet"] == true && json["ip"] == "192.0.2.83");
        assert(json["code"] == "*****234" && json["id"] == "************345" && json["controllerName"] == "");
        assert(json["accessPoint"] == false && json["address"] == "http://blflow.local/");
        assert(webServer.lastBody.find("test-password") == std::string::npos);
        assert(webServer.lastBody.find("TEST1234") == std::string::npos);
    });
    check("saving Wi-Fi setup stores the network, controller name and printer, then restarts to join them", [] {
        putConfig(legacyConfig); reboot();
        strcpy(printerConfig.BSSID, "02:00:00:00:00:01");
        assert(wifiSetup({{"ssid","workshop"},{"password","workshop-pass"},{"controllerName","  Garage P1S  "},
                          {"ip","192.0.2.99"},{"serial","01p00a123456789"},{"code","12345678"}}) == 200);
        assert(ESP.restarts == 1 && webServer.responseHeaders["Connection"] == "close");
        reboot();
        assertWifi("workshop", "workshop-pass");
        assert(std::string(printerConfig.controllerName) == "Garage P1S" && controllerName() == "Garage P1S");
        handleGetWifiSetup(); assert(response()["address"] == "http://garage-p1s.local/");
        assert(std::string(printerConfig.printerIP) == "192.0.2.99" && std::string(printerConfig.accessCode) == "12345678");
        assert(std::string(printerConfig.serialNumber) == "01P00A123456789");
        assert(!printerConfig.BSSID[0]); // The access point found for the old network
        // The masked values the page shows keep the saved ones, as does a blank password for the same network.
        assert(wifiSetup({{"ssid","workshop"},{"password",""},{"ip","192.0.2.99"},
                          {"serial","************789"},{"code","*****678"}}) == 200);
        reboot();
        assertWifi("workshop", "workshop-pass");
        assert(std::string(printerConfig.serialNumber) == "01P00A123456789" && std::string(printerConfig.accessCode) == "12345678");
        // An open network, which the page found in its scan, has no password. Clearing the name
        // brings back the defaults.
        assert(wifiSetup({{"ssid","Guest"},{"password",""},{"open","on"},{"controllerName",""}}) == 200);
        reboot();
        assertWifi("Guest", "");
        assert(std::string(printerConfig.printerIP) == "192.0.2.99" && ESP.restarts == 3);
        assert(controllerName() == "BL Smart Flow+" && controllerHostname() == "blflow");
    });
    check("invalid Wi-Fi setup is refused without saving or restarting", [] {
        putConfig(legacyConfig); reboot();
        const auto disk = savedConfig();
        for (const auto &params : std::vector<std::map<std::string,String>>{
            {{"ssid",""},{"password","new-password"}}, {{"ssid",std::string(33,'s')},{"password","new-password"}},
            {{"ssid","workshop"}}, {{"ssid","workshop"},{"password",""}}, {{"ssid","workshop"},{"password","short"}},
            {{"ssid","workshop"},{"password",std::string(64,'p')}}, {{"ssid","test-network"},{"ip","192.0.2.100.200.300"}},
            {{"ssid","test-network"},{"code","TOO-LONG-CODE"}}, {{"ssid","test-network"},{"serial",std::string(16,'S')}},
            {{"ssid","test-network"},{"controllerName",std::string(33,'n')}}}) {
            assert(wifiSetup(params) == 400);
            assert(savedConfig() == disk && ESP.restarts == 0);
            assertWifi("test-network", "test-password");
        }
        failRename = true;
        assert(wifiSetup({{"ssid","workshop"},{"password","workshop-pass"}}) == 500);
        assert(savedConfig() == disk && ESP.restarts == 0);
        assertWifi("test-network", "test-password");
    });
    check("the setup page and its requests need the login, and saving must come from the page", [] {
        putConfig(legacyConfig); reboot(); setLogin();
        const auto disk = savedConfig();
        setupApActive = true; // The login still applies on the setup network.
        for (auto handler : {handleWifiPage, handleGetWifiSetup, handleWifiScan, handlePrinters}) {
            assert(request(handler, {{"start","1"}}) == 401);
        }
        assert(wifiSetup({{"ssid","attacker"},{"password","attacker-pass"}}) == 401);
        webServer.credentials = {"admin","correct horse"};
        assert(request(handleSubmitWifiSetup, {{"ssid","attacker"},{"password","attacker-pass"}}) == 403);
        assert(savedConfig() == disk && ESP.restarts == 0 && WiFi.scansStarted == 0 && !discoveryListening);
    });
    check("network scans run in the background and list each network once, strongest first", [] {
        // Results left by the scan at startup are out of date.
        WiFi.networks = {{"stale", -40, WIFI_AUTH_WPA2_PSK}};
        WiFi.scanState = 1;
        assert(request(handleWifiScan, {{"start","1"}}) == 202);
        assert(WiFi.scansDeleted == 1 && WiFi.scansStarted == 1);
        assert(request(handleWifiScan) == 202);
        WiFi.networks = {{"home", -70, WIFI_AUTH_WPA2_PSK}, {"", -40, WIFI_AUTH_WPA2_PSK}, {"Guest", -55, WIFI_AUTH_OPEN},
                         {"home", -50, WIFI_AUTH_WPA2_PSK}, {"upstairs", -80, WIFI_AUTH_WPA_PSK}};
        WiFi.scanState = 5;
        assert(request(handleWifiScan) == 200);
        const JsonDocument scan = response();
        auto networks = scan["networks"];
        assert(networks.size() == 3);
        assert(networks[0]["ssid"] == "home" && networks[0]["rssi"] == -50 && networks[0]["secure"] == true);
        assert(networks[1]["ssid"] == "Guest" && networks[1]["secure"] == false);
        assert(networks[2]["ssid"] == "upstairs" && networks[2]["rssi"] == -80);
        assert(WiFi.scansDeleted == 2);
        assert(request(handleWifiScan) == 503); // Nothing scanning: the page starts again
        WiFi.startResult = WIFI_SCAN_FAILED;
        assert(request(handleWifiScan, {{"start","1"}}) == 503);
    });
    check("printer discovery lists Bambu Lab printers that announce themselves, and nothing else", [] {
        assert(request(handlePrinters) == 200);
        auto json = response();
        assert(json["listening"] == true && json["printers"].size() == 0);
        assert(discoveryUdp.listening && discoveryUdp.port == 2021 && discoveryUdp.group.toString() == "239.255.255.250");
        assert(discoveryUdp.sent.size() == 1 && discoveryUdp.sent[0].rfind("M-SEARCH * HTTP/1.1\r\n", 0) == 0);
        assert(discoveryUdp.sent[0].find("ST: urn:bambulab-com:device:3dprinter:1\r\n") != std::string::npos);
        discoveryUdp.incoming = {
            {printerAnnouncement, IPAddress(192, 0, 2, 50)},
            {discoveryUdp.sent[0], IPAddress(192, 0, 2, 10)}, // The controller's own search, looped back
            {"NOTIFY * HTTP/1.1\r\nNT: urn:schemas-upnp-org:device:DimmableLight:1\r\nUSN: uuid:light\r\n\r\n", IPAddress(192, 0, 2, 60)},
            {"HTTP/1.1 200 OK\r\nST: urn:bambulab-com:device:3dprinter:1\r\nusn: 03W00X987654321\r\n"
             "devname.bambu.com:   Office X1C  \r\n\r\n", IPAddress(192, 0, 2, 51)},
            {"NOTIFY * HTTP/1.1\r\nNT: urn:bambulab-com:device:3dprinter:1\r\nUSN: not a serial\r\n\r\n", IPAddress(192, 0, 2, 52)},
        };
        webserverloop(); webserverloop();
        assert(discoveryUdp.incoming.empty());
        assert(request(handlePrinters) == 200);
        json = response();
        JsonArrayConst printers = json["printers"];
        assert(printers.size() == 2);
        assert(printers[0]["ip"] == "192.0.2.50" && printers[0]["serial"] == "01P00A123456789");
        assert(printers[0]["name"] == "Garage P1S" && printers[0]["model"] == "C12");
        assert(printers[1]["ip"] == "192.0.2.51" && printers[1]["serial"] == "03W00X987654321");
        assert(printers[1]["name"] == "Office X1C" && printers[1]["model"] == "");
        // A printer announcing from a new address replaces its entry.
        discoveryUdp.incoming = {{printerAnnouncement, IPAddress(192, 0, 2, 77)}};
        webserverloop();
        assert(request(handlePrinters) == 200);
        json = response();
        assert(json["printers"].size() == 2 && json["printers"][0]["ip"] == "192.0.2.77");
        // Searches go out at most every 10 seconds.
        assert(discoveryUdp.sent.size() == 1);
        mockMillis += 10000; request(handlePrinters);
        assert(discoveryUdp.sent.size() == 2);
        // Listening stops two minutes after the page last asked, keeping what it found.
        mockMillis += 119999; webserverloop(); assert(discoveryUdp.listening);
        mockMillis += 1; webserverloop(); assert(!discoveryUdp.listening && !discoveryListening);
        assert(request(handlePrinters) == 200 && discoveryUdp.listening && response()["printers"].size() == 2);
        // The list is bounded.
        for (int i = 0; i < 12; ++i) {
            discoveryUdp.incoming.push_back({"NOTIFY * HTTP/1.1\r\nNT: urn:bambulab-com:device:3dprinter:1\r\nUSN: SERIAL" +
                                             std::to_string(i) + "\r\n\r\n", IPAddress(192, 0, 2, 100 + i)});
        }
        for (int i = 0; i < 4; ++i) webserverloop();
        assert(discoveredPrinterCount == maxDiscoveredPrinters);
    });
}
