#include "aa55.h"

#include <string.h>

namespace aa55 {

namespace {

uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

uint32_t be32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

}  // namespace

uint16_t checksum(const uint8_t* bytes, size_t n) {
    uint16_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += bytes[i];
    return sum;
}

size_t encode(uint8_t src, uint8_t dst, uint8_t ctrl, uint8_t func, const uint8_t* data,
              uint8_t len, uint8_t* out, size_t out_size) {
    size_t total = kOverhead + len;
    if (out_size < total) return 0;
    out[0] = kHeader0;
    out[1] = kHeader1;
    out[2] = src;
    out[3] = dst;
    out[4] = ctrl;
    out[5] = func;
    out[6] = len;
    if (len) memcpy(out + 7, data, len);
    uint16_t sum = checksum(out, 7 + len);
    out[7 + len] = sum >> 8;
    out[8 + len] = sum & 0xFF;
    return total;
}

bool Parser::feed(uint8_t b) {
    if (pos_ == 0 && b != kHeader0) return false;
    if (pos_ == 1 && b != kHeader1) {
        pos_ = b == kHeader0 ? 1 : 0;
        return false;
    }
    buf_[pos_++] = b;
    if (pos_ < 7) return false;

    size_t total = kOverhead + buf_[6];
    if (pos_ < total) return false;

    uint16_t expected = be16(buf_ + total - 2);
    if (checksum(buf_, total - 2) != expected) {
        checksum_errors_++;
        reset();
        return false;
    }

    frame_.src = buf_[2];
    frame_.dst = buf_[3];
    frame_.ctrl = buf_[4];
    frame_.func = buf_[5];
    frame_.len = buf_[6];
    memcpy(frame_.data, buf_ + 7, frame_.len);
    reset();
    return true;
}

bool decode_running_info(const uint8_t* d, size_t len, RunningInfo& out) {
    if (len < kRunningInfoLen) return false;
    out.vpv1 = be16(d + 0) / 10.0f;
    out.vpv2 = be16(d + 2) / 10.0f;
    out.ipv1 = be16(d + 4) / 10.0f;
    out.ipv2 = be16(d + 6) / 10.0f;
    out.vac = be16(d + 8) / 10.0f;
    out.iac = be16(d + 10) / 10.0f;
    out.fac = be16(d + 12) / 100.0f;
    out.pac = be16(d + 14);
    out.work_mode = be16(d + 16);
    out.temperature = be16(d + 18) / 10.0f;
    out.error_code = be32(d + 20);
    out.e_total = be32(d + 24) / 10.0f;
    out.h_total = be32(d + 28);
    out.e_day = be16(d + 44) / 10.0f;
    return true;
}

}  // namespace aa55
