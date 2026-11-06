#pragma once

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <memory>
#include <string>

#include "aa55.h"
#include "ha.h"
#include "master.h"

// Inverter <-> Home Assistant over MQTT, independent of the MQTT client and the transport:
// the Linux service and the ESP firmware only supply the I/O. Registers and polls the
// inverter, publishes discovery, state and details, and handles the control entities.
// Single-threaded: call everything from one thread (queue MQTT messages if the client
// delivers them elsewhere).

namespace ha {

class Bridge {
public:
    struct Io {
        std::function<void(const std::string& topic, const std::string& payload, bool retain)>
            publish;
        std::function<void(const std::string& topic)> subscribe;
        std::function<void(const uint8_t* frame, size_t len)> send;  // to the inverter
        std::function<void(const char* line)> log;
        std::function<void(const aa55::RunningInfo&)> on_info;  // optional, e.g. a status LED
    };

    Bridge(const Config& config, Io io, uint8_t address = 0x11, uint32_t poll_ms = 10000);

    // The inverter's port appeared or went away (every night, for the USB port).
    void port_opened();
    void port_closed();
    bool port_open() const { return master_ != nullptr; }

    void feed(const uint8_t* bytes, size_t n, uint32_t now_ms);
    void tick(uint32_t now_ms);

    // Call on every (re)connect: discovery and subscriptions are sent again, as the broker
    // may have restarted without persistence.
    void mqtt_connected() { announced_ = subscribed_ = false; }
    void message(const std::string& topic, const std::string& payload);

    const std::string& serial() const { return serial_; }

private:
    void log(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void announce();
    void on_execute(uint8_t func, bool ack);
    void trace(const char* dir, uint8_t src, uint8_t dst, uint8_t ctrl, uint8_t func,
               const uint8_t* data, uint8_t len);

    Config config_;
    Io io_;
    uint8_t address_;
    uint32_t poll_ms_;
    std::unique_ptr<aa55::Master> master_;

    std::string serial_, firmware_, details_;
    bool announced_ = false;
    // Separate from announced_: subscribing again makes the broker resend the retained
    // limit, so only after a (re)connect or a new registration, not for the details.
    bool subscribed_ = false;
    // Outlives the port: the wanted limit must survive the inverter's nightly shutdown.
    PowerLimit limit_;
    uint8_t button_ = 0;  // aa55::kStop or kStart waiting for the bus

    // Every frame from the first unanswered poll until registered again, to find out why
    // the inverter drops the registration (and why getting it back sometimes takes 30 s).
    static constexpr uint32_t kTraceMs = 120000;  // the inverter switching off for the night
    uint32_t now_ = 0;
    uint32_t registered_at_ = 0, last_reply_at_ = 0;
    bool tracing_ = false;
    uint32_t trace_start_ = 0, trace_checksum_errors_ = 0;
};

}  // namespace ha
