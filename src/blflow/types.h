#ifndef _TYPES
#define _TYPES

#include <Arduino.h>
#include "fan-curve.h"
#include "printer-fan.h"
#include "post-print-types.h"
#include "control-mode.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct PrinterVaraiblesStruct{
        PrinterFanReading fans[printerFanCount];
        double nozzletemp = 0;
        bool online = false;
        bool printWasRunning = false;
        bool printInProgress = false;   // RUNNING since the last end state, even across reconnects
        PostPrintRun postPrint[2];
        String errorcode = "";
        int printStage = -1;            // stg_cur; -1 until reported
        uint64_t hmsCode = 0;           // Most severe HMS code reported, or 0
        //Time since
        unsigned long disconnectMQTTms = 0;        
    } PrinterVariables;
    PrinterVariables printerVariables;

    typedef struct GlobalVariablesStruct{
        char SSID[33];                  // 32-character SSID plus terminator
        char APPW[64];                  // 63-character WPA2 passphrase plus terminator
        String FWVersion = "Stable " STRVERSION " (" STRBUILDDATE ")";
        bool started = false;
        int fanSpeed = 0;
        int fanSpeed2 = 0;
        // Increments when Settings change, so open web pages can reload them.
        uint32_t settingsRevision = 0;
        // Hybrid control: Home Assistant has set a fan speed since the printer last had control.
        bool haOverride = false;
    } GlobalVariables;
    GlobalVariables globalVariables;

    typedef struct PrinterConfigStruct
    {
        char printerIP[16];             //BBLP IP Address - used for MQTT reports
        char accessCode[9];             //BBLP Access Code - used for MQTT reports
        char serialNumber[16];          //BBLP Serial Number - used for MQTT reports

        char BSSID[18];                 //Nominated AP to connect to (Useful if multiple accesspoints with same name)
        bool rescanWiFiNetwork = false; //Scans available WiFi networks for strongest signal

        // Debugging
        bool debuging = true;          //Debugging for all interactions through functions
        bool debugingchange = true;     //Default debugging level - to shows onChange
        bool mqttdebug = false;         //Writes each packet from BBLP to the serial log

        bool fansSynchronized = true;
        bool staticFan = false;
        int staticFanSpeed = 0;
        bool followPrinterFan = false;
        PrinterFanSource printerFanSource = PrinterFanSource::Chamber;
        bool staticFan2 = false;
        int staticFanSpeed2 = 0;
        bool followPrinterFan2 = false;
        PrinterFanSource printerFanSource2 = PrinterFanSource::Chamber;
        PostPrintSettings postPrint;
        PostPrintSettings postPrint2;

        FanCurve fanGraph = defaultFanCurve();
        // Empty until independent settings are first saved; then retained in sync mode.
        FanCurve fanGraph2;

        // Names the device in Home Assistant and gives the .local address; empty uses the defaults.
        char controllerName[33] = "";

        // Home Assistant MQTT broker; a separate connection from the printer's.
        bool haEnabled = false;
        char haHost[65] = "";
        int haPort = 1883;
        char haUser[65] = "";
        char haPassword[65] = "";
        ControlMode controlMode = ControlMode::Hybrid;
        int haFanSpeed = 0;             // Speeds Home Assistant last held the fans at
        int haFanSpeed2 = 0;

        // Web page login; both empty leaves the pages open.
        char webUser[33] = "";
        char webPassword[65] = "";

        // Debug & Diagnostics: Wi-Fi radio tuning and switches for ruling out a subsystem.
        // All apply when saved.
        int wifiTxPower = 78;           // Quarter-dBm steps; 78 is the radio's maximum, 19.5 dBm
        bool wifiSleep = true;          // Modem sleep, the ESP32 default
        bool printerConnection = true;  // MQTT connection to the printer
        bool ssdpEnabled = true;        // Discovery by the xtouch project

    } PrinterConfig;
    PrinterConfig printerConfig;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
