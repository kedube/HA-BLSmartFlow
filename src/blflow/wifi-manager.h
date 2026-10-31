#ifndef _BLLEDWIFI_MANAGER
#define _BLLEDWIFI_MANAGER

#include <Arduino.h>
#include <ArduinoJson.h> 
#include <WiFi.h>

#include "diagnostics.h"
#include "filesystem.h"
#include "log-serial.h"
#include "types.h"

bool shouldSaveConfig = true;
int connectionAttempts = 1;
int wifimode = 0;
uint8_t bssid[6] = {0};

void configModeCallback() {
  LogSerial.println(F("Entered config mode"));
  LogSerial.print(F("AP IP address: "));
  LogSerial.println(WiFi.softAPIP());
}
const char* wl_status_to_string(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD: return "WL_NO_SHIELD";
    case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
    case WL_CONNECTED: return "WL_CONNECTED";
    case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED: return "WL_DISCONNECTED";
  }
  return "UNKNOWN";
}
int str2mac(const char* mac, uint8_t* values){
    if( 6 == sscanf( mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",&values[0], &values[1], &values[2],&values[3], &values[4], &values[5] ) ){
        return 1;
    }else{
        return 0;
    }
}
bool connectToWifi(){
    LogSerial.println(F("-------------------------------------"));
    WiFi.mode(WIFI_STA);   //ESP32 connects to an access point
    delay(10);
    WiFi.disconnect();
    if(strlen(printerConfig.BSSID) == 0){
        WiFi.begin(globalVariables.SSID, globalVariables.APPW);
        wifimode = 1;
    }
    else{
        // Reference https://stackoverflow.com/questions/70415075/esp32-select-one-from-multiple-wifi-ap-with-same-name-ssid

        //Function to convert String MAC address to 6 byte array
        if(str2mac(printerConfig.BSSID,bssid)){
            WiFi.begin(globalVariables.SSID, globalVariables.APPW, 0, bssid);
            wifimode = 0;
        }
        else{
            if (printerConfig.debuging || printerConfig.debugingchange){
                LogSerial.print(F("Parsing MAC Address Failed, reverting to "));
                LogSerial.println(globalVariables.SSID);
            } 
            WiFi.begin(globalVariables.SSID, globalVariables.APPW);
            wifimode = 1;
        }
    }
    
    wl_status_t status = WiFi.status();
    while (status != WL_CONNECTED) {
        if(connectionAttempts > 10 && wifimode == 0){
            WiFi.disconnect();
            WiFi.begin(globalVariables.SSID, globalVariables.APPW);
            connectionAttempts = 1;
            wifimode = 1;
            LogSerial.println(F("Attempting to connect without specific BSSI"));
        }
        if(connectionAttempts > 10 && wifimode == 1){
            WiFi.disconnect();
            WiFi.begin(globalVariables.SSID);
            connectionAttempts = 1;
            wifimode = 2;
            if(strlen(printerConfig.BSSID) == 0) wifimode = 0;
            
            LogSerial.println(F("Attempting to connect to open network (no password)"));
        }
        if(connectionAttempts > 10 && wifimode == 2){
            WiFi.disconnect();
            WiFi.begin(globalVariables.SSID, globalVariables.APPW, 0, bssid);
            connectionAttempts = 1;
            wifimode = 0;
            LogSerial.println(F("Attempting to connect to wifi using BSSI"));
        }

        LogSerial.print(F("Connecting to WIFI.. Status check #"));
        LogSerial.print(connectionAttempts);
        LogSerial.print(F(" / 10      SSID: "));
        LogSerial.print(globalVariables.SSID);
        if(strlen(printerConfig.BSSID) > 0){
            LogSerial.print(F("   BSSID: "));
            LogSerial.print(printerConfig.BSSID);
        }
        LogSerial.println();

        if(status != WiFi.status()){
            status = WiFi.status();
            switch (status)
            {
                case WL_CONNECTED:
                case WL_IDLE_STATUS:
                case WL_CONNECT_FAILED:
                    LogSerial.print(F("Wifi Status: "));
                    LogSerial.println(wl_status_to_string(status));
                    break;
                case WL_NO_SSID_AVAIL:
                    LogSerial.print(F("Wifi Status: "));
                    LogSerial.println(wl_status_to_string(status));
                    LogSerial.println(F("Bad WiFi credentials"));
                    return false;
                case WL_DISCONNECTED:
                    LogSerial.print(F("Wifi Status: "));
                    LogSerial.println(wl_status_to_string(status));
                    LogSerial.println(F("Disconnected. (Check low RSSI)"));
                    return false;
                default:
                    LogSerial.print(F("Uncaught Status - Wifi Status: "));
                    LogSerial.println(wl_status_to_string(status));
                    break;
            }
        }
        delay(2000); // Giving time to connect
        connectionAttempts++;
    }


    // Transmit power defaults to the maximum: https://github.com/G6EJD/ESP32-8266-Adjust-WiFi-RF-Power-Output/blob/main/README.md
    applyWifiRadioSettings();
    LogSerial.print(F("IP_ADDRESS:"));    // !!! Line required in this format for WifiSetup.html page to show IP Address correct.
    LogSerial.print(WiFi.localIP());      // !!! Line required in this format for WifiSetup.html page to show IP Address correct.
    LogSerial.println(F("\n         "));  // !!! Line required in this format for WifiSetup.html page to show IP Address correct.

    LogSerial.print(F("Connected To Wifi Access Point:  "));
    LogSerial.println(globalVariables.SSID);
    LogSerial.print(F("Specific BSSID:                  "));  //MAC address of connected AP
    LogSerial.println(WiFi.BSSIDstr());
    LogSerial.print(F("RSSI (Signal Strength):          "));
    LogSerial.println(WiFi.RSSI());
    LogSerial.print(F("BLLED Controller IP Address:     "));
    LogSerial.println(WiFi.localIP());
    LogSerial.println();
    LogSerial.print(F("Use web browser to access 'http://"));  // Instruction for user to go to Config page
    LogSerial.print(WiFi.localIP());                           // Instruction for user to go to Config page
    LogSerial.println(F("/' to view the setup page"));         // Instruction for user to go to Config page
    LogSerial.println();
    return true;
};

void scanNetwork()
{
    //Reference / Credit - https://www.esp32.com/viewtopic.php?t=18979#p70299
    int bestRSSI = -200;
    String bestBSSID = "";

    LogSerial.println(F("Wifi network scan start"));
    int n = WiFi.scanNetworks();
    LogSerial.println(F("Network scan complete"));

    if (n == 0) {
        LogSerial.println(F("No wifi networks found :("));
    } else {
        LogSerial.print(n); // Count
        LogSerial.println(F(" Wifi networks found"));
        LogSerial.println(F("-------------------------------------------------------------------------"));
        for (int i = 0; i < n; ++i) {                // Print SSID and RSSI for each network found
            LogSerial.print(i + 1);
            LogSerial.print(F(": "));
            LogSerial.print(WiFi.SSID(i));
            LogSerial.print(F(" ("));
            LogSerial.print(WiFi.RSSI(i));
            LogSerial.print(F("dBm, "));
            if (WiFi.RSSI(i) < -50) LogSerial.print(F(" ")); //Spacing... because I just can't look at the results when misaligned.
            LogSerial.print(constrain(2 * (WiFi.RSSI(i) + 100), 0, 100));
            LogSerial.print(F("%)      "));
            LogSerial.print(F("BSSID: "));
            LogSerial.print(WiFi.BSSIDstr(i));
            LogSerial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " open" : " encrypted");

            if(WiFi.SSID(i) == globalVariables.SSID && WiFi.RSSI(i) > bestRSSI)
            {
                bestBSSID = WiFi.BSSIDstr(i);
                bestRSSI = WiFi.RSSI(i); 
            }
            delay(10);
        }
        LogSerial.println();
        if(strcmp(printerConfig.BSSID, bestBSSID.c_str()) == 0){
                LogSerial.print(F("BSSID already set to: "));
                LogSerial.print(printerConfig.BSSID);
        }
        else{
            if(strlen(printerConfig.BSSID) == 0 || printerConfig.rescanWiFiNetwork){
                strcpy(printerConfig.BSSID,bestBSSID.c_str());
                LogSerial.print(F("Saving strongest AP for: "));
                LogSerial.println(globalVariables.SSID);
                LogSerial.print(F("Strongest BSSID (MAC Address): "));
                LogSerial.print(printerConfig.BSSID);
            }
            else{
                LogSerial.print(F("Ignoring strongest AP for: "));
                LogSerial.println(globalVariables.SSID);
                LogSerial.print(F("Using alrady saved BSSID (MAC Address): "));
                LogSerial.print(printerConfig.BSSID);
            }
            LogSerial.print(F("     RSSI (Signal Strength): "));
            LogSerial.print(bestRSSI);
            LogSerial.println(F("dBm"));
        }
    }
    LogSerial.println();
}


#endif
