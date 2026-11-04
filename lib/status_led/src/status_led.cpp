#include "status_led.h"

namespace status_led {

namespace {

constexpr Rgb kOrange{255, 80, 0};
constexpr Rgb kGreen{0, 255, 0};
constexpr Rgb kRed{255, 0, 0};

uint8_t scale(uint8_t c, uint8_t brightness) {
    return uint8_t((c * brightness + 127) / 255);
}

}  // namespace

Rgb color_for(const aa55::RunningInfo& info) {
    if (info.error_code) return kRed;
    switch (info.work_mode) {
    case 0: return kOrange;  // Wait
    case 1: return kGreen;   // Normal
    default: return kRed;    // Fault, Permanent Fault, or unknown
    }
}

void Status::on_info(const aa55::RunningInfo& info, uint32_t now_ms) {
    full_ = color_for(info);
    last_ms_ = now_ms;
    seen_ = true;
}

Rgb Status::color(uint32_t now_ms) const {
    if (!seen_ || now_ms - last_ms_ > kStaleMs) return kOff;
    return {scale(full_.r, brightness_), scale(full_.g, brightness_),
            scale(full_.b, brightness_)};
}

}  // namespace status_led
