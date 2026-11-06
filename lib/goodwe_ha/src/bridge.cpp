#include "bridge.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace ha {

Bridge::Bridge(const Config& config, Io io, uint8_t address, uint32_t poll_ms)
    : config_(config), io_(std::move(io)), address_(address), poll_ms_(poll_ms) {}

void Bridge::log(const char* fmt, ...) {
    if (!io_.log) return;
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    io_.log(line);
}

void Bridge::port_opened() {
    aa55::Master::Callbacks cb;
    cb.send = [this](const uint8_t* f, size_t n) {
        if (tracing_ && n >= 9) trace("tx", f[2], f[3], f[4], f[5], f + 7, f[6]);
        if (io_.send) io_.send(f, n);
    };
    cb.on_frame = [this](const aa55::Frame& f) {
        if (tracing_) trace("rx", f.src, f.dst, f.ctrl, f.func, f.data, f.len);
        if (f.src == address_) last_reply_at_ = now_;
        // Unexpected while registered; logged to find out why registrations get dropped.
        if (f.ctrl == aa55::kRegister && master_->state() == aa55::Master::State::kRunning)
            log("registration frame from 0x%02X while registered: func %02X, %u bytes", f.src,
                f.func, f.len);
    };
    cb.on_registered = [this](const char* s) {
        limit_.on_registered();
        registered_at_ = now_;
        if (tracing_) {
            log("trace end: registered %u ms after the first missed poll, %u checksum errors",
                unsigned(now_ - trace_start_),
                unsigned(master_->checksum_errors() - trace_checksum_errors_));
            tracing_ = false;
        }
        // The same inverter again (it drops the registration now and then, and every
        // morning): the retained discovery still holds, and announcing it again without
        // the firmware would make HA set up every entity again.
        if (serial_ == s) {
            log("registered inverter %s again", s);
            return;
        }
        log("registered inverter %s at address 0x%02X", s, address_);
        serial_ = s;
        firmware_.clear();
        details_.clear();
        announced_ = subscribed_ = false;
    };
    cb.on_details = [this](const aa55::IdInfo* id, const aa55::Settings* st) {
        std::string details = details_json(id, st);
        std::string firmware = id ? id->firmware : firmware_;
        if (details == details_ && firmware == firmware_) return;
        log("firmware %s, settings %s", id ? id->firmware : "not read", st ? "read" : "not read");
        details_ = details;
        firmware_ = firmware;
        announced_ = false;  // again, with sw_version and the details
    };
    cb.on_info = [this](const aa55::RunningInfo& info) {
        io_.publish(state_topic(config_, serial_.c_str()), state_json(serial_.c_str(), info),
                    false);
        limit_.on_info(info);
        if (io_.on_info) io_.on_info(info);
    };
    cb.on_execute = [this](uint8_t func, bool ack) { on_execute(func, ack); };
    master_.reset(new aa55::Master(cb, address_, poll_ms_));
    log("inverter port open");
}

void Bridge::trace(const char* dir, uint8_t src, uint8_t dst, uint8_t ctrl, uint8_t func,
                   const uint8_t* data, uint8_t len) {
    if (now_ - trace_start_ > kTraceMs) {
        log("trace end: not registered after %u s", unsigned(kTraceMs / 1000));
        tracing_ = false;
        return;
    }
    char hex[3 * 24 + 4] = "";
    size_t shown = len < 24 ? len : 24;
    for (size_t i = 0; i < shown; i++) snprintf(hex + 3 * i, 4, " %02X", data[i]);
    if (shown < len) strcat(hex, " ..");
    log("trace +%u ms %s %02X>%02X %02X/%02X len %u:%s", unsigned(now_ - trace_start_), dir,
        src, dst, ctrl, func, len, hex);
}

void Bridge::port_closed() {
    if (!master_) return;
    master_.reset();
    button_ = 0;
    tracing_ = false;
    log("inverter port closed");
}

