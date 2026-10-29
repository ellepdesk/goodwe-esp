#pragma once

#include <stddef.h>
#include <stdint.h>

// GoodWe's AA55 serial protocol, as used on the inverter UART behind the HF-A11 and on
// RS485. Frame layout and checksum follow jantenhove/GoodWeLogger:
//   AA 55 <src> <dst> <ctrl> <func> <len> <data...> <sum hi> <sum lo>
// where the checksum is the 16-bit sum of every byte before it, header included.

namespace aa55 {

constexpr uint8_t kHeader0 = 0xAA;
constexpr uint8_t kHeader1 = 0x55;
constexpr size_t kOverhead = 9;
constexpr size_t kMaxData = 255;
constexpr size_t kMaxFrame = kOverhead + kMaxData;

constexpr uint8_t kBroadcast = 0x7F;

enum Control : uint8_t {
    kRegister = 0x00,
    kRead = 0x01,
    kExecute = 0x03,
};

// Responses set bit 7 of the request's function code. See docs/protocol.md for the full
// table recovered from EzExplorer.
enum Function : uint8_t {
    kOfflineQuery = 0x00,
    kAllocateAddress = 0x01,
    kRunningInfo = 0x01,
    kStart = 0x1B,
    kStop = 0x1C,
    kRealPowerAdjust = 0x1E,
    kResponse = 0x80,
};

// Data byte of the one-byte reply to an execute command.
constexpr uint8_t kAck = 0x06;
constexpr uint8_t kNak = 0x15;

struct Frame {
    uint8_t src;
    uint8_t dst;
    uint8_t ctrl;
    uint8_t func;
    uint8_t len;
    uint8_t data[kMaxData];
};

uint16_t checksum(const uint8_t* bytes, size_t n);

// Writes a complete frame to out and returns its length, or 0 if out is too small.
size_t encode(uint8_t src, uint8_t dst, uint8_t ctrl, uint8_t func, const uint8_t* data,
              uint8_t len, uint8_t* out, size_t out_size);

// Byte-at-a-time decoder that resyncs on the AA 55 header after garbage or a bad checksum.
// A corrupt frame is dropped whole: the bus is request/reply with gaps, so the next frame
// is unlikely to start inside it, and a lost poll is simply repeated.
class Parser {
public:
    // Returns true when b completes a frame with a valid checksum; read it with frame().
    bool feed(uint8_t b);
    const Frame& frame() const { return frame_; }
    uint32_t checksum_errors() const { return checksum_errors_; }

private:
    void reset() { pos_ = 0; }

    uint8_t buf_[kMaxFrame];
    size_t pos_ = 0;
    Frame frame_{};
    uint32_t checksum_errors_ = 0;
};

// Single-phase running info (ctrl 0x01, func 0x81), scaled to SI units.
struct RunningInfo {
    float vpv1, vpv2;
    float ipv1, ipv2;
    float vac, iac, fac;
    uint16_t pac;
    uint16_t work_mode;
    float temperature;
    uint32_t error_code;
    float e_total;
    uint32_t h_total;
    float e_day;
};

constexpr uint8_t kRunningInfoLen = 46;

bool decode_running_info(const uint8_t* data, size_t len, RunningInfo& out);

}  // namespace aa55
