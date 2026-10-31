#ifndef BLFLOW_CONTROLLER_NAME_H
#define BLFLOW_CONTROLLER_NAME_H

// The controller name tells several controllers apart: it names the device in Home Assistant
// and gives the .local address. Without one, both keep their earlier defaults.

#include <Arduino.h>
#include <ctype.h>
#include "types.h"

const char *const defaultControllerName = "BL Smart Flow+";
const char *const defaultControllerHostname = "blflow";

inline String controllerName(const PrinterConfig &config = printerConfig) {
    return config.controllerName[0] ? String(config.controllerName) : String(defaultControllerName);
}

// Host names may hold only letters, digits, and hyphens, so each run of other characters
// becomes one hyphen: "Garage P1S" is garage-p1s.local. The setup page shows the same.
inline String controllerHostname(const PrinterConfig &config = printerConfig) {
    String host;
    bool separated = false;
    for (const char *c = config.controllerName; *c; ++c) {
        const unsigned char character = static_cast<unsigned char>(*c);
        if (!isalnum(character)) {
            separated = true;
            continue;
        }
        if (separated && host.length() > 0) host += '-';
        host += static_cast<char>(tolower(character));
        separated = false;
    }
    return host.length() > 0 ? host : String(defaultControllerHostname);
}

#endif
