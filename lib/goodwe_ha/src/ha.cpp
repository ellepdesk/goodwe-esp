#include "ha.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ha {

namespace {

struct Sensor {
    const char* tag;
    const char* name;
    const char* uom;
    const char* device_class;
    const char* state_class;
    const char* entity_category;
    const char* icon;
};

// Same list, names and order as goodwe_logging_proxy's goodwemqttclient.py.
const Sensor kSensors[] = {
    {"voltage_pv1", "DC Voltage String 1", "V", "voltage", "measurement", nullptr, nullptr},
    {"voltage_pv2", "DC Voltage String 2", "V", "voltage", "measurement", nullptr, nullptr},
    {"current_pv1", "DC Current String 1", "A", "current", "measurement", nullptr, nullptr},
    {"current_pv2", "DC Current String 2", "A", "current", "measurement", nullptr, nullptr},
    {"voltage_ac", "AC Voltage", "V", "voltage", "measurement", nullptr, nullptr},
    {"current_ac", "AC Current", "A", "current", "measurement", nullptr, nullptr},
    {"freq_ac", "AC Frequency", "Hz", "frequency", "measurement", nullptr, nullptr},
    {"power_ac", "AC Power", "W", "power", "measurement", nullptr, nullptr},
    {"temperature", "Inverter Temperature", "°C", "temperature", "measurement", nullptr,
     nullptr},
    {"kwh_total", "Total Generated Energy", "kWh", "energy", "total_increasing", nullptr,
     nullptr},
    {"hours_total", "Operating Hours", "h", "duration", "total_increasing", nullptr, nullptr},
    {"kwh_daily", "Daily Generated Energy", "kWh", "energy", "total_increasing", nullptr,
     nullptr},
    {"work_mode", "Work Mode", nullptr, nullptr, nullptr, nullptr, "mdi:solar-power"},
    {"errors", "Errors", nullptr, nullptr, nullptr, nullptr, "mdi:alert-circle-outline"},
    {"error_code", "Error Code", nullptr, nullptr, nullptr, "diagnostic", nullptr},
    {"voltage_bus", "DC Bus Voltage", "V", "voltage", "measurement", "diagnostic", nullptr},
};

constexpr int kExpireAfterS = 600;

// EzExplorer's names; see docs/protocol.md for where GoodWe's PDF differs.
const char* const kErrorBits[32] = {
    "GFCI Device Failure",
    "AC HCT Failure",
    "TBD",
    "DCI Consistency Failure",
    "GFCI Consistency Failure",
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    "Utility Loss",
    "Ground I Failure",
    "DC Bus High",
    "Internal Fan Failure",
    "Over Temperature",
    "Auto Test Failure",
    "PV Over Voltage",
    "External Fan Failure",
    "Vac Failure",
    "Isolation Failure",
    "DC Injection High",
    nullptr,
    nullptr,
    "Fac Consistency Failure",
    "Vac Consistency Failure",
    nullptr,
    "Relay Check Failure",
    nullptr,
    nullptr,
    nullptr,
    "Fac Failure",
    "EEPROM R/W Failure",
    "Internal Communication Failure",
};

std::string format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string format(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return buf;
}

void add_field(std::string& json, const char* key, const char* value) {
    if (!value) return;
    json += format(",\"%s\":\"%s\"", key, value);
}

std::string node(const Config& c, const char* component, const char* serial) {
    return c.prefix + "/" + component + "/goodwe_" + serial;
}

std::string device_json(const char* serial) {
    // The proxy derives the model from the serial: 1 3600 DS ... -> GW-3600-DS.
    std::string s(serial);
    std::string model = s.size() >= 7 ? "GW-" + s.substr(1, 4) + "-" + s.substr(5, 2) : "GW";
    return format("{\"identifiers\":[\"%s\"],\"name\":\"GoodWe Inverter\","
                  "\"manufacturer\":\"GoodWe\",\"model\":\"%s\"}",
                  serial, model.c_str());
}

// Parses a whole command payload as a number in [0, max].
bool parse_number(const char* payload, double max, double& v) {
    char* end;
    v = strtod(payload, &end);
    while (*end == ' ' || *end == '\n' || *end == '\r') end++;
    return end != payload && !*end && v >= 0 && v <= max;
}

}  // namespace

