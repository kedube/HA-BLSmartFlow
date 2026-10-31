#ifndef _MQTTMANAGER
#define _MQTTMANAGER

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> 

#include "AutoGrowBufferStream.h"
#include "connect-timeout.h"
#include "diagnostics.h"
#include "log-serial.h"
#include "types.h"
#include "mqtt-report.h"
#include "fans.h"

WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);

String device_topic;
String report_topic;
String clientId = "BLLED-";

AutoGrowBufferStream stream;

// Each failed attempt blocks the loop, so retries slow down while the printer is off.
const unsigned long mqttRetryMinimumMs = 3000;
const unsigned long mqttRetryMaximumMs = 30000;
unsigned long mqttRetryMs = 0; // Wait before the next attempt; grows while attempts fail
unsigned long mqttattempt = 0;
unsigned long lastMQTTupdate = millis();

void ParseMQTTState(int code){
    switch (code)
    {
    case -4: // MQTT_CONNECTION_TIMEOUT
        LogSerial.println(F("MQTT TIMEOUT"));
        printerVariables.errorcode = "no mqtt";
        break;
    case -3: // MQTT_CONNECTION_LOST
        LogSerial.println(F("MQTT CONNECTION_LOST"));
        printerVariables.errorcode = "no mqtt";
        break;
    case -2: // MQTT_CONNECT_FAILED
        LogSerial.println(F("MQTT CONNECT_FAILED"));
        printerVariables.errorcode = "no mqtt";
        break;
    case -1: // MQTT_DISCONNECTED
        LogSerial.println(F("MQTT DISCONNECTED"));
        printerVariables.errorcode = "no mqtt";
        break;
    case 0:  // MQTT_CONNECTED
        LogSerial.println(F("MQTT CONNECTED"));
        if (printerVariables.errorcode == "no mqtt"){
            //if this routine set the error code, clear it
            printerVariables.errorcode = "";
        }
        break;
    case 1:  // MQTT_CONNECT_BAD_PROTOCOL
        LogSerial.println(F("MQTT BAD PROTOCOL"));
        printerVariables.errorcode = "no mqtt";
        break;
    case 2:  // MQTT_CONNECT_BAD_CLIENT_ID
        LogSerial.println(F("MQTT BAD CLIENT ID"));
        printerVariables.errorcode = "no mqtt";
        break;
    case 3:  // MQTT_CONNECT_UNAVAILABLE
        LogSerial.println(F("MQTT UNAVAILABLE"));
        printerVariables.errorcode = "no mqtt";
        break;
    case 4:  // MQTT_CONNECT_BAD_CREDENTIALS
        LogSerial.println(F("MQTT BAD CREDENTIALS"));
        printerVariables.errorcode = "no mqtt";
        break;
    case 5: // MQTT UNAUTHORIZED
        LogSerial.println(F("MQTT UNAUTHORIZED"));
        printerVariables.errorcode = "no mqtt";
        break;
    }
}

void connectMqtt(){
    if(WiFi.status() != WL_CONNECTED || !printerConfig.printerConnection){
        //Abort MQTT connection attempt when no Wifi, or while turned off in Debug & Diagnostics
        return;
    }
    if (!mqttClient.connected() && (millis() - mqttattempt) >= mqttRetryMs){
        LogSerial.println(F("Connecting to mqtt..."));
        const bool connected = mqttClient.connect(clientId.c_str(),"bblp",printerConfig.accessCode);
        // Measure the retry delay from the end of the attempt, which can block for seconds.
        mqttattempt = millis();
        if (connected){
            mqttRetryMs = 0;
            LogSerial.print(F("MQTT connected, subscribing to MQTT Topic:  "));
            LogSerial.println(report_topic);
            mqttClient.subscribe(report_topic.c_str());
            printerVariables.online = true;
            printerVariables.disconnectMQTTms = 0;
            if (printerVariables.errorcode == "no mqtt"){
                //if this routine set the error code, clear it
                printerVariables.errorcode = "";
            }
            //LogSerial.println(F("Updating LEDs from MQTT connect"));
        }else{
            mqttRetryMs = std::min(std::max(mqttRetryMs * 2, mqttRetryMinimumMs), mqttRetryMaximumMs);
            LogSerial.println(F("Failed to connect with error code: "));
            LogSerial.print(mqttClient.state());
            LogSerial.print(F("  "));
            ParseMQTTState(mqttClient.state());
            // if(mqttClient.state() == 5){
            //     LogSerial.println(F("Restarting Device"));
            //     delay(1000);
            //     ESP.restart();                
            // }
        }
    }
}

void ParseCallback(char *topic, byte *payload, unsigned int length){
    if (parsePrinterReport(payload, length) && printerConfig.debuging) {
        LogSerial.println(F("Change from MQTT"));
    }
}


void mqttCallback(char *topic, byte *payload, unsigned int length){
    printerTraffic.add(stream.current_length(), millis());
    ParseCallback(topic, (byte *)stream.get_buffer(), stream.current_length());
    stream.flush();
}

void setupMqtt(){
    clientId += String(random(0xffff), HEX);
    LogSerial.print(F("Setting up MQTT with Bambu Lab Printer IP address: "));
    LogSerial.println(printerConfig.printerIP);

    device_topic = String("device/") + printerConfig.serialNumber;
    report_topic = device_topic + String("/report");

    wifiSecureClient.setInsecure();
    // Fail fast while the printer is off so the web page and Home Assistant stay responsive.
    setConnectTimeout(wifiSecureClient, 3000);
    wifiSecureClient.setHandshakeTimeout(10);
    mqttClient.setBufferSize(1024); //1024
    mqttClient.setServer(printerConfig.printerIP, 8883);
    mqttClient.setStream(stream);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setSocketTimeout(20);
    LogSerial.println(F("Finished setting up MQTT"));
    connectMqtt();
}

void mqttloop(){
    if(WiFi.status() != WL_CONNECTED){
        printerCommunicationLost();
        fanloop(); // Restore normal outputs before any blocking Wi-Fi reconnect.
        //Abort MQTT connection attempt when no Wifi
        return;
    }
    if (!printerConfig.printerConnection){
        // Turned off under Debug & Diagnostics, to rule the printer connection out.
        if (mqttClient.connected()) {
            mqttClient.disconnect();
            LogSerial.println(F("Printer connection turned off"));
        }
        printerCommunicationLost();
        fanloop();
        return;
    }
    if (!mqttClient.connected()){
        printerCommunicationLost();
        fanloop(); // Restore normal outputs before the blocking MQTT reconnect.
        //Only sent the timer from the first instance of a MQTT disconnect
        if(printerVariables.disconnectMQTTms == 0) {
            printerVariables.disconnectMQTTms = millis();
            //Record last time MQTT dropped connection
            LogSerial.println(F("MQTT dropped during mqttloop"));
            ParseMQTTState(mqttClient.state());
        }
        connectMqtt();
        return;
    }
    else{
        printerVariables.disconnectMQTTms = 0;
    }
    mqttClient.loop();
    if (!mqttClient.connected()) {
        printerCommunicationLost();
        fanloop();
    }
    delay(10);
}

#endif
