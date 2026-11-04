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
    // Adds the control entities: power limit numbers (execute 03 1E) and stop/start
    // buttons (03 1C/03 1B).
    bool power_limit = false;
    // Rated power in W for the watt version of the power limit; 0 takes it from the serial.
    int rated_power = 0;
};

const char* work_mode_name(uint16_t mode);  // nullptr if unknown
std::string error_names(uint32_t code);      // "OK" when no bit is set

std::string state_topic(const Config& c, const char* serial);
std::string state_json(const char* serial, const aa55::RunningInfo& info);
// sw_version (the firmware from ID info) goes into the device block when known.
std::vector<Message> discovery(const Config& c, const char* serial,
                               const char* sw_version = nullptr);

// ID info and settings, read once per registration, for the diagnostic sensors. Either
// may be nullptr when the inverter didn't answer; publish retained.
std::string details_topic(const Config& c, const char* serial);
std::string details_json(const aa55::IdInfo* id, const aa55::Settings* settings);

std::string power_limit_command_topic(const Config& c, const char* serial);
std::string power_limit_state_topic(const Config& c, const char* serial);
std::string power_limit_watts_command_topic(const Config& c, const char* serial);
std::string power_limit_watts_state_topic(const Config& c, const char* serial);

// Stop/start buttons. They are buttons, not a switch: a stopped inverter reports plain
// Wait, so there is no state to show. Disabled by default in HA; they are for
// maintenance, not daily use.
std::string stop_command_topic(const Config& c, const char* serial);
std::string start_command_topic(const Config& c, const char* serial);

// Config::rated_power, else the model's W from the serial (1 3600 DS ... -> 3600); 0 if
// neither gives one.
int rated_power(const Config& c, const char* serial);
// Converts a watt command ("0".."rated") to a percentage, rounded down so the limit never
// exceeds the request. Returns false for a bad payload.
bool watts_to_percent(const char* payload, int rated, int& pct);

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