void Bridge::feed(const uint8_t* bytes, size_t n, uint32_t now_ms) {
    if (!master_) return;
    now_ = now_ms;
    for (size_t i = 0; i < n; i++) master_->feed(bytes[i], now_ms);
}

void Bridge::on_execute(uint8_t func, bool ack) {
    if (func != aa55::kRealPowerAdjust) {
        log("%s: %s", func == aa55::kStop ? "stop" : "start", ack ? "ACK" : "NAK or no reply");
        return;
    }
    limit_.on_result(ack);
    if (!ack) {
        log("power limit not acknowledged");
        return;
    }
    log("power limit %d%% applied", limit_.applied());
    io_.publish(power_limit_state_topic(config_, serial_.c_str()),
                std::to_string(limit_.applied()), true);
    int rated = rated_power(config_, serial_.c_str());
    if (rated > 0)
        io_.publish(power_limit_watts_state_topic(config_, serial_.c_str()),
                    std::to_string(limit_.applied() * rated / 100), true);
}

void Bridge::announce() {
    const char* s = serial_.c_str();
    for (const Message& m : discovery(config_, s, firmware_.c_str()))
        io_.publish(m.topic, m.payload, m.retain);
    if (!details_.empty()) io_.publish(details_topic(config_, s), details_, true);
    if (config_.power_limit && !subscribed_) {
        io_.subscribe(power_limit_command_topic(config_, s));
        io_.subscribe(power_limit_watts_command_topic(config_, s));
        io_.subscribe(stop_command_topic(config_, s));
        io_.subscribe(start_command_topic(config_, s));
        subscribed_ = true;
    }
    announced_ = true;
}

void Bridge::message(const std::string& topic, const std::string& payload) {
    if (serial_.empty() || !config_.power_limit) return;
    const char* s = serial_.c_str();
    if (topic == power_limit_command_topic(config_, s)) {
        if (limit_.command(payload.c_str()))
            log("power limit %d%% requested", limit_.wanted());
        else
            log("ignoring power limit command '%s'", payload.c_str());
    } else if (topic == power_limit_watts_command_topic(config_, s)) {
        // Republished as the retained percentage command, the one setting that is restored
        // after a restart; it comes back to us through the subscription.
        int pct;
        if (watts_to_percent(payload.c_str(), rated_power(config_, s), pct)) {
            log("power limit %s W requested", payload.c_str());
            io_.publish(power_limit_command_topic(config_, s), std::to_string(pct), true);
        } else {
            log("ignoring power limit command '%s W'", payload.c_str());
        }
    } else if (topic == stop_command_topic(config_, s)) {
        button_ = aa55::kStop;
    } else if (topic == start_command_topic(config_, s)) {
        button_ = aa55::kStart;
    }
}

void Bridge::tick(uint32_t now_ms) {
    if (!master_) return;
    now_ = now_ms;
    bool registered = master_->state() == aa55::Master::State::kRunning;
    master_->tick(now_ms);
    if (!tracing_ && registered && master_->missed_polls() > 0) {
        log("poll unanswered: registered %u s ago, last reply %u ms ago; tracing frames",
            unsigned((now_ms - registered_at_) / 1000), unsigned(now_ms - last_reply_at_));
        tracing_ = true;
        trace_start_ = now_ms;
        trace_checksum_errors_ = master_->checksum_errors();
    }
    if (registered && master_->state() != aa55::Master::State::kRunning)
        log("inverter stopped answering, registering again");
    if (!serial_.empty() && !announced_) announce();
    if (button_ && !master_->exec_pending()) {
        log("%s requested", button_ == aa55::kStop ? "stop" : "start");
        master_->execute(button_, nullptr, 0);
        button_ = 0;
    }
    uint8_t pct;
    if (!master_->exec_pending() && limit_.due(pct))
        master_->execute(aa55::kRealPowerAdjust, &pct, 1);
}

}  // namespace ha
