#ifndef _BLFLOW_LOG_SERIAL
#define _BLFLOW_LOG_SERIAL

#include <Arduino.h>

// Everything the firmware logs goes to USB serial and to a ring buffer that the Controller Log
// page reads, so the controller can be debugged without a cable.
class LogSerialClass : public Print {
public:
    static const uint32_t capacity = 8192;

    size_t write(uint8_t c) override { return write(&c, 1); }

    size_t write(const uint8_t *data, size_t length) override {
        Serial.write(data, length);
        for (size_t i = 0; i < length; ++i) buffer[(written + i) % capacity] = data[i];
        written += length;
        return length;
    }
    using Print::write;

    // Total bytes logged since boot; a reader asks for what follows the total it last saw.
    uint32_t total() const { return written; }

    // Appends the text logged after `from` and returns where it starts: later than `from` when
    // that text was overwritten, and the oldest kept text when `from` is from before a restart.
    uint32_t read(uint32_t from, String &out) const {
        const uint32_t oldest = written > capacity ? written - capacity : 0;
        if (from < oldest || from > written) from = oldest;
        out.reserve(out.length() + (written - from));
        for (uint32_t i = from; i < written; ++i) out += buffer[i % capacity];
        return from;
    }

private:
    char buffer[capacity];
    uint32_t written = 0;
};

LogSerialClass LogSerial;

#endif