const char* work_mode_name(uint16_t mode) {
    static const char* const kModes[] = {"Wait", "Normal", "Fault", "Permanent Fault"};
    return mode < 4 ? kModes[mode] : nullptr;
}

std::string error_names(uint32_t code) {
    std::string out;
    for (int bit = 0; bit < 32; bit++) {
        if (!(code & (1UL << bit))) continue;
        if (!out.empty()) out += ", ";
        out += kErrorBits[bit] ? kErrorBits[bit] : format("Unknown bit %d", bit);
    }
    return out.empty() ? "OK" : out;
}

std::string state_topic(const Config& c, const char* serial) {
    return node(c, "sensor", serial) + "/state";
}

std::string state_json(const char* serial, const aa55::RunningInfo& i) {
    const char* mode = work_mode_name(i.work_mode);
    std::string mode_str = mode ? mode : format("Unknown (%u)", i.work_mode);
    return format("{\"device_id\":\"%s\",\"voltage_pv1\":%.1f,\"voltage_pv2\":%.1f,"
                  "\"current_pv1\":%.1f,\"current_pv2\":%.1f,\"voltage_ac\":%.1f,"
                  "\"current_ac\":%.1f,\"freq_ac\":%.2f,\"power_ac\":%u,",
                  serial, i.vpv1, i.vpv2, i.ipv1, i.ipv2, i.vac, i.iac, i.fac, i.pac) +
           format("\"work_mode\":\"%s\",\"temperature\":%.1f,\"error_code\":%lu,"
                  "\"errors\":\"%s\",",
                  mode_str.c_str(), i.temperature, (unsigned long)i.error_code,
                  error_names(i.error_code).c_str()) +
           format("\"kwh_total\":%.1f,\"hours_total\":%lu,\"voltage_bus\":%.1f,"
                  "\"kwh_daily\":%.1f}",
                  i.e_total, (unsigned long)i.h_total, i.vbus, i.e_day);
}

std::string power_limit_command_topic(const Config& c, const char* serial) {
    return node(c, "number", serial) + "/power_limit/set";
}

std::string power_limit_state_topic(const Config& c, const char* serial) {
    return node(c, "number", serial) + "/power_limit/state";
}

std::string power_limit_watts_command_topic(const Config& c, const char* serial) {
    return node(c, "number", serial) + "/power_limit_w/set";
}

std::string power_limit_watts_state_topic(const Config& c, const char* serial) {
    return node(c, "number", serial) + "/power_limit_w/state";
}

std::string stop_command_topic(const Config& c, const char* serial) {
    return node(c, "button", serial) + "/stop/press";
}

std::string start_command_topic(const Config& c, const char* serial) {
    return node(c, "button", serial) + "/start/press";
}

int rated_power(const Config& c, const char* serial) {
    if (c.rated_power > 0) return c.rated_power;
    if (strlen(serial) < 5) return 0;
    char digits[5] = {serial[1], serial[2], serial[3], serial[4], 0};
    for (int i = 0; i < 4; i++)
        if (digits[i] < '0' || digits[i] > '9') return 0;
    return atoi(digits);
}

bool watts_to_percent(const char* payload, int rated, int& pct) {
    double w;
    if (rated <= 0 || !parse_number(payload, rated, w)) return false;
    pct = int(w * 100 / rated);
    return true;
}

