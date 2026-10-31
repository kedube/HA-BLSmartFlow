#ifndef BLFLOW_CONNECT_TIMEOUT_H
#define BLFLOW_CONNECT_TIMEOUT_H

#include <Arduino.h>

// Bound how long a failed connection blocks the main loop; the TLS client
// otherwise waits 30 s. Arduino-ESP32 2.x takes seconds through setTimeout(),
// while 3.x uses milliseconds through setConnectionTimeout().
template <typename Client>
void setConnectTimeout(Client &client, uint32_t milliseconds) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    client.setConnectionTimeout(milliseconds);
#else
    client.setTimeout(milliseconds / 1000);
#endif
}

#endif
