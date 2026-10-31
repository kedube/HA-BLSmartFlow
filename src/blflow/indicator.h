#ifndef _INDICATOR
#define _INDICATOR

#include "types.h"
#include <Arduino.h>

const int baseInterval = 200;
const int LED_PIN = 21;

struct ErrorPattern {
    String error;
    int blinkCount;
};

ErrorPattern errors[] = {
    {"no config", 1},
    {"no wifi", 2},
    {"no mqtt", 3},
};

void indicatorloop() {
    if (printerVariables.errorcode != ""){
        int blinkTimes = 0;
        for (auto &err : errors) {
            if (err.error == printerVariables.errorcode) {
                blinkTimes = err.blinkCount;
                break;
            }
        }

        if (blinkTimes == 0) {
            digitalWrite(LED_PIN, HIGH);
            return;
        }

        // Derive the pattern from the clock rather than delay(), which would stall
        // the web server and MQTT for up to two seconds on every pass.
        const unsigned long blinkPeriod = 2 * baseInterval;
        const unsigned long phase = millis() % (blinkTimes * blinkPeriod + baseInterval * 4);
        const bool lit = phase < blinkTimes * blinkPeriod && phase % blinkPeriod < baseInterval;
        digitalWrite(LED_PIN, lit ? HIGH : LOW);
    }else{
        digitalWrite(LED_PIN, HIGH);
    }
}

void indicatorsetup(){
    pinMode(LED_PIN, OUTPUT);
}

#endif