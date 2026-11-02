#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "aa55.h"

// Home Assistant MQTT discovery and state for one inverter, shared by the ESP firmware and
// the Linux service. Topics, unique IDs and state fields match goodwe_logging_proxy, so
// either can take over from the proxy without HA creating new entities.

namespace ha {

struct Message {
    std::string topic;
    std::string payload;
    bool retain;
};

struct Config {
    std::string prefix = "homeassistant";
    // Adds a number entity that sets the real power limit (execute 03 1E).
    bool power_limit = false;
};

const char* work_mode_name(uint16_t mode);  // nullptr if unknown
std::string error_names(uint32_t code);      // "OK" when no bit is set

std::string state_topic(const Config& c, const char* serial);
std::string state_json(const char* serial, const aa55::RunningInfo& info);
std::vector<Message> discovery(const Config& c, const char* serial);

std::string power_limit_command_topic(const Config& c, const char* serial);
std::string power_limit_state_topic(const Config& c, const char* serial);

// Decides when to send the power limit. The inverter can't report its limit and may not
// keep it across a restart, so it is re-sent after every registration and every return
// to Normal, but only while in Normal: that is the only mode it was verified in.
class PowerLimit {
public:
    static constexpr int kMaxAttempts = 3;

    // Takes a command payload ("0".."100"); returns false and changes nothing otherwise.
    bool command(const char* payload);
    void on_registered();
    void on_info(const aa55::RunningInfo& info);
    // Returns true, with the percentage to send, when a send is due.
    bool due(uint8_t& pct);
    // Result of the send started by the last due(). A failure is retried on the next
    // running info, up to kMaxAttempts per trigger.
    void on_result(bool ack);

    int wanted() const { return wanted_; }    // -1 until a command arrives
    int applied() const { return applied_; }  // -1 until the inverter ACKs

private:
    void trigger();

    int wanted_ = -1;
    int applied_ = -1;
    bool pending_ = false;
    bool in_flight_ = false;
    bool normal_ = false;
    bool fresh_ = false;
    int sent_ = -1;
    int attempts_ = 0;
};

}  // namespace ha