std::vector<Message> discovery(const Config& c, const char* serial) {
    std::vector<Message> out;
    const std::string device = device_json(serial);
    const std::string state = state_topic(c, serial);
    for (const Sensor& s : kSensors) {
        std::string json = format("{\"name\":\"%s\",\"state_topic\":\"%s\",", s.name,
                                  state.c_str()) +
                           format("\"unique_id\":\"%s_%s\",\"value_template\":"
                                  "\"{{ value_json.%s }}\"",
                                  s.tag, serial, s.tag);
        json += ",\"device\":" + device;
        add_field(json, "unit_of_measurement", s.uom);
        add_field(json, "device_class", s.device_class);
        add_field(json, "state_class", s.state_class);
        add_field(json, "entity_category", s.entity_category);
        add_field(json, "icon", s.icon);
        // Totals keep their last value overnight; everything else is real-time.
        if (!s.state_class || strncmp(s.state_class, "total", 5) != 0)
            json += format(",\"expire_after\":%d", kExpireAfterS);
        json += "}";
        out.push_back({node(c, "sensor", serial) + "/" + s.tag + "/config", json, true});
    }

    std::string limit_config = node(c, "number", serial) + "/power_limit/config";
    std::string watts_config = node(c, "number", serial) + "/power_limit_w/config";
    std::string stop_config = node(c, "button", serial) + "/stop/config";
    std::string start_config = node(c, "button", serial) + "/start/config";
    const int rated = rated_power(c, serial);
    // An empty retained config removes an entity that was announced before.
    if (!c.power_limit) {
        out.push_back({stop_config, "", true});
        out.push_back({start_config, "", true});
        out.push_back({limit_config, "", true});
        out.push_back({watts_config, "", true});
        return out;
    }
    struct Button {
        const std::string& config;
        std::string topic;
        const char* tag;
        const char* name;
        const char* icon;
    };
    const Button buttons[] = {
        {stop_config, stop_command_topic(c, serial), "stop", "Stop Inverter", "mdi:power-off"},
        {start_config, start_command_topic(c, serial), "start", "Start Inverter", "mdi:power-on"},
    };
    for (const Button& b : buttons) {
        out.push_back(
            {b.config,
             format("{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"command_topic\":\"%s\",",
                    b.name, b.tag, serial, b.topic.c_str()) +
                 format("\"entity_category\":\"config\",\"enabled_by_default\":false,"
                        "\"icon\":\"%s\",\"device\":",
                        b.icon) +
                 device + "}",
             true});
    }
    // retain makes the broker hand the last setting back after a restart of this service.
    std::string json =
        format("{\"name\":\"Power Limit\",\"unique_id\":\"power_limit_%s\",", serial) +
        format("\"command_topic\":\"%s\",", power_limit_command_topic(c, serial).c_str()) +
        format("\"state_topic\":\"%s\",", power_limit_state_topic(c, serial).c_str()) +
        "\"min\":0,\"max\":100,\"step\":1,\"mode\":\"box\",\"unit_of_measurement\":\"%\","
        "\"retain\":true,\"entity_category\":\"config\",\"icon\":\"mdi:transmission-tower-export\","
        "\"device\":" +
        device + "}";
    out.push_back({limit_config, json, true});
    if (rated <= 0) {
        out.push_back({watts_config, "", true});
        return out;
    }
    // Not retained: the owner turns it into a retained percentage command, so a restart
    // has a single setting to restore.
    json = format("{\"name\":\"Power Limit (W)\",\"unique_id\":\"power_limit_w_%s\",",
                  serial) +
           format("\"command_topic\":\"%s\",",
                  power_limit_watts_command_topic(c, serial).c_str()) +
           format("\"state_topic\":\"%s\",", power_limit_watts_state_topic(c, serial).c_str()) +
           format("\"min\":0,\"max\":%d,\"step\":1,", rated) +
           "\"mode\":\"box\",\"unit_of_measurement\":\"W\",\"device_class\":\"power\","
           "\"entity_category\":\"config\",\"icon\":\"mdi:transmission-tower-export\","
           "\"device\":" +
           device + "}";
    out.push_back({watts_config, json, true});
    return out;
}

bool PowerLimit::command(const char* payload) {
    double v;
    if (!parse_number(payload, 100, v)) return false;
    wanted_ = int(v + 0.5);
    trigger();
    return true;
}

void PowerLimit::on_registered() {
    normal_ = false;
    in_flight_ = false;
    applied_ = -1;
    trigger();
}

void PowerLimit::on_info(const aa55::RunningInfo& info) {
    bool normal = info.work_mode == 1;
    if (normal && !normal_) trigger();
    normal_ = normal;
    fresh_ = true;
}

bool PowerLimit::due(uint8_t& pct) {
    if (!pending_ || in_flight_ || !normal_ || !fresh_ || wanted_ < 0) return false;
    fresh_ = false;
    in_flight_ = true;
    attempts_++;
    sent_ = wanted_;
    pct = uint8_t(sent_);
    return true;
}

void PowerLimit::on_result(bool ack) {
    if (!in_flight_) return;
    in_flight_ = false;
    if (ack) {
        applied_ = sent_;
        // A command that arrived while this one was in flight still has to go out.
        if (wanted_ == sent_) pending_ = false;
    } else if (attempts_ >= kMaxAttempts) {
        pending_ = false;
    }
}

void PowerLimit::trigger() {
    pending_ = true;
    attempts_ = 0;
}

}  // namespace ha
