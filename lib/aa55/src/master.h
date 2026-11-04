#pragma once

#include <functional>

#include "aa55.h"

// AA55 bus master: registers the inverter, polls running info and sends execute
// commands. Transport- and clock-agnostic so the ESP firmware and the host CLI share it:
// the owner feeds received bytes, calls tick() regularly and supplies a send function.

namespace aa55 {

class Master {
public:
    enum class State { kDiscover, kAllocating, kRunning };

    struct Callbacks {
        std::function<void(const uint8_t* frame, size_t len)> send;
        std::function<void(const Frame&)> on_frame;
        std::function<void(const char* serial)> on_registered;
        std::function<void(const RunningInfo&)> on_info;
        // ack is false for a NAK, a malformed reply or a timeout.
        std::function<void(uint8_t func, bool ack)> on_execute;
        // Once per registration, after ID info and settings were read; either is nullptr
        // if the inverter didn't answer.
        std::function<void(const IdInfo*, const Settings*)> on_details;
    };

    static constexpr uint8_t kOwnAddress = 0x80;
    static constexpr uint32_t kRetryMs = 2000;
    static constexpr uint32_t kReplyTimeoutMs = 2000;
    static constexpr uint32_t kOfflineMs = 30000;
    static constexpr int kDetailAttempts = 3;

    Master(Callbacks cb, uint8_t inverter_address = 0x11, uint32_t poll_ms = 10000)
        : cb_(std::move(cb)), address_(inverter_address), poll_ms_(poll_ms) {}

    void feed(uint8_t b, uint32_t now_ms);
    void tick(uint32_t now_ms);

    // Queues one execute command (ctrl 0x03), sent once registered and the bus is idle.
    // Returns false if another command is still queued.
    bool execute(uint8_t func, const uint8_t* data, uint8_t len);

    State state() const { return state_; }
    bool exec_pending() const { return exec_pending_; }
    const char* serial() const { return serial_; }

private:
    enum class Awaiting { kNone, kInfo, kExecute, kIdInfo, kSettings };

    void send(uint8_t dst, uint8_t ctrl, uint8_t func, const uint8_t* data, uint8_t len);
    void handle(const Frame& f, uint32_t now_ms);
    void restart(uint32_t now_ms);
    void next_detail();

    Callbacks cb_;
    Parser parser_;
    uint8_t address_;
    uint32_t poll_ms_;

    State state_ = State::kDiscover;
    Awaiting awaiting_ = Awaiting::kNone;
    bool started_ = false;
    uint32_t last_send_ms_ = 0;
    uint32_t last_seen_ms_ = 0;
    uint32_t last_poll_ms_ = 0;
    char serial_[17] = {};

    // Details read once after registration, before the first poll.
    enum class Detail { kIdInfo, kSettings, kDone };
    Detail detail_ = Detail::kDone;
    int detail_attempts_ = 0;
    bool have_id_ = false, have_settings_ = false;
    IdInfo id_{};
    Settings settings_{};

    bool exec_pending_ = false;
    uint8_t exec_func_ = 0;
    uint8_t exec_data_[kMaxData] = {};
    uint8_t exec_len_ = 0;
};

}  // namespace aa55
