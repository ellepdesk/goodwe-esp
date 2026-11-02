#pragma once

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>

// Byte transports to the inverter shared by the host tools: its USB HID port, or a serial
// adapter on the inverter UART / RS485.

struct Transport {
    virtual ~Transport() = default;
    virtual bool write(const uint8_t* frame, size_t len) = 0;
    // Returns the number of bytes read (0 on timeout), or -1 on error.
    virtual int read(uint8_t* buf, size_t size, int timeout_ms) = 0;
};

// Both return nullptr and set error on failure.
std::unique_ptr<Transport> open_serial(const char* path, int baud, std::string& error);
std::unique_ptr<Transport> open_hid(unsigned vid, unsigned pid, size_t report_size,
                                    std::string& error);

uint32_t now_ms();
void hexdump(const char* tag, const uint8_t* b, size_t n);
