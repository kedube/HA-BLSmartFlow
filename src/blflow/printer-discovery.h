#ifndef BLFLOW_PRINTER_DISCOVERY_H
#define BLFLOW_PRINTER_DISCOVERY_H

// Bambu Lab printers announce themselves over SSDP on UDP port 2021, with their serial number
// and name. The setup page lists them, so choosing one fills in the printer's IP address and
// serial number. The controller listens only for two minutes after the page last asked, and
// reads waiting announcements on each pass rather than waiting for them, so the fans and web
// pages keep running.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ctype.h>
#include <strings.h>
#include "log-serial.h"

const uint16_t printerDiscoveryPort = 2021;
const uint32_t printerDiscoveryListenMs = 120000;
const uint32_t printerSearchIntervalMs = 10000;
const size_t maxDiscoveredPrinters = 10;
const char *const bambuPrinterType = "urn:bambulab-com:device:3dprinter:1";

struct DiscoveredPrinter {
    char ip[16];
    char serial[16];
    char name[33];
    char model[33];
};

WiFiUDP discoveryUdp;
DiscoveredPrinter discoveredPrinters[maxDiscoveredPrinters];
size_t discoveredPrinterCount = 0;
bool discoveryListening = false;
uint32_t discoveryRequested = 0;
uint32_t lastPrinterSearch = 0;

// Copies an SSDP header's value, such as USN, cut to fit; header names ignore case.
template <size_t N>
bool readSsdpHeader(const char *message, const char *name, char (&value)[N]) {
    const size_t nameLength = strlen(name);
    for (const char *line = message; *line;) {
        const size_t length = strcspn(line, "\r\n");
        if (length > nameLength && line[nameLength] == ':' && strncasecmp(line, name, nameLength) == 0) {
            const char *start = line + nameLength + 1;
            const char *end = line + length;
            while (start < end && *start == ' ') ++start;
            while (end > start && end[-1] == ' ') --end;
            const size_t copied = std::min(static_cast<size_t>(end - start), N - 1);
            memcpy(value, start, copied);
            value[copied] = '\0';
            return copied > 0;
        }
        line += length;
        while (*line == '\r' || *line == '\n') ++line;
    }
    return false;
}

// Reads a printer's announcement or search reply. Anything else, including this controller's
// own search, is rejected.
bool readPrinterAnnouncement(const char *message, const char *sender, DiscoveredPrinter &printer) {
    char serial[17];
    if (!strstr(message, bambuPrinterType) || !readSsdpHeader(message, "USN", serial) ||
        strlen(serial) >= sizeof(printer.serial)) return false;
    for (const char *c = serial; *c; ++c) {
        if (!isalnum(static_cast<unsigned char>(*c))) return false;
    }
    printer = DiscoveredPrinter{};
    strcpy(printer.serial, serial);
    // The sender is the printer itself, which a Location header could only repeat.
    snprintf(printer.ip, sizeof(printer.ip), "%s", sender);
    readSsdpHeader(message, "DevName.bambu.com", printer.name);
    readSsdpHeader(message, "DevModel.bambu.com", printer.model);
    return true;
}

// A printer that announces again replaces its entry, in case its address changed.
void rememberPrinter(const DiscoveredPrinter &printer) {
    for (size_t i = 0; i < discoveredPrinterCount; ++i) {
        if (strcmp(discoveredPrinters[i].serial, printer.serial) == 0) {
            discoveredPrinters[i] = printer;
            return;
        }
    }
    if (discoveredPrinterCount == maxDiscoveredPrinters) return;
    discoveredPrinters[discoveredPrinterCount++] = printer;
    LogSerial.print(F("[Discovery] Found printer "));
    LogSerial.print(printer.serial);
    LogSerial.print(F(" at "));
    LogSerial.println(printer.ip);
}

// Starts or extends listening, and asks printers to answer now rather than at their next
// announcement.
void requestPrinterDiscovery() {
    const uint32_t now = millis();
    discoveryRequested = now;
    if (!discoveryListening) {
        discoveryListening = discoveryUdp.beginMulticast(IPAddress(239, 255, 255, 250), printerDiscoveryPort);
        if (!discoveryListening) {
            LogSerial.println(F("[Discovery] Unable to listen for printers"));
            return;
        }
        lastPrinterSearch = now - printerSearchIntervalMs;
    }
    if (now - lastPrinterSearch < printerSearchIntervalMs) return;
    lastPrinterSearch = now;
    static const char search[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:2021\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 3\r\n"
        "ST: urn:bambulab-com:device:3dprinter:1\r\n\r\n";
    discoveryUdp.beginPacket(IPAddress(239, 255, 255, 250), printerDiscoveryPort);
    discoveryUdp.write(reinterpret_cast<const uint8_t *>(search), sizeof(search) - 1);
    discoveryUdp.endPacket();
}

void printerDiscoveryLoop() {
    if (!discoveryListening) return;
    if (millis() - discoveryRequested >= printerDiscoveryListenMs) {
        discoveryUdp.stop();
        discoveryListening = false;
        return;
    }
    static char message[1024];
    // A few per pass, so a burst of announcements cannot hold up the fans.
    for (int i = 0; i < 4 && discoveryUdp.parsePacket() > 0; ++i) {
        const int length = discoveryUdp.read(message, sizeof(message) - 1);
        if (length <= 0) continue;
        message[length] = '\0';
        DiscoveredPrinter printer;
        const String sender = discoveryUdp.remoteIP().toString();
        if (readPrinterAnnouncement(message, sender.c_str(), printer)) rememberPrinter(printer);
    }
}

#endif
