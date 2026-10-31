#include <Arduino.h>
#include "./blflow/web-server.h"
#include "./blflow/mqttmanager.h"
#include "./blflow/filesystem.h"
#include "./blflow/types.h"
#include "./blflow/fans.h"
#include "./blflow/serialmanager.h"
#include "./blflow/wifi-manager.h"
#include "./blflow/ssdp.h"
#include "./blflow/indicator.h"
#include "./blflow/home-assistant.h"
#include "./blflow/log-serial.h"
#include "./blflow/setup-access-point.h"

int wifi_reconnect_count = 0;

void setup(){
    Serial.begin(115200);
    delay(100);
    LogSerial.println(F("Initializing"));
    LogSerial.println(ESP.getFreeHeap());
    LogSerial.println("");
    LogSerial.print(F("** Using firmware version: "));
    LogSerial.print(globalVariables.FWVersion);
    LogSerial.println(F(" **"));
    LogSerial.println("");
    
    fansetup();
    indicatorsetup();
    delay(1000);
    setupFileSystem();
    loadFileSystem();
    LogSerial.println(F(""));
    delay(500);

    setupSerial();

    // An open network has no password, so only the network name is required.
    if (!wifiConfigured()) {
        printerVariables.errorcode = "no config";
        LogSerial.println(F("No Wi-Fi network is set up yet"));
        startSetupAccessPoint();
        setupWebserver();
        return;
    }
    else if (printerVariables.errorcode == "no config"){
        //if this routine set the error code, clear it
        printerVariables.errorcode = "";
    }
    
   
    scanNetwork(); //Sets the MAC address for following connection attempt
    if(!connectToWifi()){
        printerVariables.errorcode = "no wifi";
        startSetupAccessPoint();
        setupWebserver();
        return;
    }
    else if (printerVariables.errorcode == "no wifi"){
        //if this routine set the error code, clear it
        printerVariables.errorcode = "";
    }
    
    setupWebserver();
    delay(500);
    if (printerConfig.ssdpEnabled) start_ssdp();
    setupMqtt();
    setupHomeAssistant();

    LogSerial.println();
    LogSerial.print(F("** BLLED Controller started "));
    LogSerial.print(F("using firmware version: "));
    LogSerial.print(globalVariables.FWVersion);
    LogSerial.println(F(" **"));
    LogSerial.println();
    globalVariables.started = true;
}

void loop(){
    fanloop(); //Run fanloop at the start of the loop so its always updating before everything else.
    indicatorloop();
    serialLoop();

    if (setupApActive) {
        webserverloop();
        if (setupAccessPointLoop()) {
            LogSerial.println(F("[Setup] Joined the saved Wi-Fi network; restarting to start normally"));
            delay(500);
            ESP.restart();
        }
    }
    else if (globalVariables.started){
        mqttloop();
        webserverloop();
        homeAssistantLoop();

        if (WiFi.status() != WL_CONNECTED){
            LogSerial.print(F("Wifi connection dropped.  "));
            LogSerial.print(F("Wifi Status: ")); 
            LogSerial.println(wl_status_to_string(WiFi.status()));
            LogSerial.println(F("Attempting to reconnect to WiFi..."));
            wifi_reconnect_count += 1;
            if(wifi_reconnect_count <= 2){
                WiFi.disconnect();
                delay(100);
                WiFi.reconnect();
            } else {
                //Not connecting after 10 simple disconnect / reconnects
                //Do something more drastic in case needing to switch to new AP
                scanNetwork();
                connectToWifi();
                wifi_reconnect_count = 0;
            }
        }
    }
    if(printerConfig.rescanWiFiNetwork)
    {
        LogSerial.println(F("Web submitted refresh of Wifi Scan (assigning Strongest AP)"));
        scanNetwork(); //Sets the MAC address for following connection attempt
        printerConfig.rescanWiFiNetwork = false;
    }
}