#include "master.h"

#include <string.h>

namespace aa55 {

namespace {

constexpr uint8_t kRegisterRequest = kOfflineQuery | kResponse;     // 00/80: serial
constexpr uint8_t kAddressConfirm = kAllocateAddress | kResponse;   // 00/81
constexpr uint8_t kRemoveRegistration = 0x02;

}  // namespace

void Master::send(uint8_t dst, uint8_t ctrl, uint8_t func, const uint8_t* data,
                  uint8_t len) {
    uint8_t buf[kMaxFrame];
    size_t n = encode(kOwnAddress, dst, ctrl, func, data, len, buf, sizeof buf);
    if (n && cb_.send) cb_.send(buf, n);
}

void Master::restart(uint32_t now_ms) {
    if (awaiting_ == Awaiting::kExecute && cb_.on_execute) cb_.on_execute(exec_func_, false);
    if (awaiting_ == Awaiting::kExecute) exec_pending_ = false;
    state_ = State::kDiscover;
    awaiting_ = Awaiting::kNone;
    serial_[0] = 0;
    // The inverter only answers discovery while unregistered; drop a stale registration.
    send(address_, kRegister, kRemoveRegistration, nullptr, 0);
    last_send_ms_ = now_ms;
}

bool Master::execute(uint8_t func, const uint8_t* data, uint8_t len) {
    if (exec_pending_) return false;
    exec_func_ = func;
    exec_len_ = len;
    if (len) memcpy(exec_data_, data, len);
    exec_pending_ = true;
    return true;
}

void Master::feed(uint8_t b, uint32_t now_ms) {
    if (!parser_.feed(b)) return;
    const Frame& f = parser_.frame();
    if (cb_.on_frame) cb_.on_frame(f);
    // Half-duplex links (RS485) echo our own frames back.
    if (f.src == kOwnAddress) return;
    handle(f, now_ms);
}

void Master::handle(const Frame& f, uint32_t now_ms) {
    if (f.ctrl == kRegister && f.func == kRegisterRequest && f.len >= 16 &&
        state_ == State::kDiscover) {
        memcpy(serial_, f.data, 16);
        serial_[16] = 0;
        uint8_t alloc[17];
        memcpy(alloc, f.data, 16);
        alloc[16] = address_;
        send(kBroadcast, kRegister, kAllocateAddress, alloc, sizeof alloc);
        state_ = State::kAllocating;
        last_send_ms_ = now_ms;
        return;
    }
    if (f.ctrl == kRegister && f.func == kAddressConfirm && f.src == address_ &&
        state_ == State::kAllocating) {
        state_ = State::kRunning;
        last_seen_ms_ = now_ms;
        last_poll_ms_ = now_ms - poll_ms_;
        if (cb_.on_registered) cb_.on_registered(serial_);
        return;
    }
    if (state_ != State::kRunning || f.src != address_) return;
    last_seen_ms_ = now_ms;

    if (awaiting_ == Awaiting::kInfo && f.ctrl == kRead &&
        f.func == (kRunningInfo | kResponse)) {
        awaiting_ = Awaiting::kNone;
        RunningInfo info;
        if (decode_running_info(f.data, f.len, info) && cb_.on_info) cb_.on_info(info);
        return;
    }
    // EzExplorer accepts any one-byte reply from the inverter here, without checking
    // ctrl/func, so do the same.
    if (awaiting_ == Awaiting::kExecute && f.len == 1) {
        awaiting_ = Awaiting::kNone;
        exec_pending_ = false;
        if (cb_.on_execute) cb_.on_execute(exec_func_, f.data[0] == kAck);
    }
}

void Master::tick(uint32_t now_ms) {
    if (!started_) {
        started_ = true;
        restart(now_ms);
        return;
    }
    switch (state_) {
    case State::kDiscover:
        if (now_ms - last_send_ms_ >= kRetryMs) {
            send(kBroadcast, kRegister, kOfflineQuery, nullptr, 0);
            last_send_ms_ = now_ms;
        }
        return;
    case State::kAllocating:
        if (now_ms - last_send_ms_ >= kRetryMs) restart(now_ms);
        return;
    case State::kRunning:
        break;
    }

    if (now_ms - last_seen_ms_ >= kOfflineMs) {
        restart(now_ms);
        return;
    }
    if (awaiting_ != Awaiting::kNone) {
        if (now_ms - last_send_ms_ < kReplyTimeoutMs) return;
        if (awaiting_ == Awaiting::kExecute) {
            exec_pending_ = false;
            if (cb_.on_execute) cb_.on_execute(exec_func_, false);
        }
        awaiting_ = Awaiting::kNone;
    }
    if (exec_pending_) {
        send(address_, kExecute, exec_func_, exec_data_, exec_len_);
        awaiting_ = Awaiting::kExecute;
        last_send_ms_ = now_ms;
        return;
    }
    if (now_ms - last_poll_ms_ >= poll_ms_) {
        send(address_, kRead, kRunningInfo, nullptr, 0);
        awaiting_ = Awaiting::kInfo;
        last_send_ms_ = now_ms;
        last_poll_ms_ = now_ms;
    }
}

}  // namespace aa55
