#pragma once

#include <stdint.h>

#include "aa55.h"

// Colour of the optional addressable status LED: orange while the inverter waits, green in
// Normal, red on a fault or any error bit, off without fresh running info (night, or
// unplugged). Hardware-free so it can be tested; the firmware writes the colour out.

namespace status_led {

struct Rgb {
    uint8_t r, g, b;
};

constexpr Rgb kOff{0, 0, 0};
constexpr uint32_t kStaleMs = 60000;

class Status {
public:
    // brightness scales full-on (255) colours; a bare WS2812 at 255 is glaring.
    explicit Status(uint8_t brightness = 32) : brightness_(brightness) {}

    void on_info(const aa55::RunningInfo& info, uint32_t now_ms);
    Rgb color(uint32_t now_ms) const;

private:
    uint8_t brightness_;
    bool seen_ = false;
    uint32_t last_ms_ = 0;
    Rgb full_ = kOff;
};

// Unscaled colour for one running-info reply.
Rgb color_for(const aa55::RunningInfo& info);

}  // namespace status_led
