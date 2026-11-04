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
    // suggested_display_precision, matching the data's resolution: HA's defaults per
    // device class (whole V and Hz, 2 decimals for A and kWh) don't. -1 for text.
    int precision;
};

// Same list, names and order as goodwe_logging_proxy's goodwemqttclient.py.
const Sensor kSensors[] = {
    {"voltage_pv1", "DC Voltage String 1", "V", "voltage", "measurement", nullptr, nullptr, 1},
    {"voltage_pv2", "DC Voltage String 2", "V", "voltage", "measurement", nullptr, nullptr, 1},
    {"current_pv1", "DC Current String 1", "A", "current", "measurement", nullptr, nullptr, 1},
    {"current_pv2", "DC Current String 2", "A", "current", "measurement", nullptr, nullptr, 1},
    {"voltage_ac", "AC Voltage", "V", "voltage", "measurement", nullptr, nullptr, 1},
    {"current_ac", "AC Current", "A", "current", "measurement", nullptr, nullptr, 1},
    {"freq_ac", "AC Frequency", "Hz", "frequency", "measurement", nullptr, nullptr, 2},
    {"power_ac", "AC Power", "W", "power", "measurement", nullptr, nullptr, 0},
    {"temperature", "Inverter Temperature", "°C", "temperature", "measurement", nullptr,
     nullptr, 1},
    {"kwh_total", "Total Generated Energy", "kWh", "energy", "total_increasing", nullptr,
     nullptr, 1},
    {"hours_total", "Operating Hours", "h", "duration", "total_increasing", nullptr, nullptr, 0},
    {"kwh_daily", "Daily Generated Energy", "kWh", "energy", "total_increasing", nullptr,
     nullptr, 1},
    {"work_mode", "Work Mode", nullptr, nullptr, nullptr, nullptr, "mdi:solar-power", -1},
    {"errors", "Errors", nullptr, nullptr, nullptr, nullptr, "mdi:alert-circle-outline", -1},
    {"error_code", "Error Code", nullptr, nullptr, nullptr, "diagnostic", nullptr, -1},
    {"voltage_bus", "DC Bus Voltage", "V", "voltage", "measurement", "diagnostic", nullptr, 1},
};

// Read once after registration, so no state_class (no statistics) and no expire_after.
const Sensor kDetailSensors[] = {
    {"firmware", "Firmware", nullptr, nullptr, nullptr, "diagnostic", "mdi:chip", -1},
    {"safety_country", "Safety Country", nullptr, nullptr, nullptr, "diagnostic", "mdi:earth", -1},
    {"pv_start_voltage", "PV Start Voltage", "V", "voltage", nullptr, "diagnostic", nullptr, 1},
    {"start_delay", "Start Delay", "s", "duration", nullptr, "diagnostic", nullptr, 0},
    {"vac_min", "Grid Voltage Min", "V", "voltage", nullptr, "diagnostic", nullptr, 1},
    {"vac_max", "Grid Voltage Max", "V", "voltage", nullptr, "diagnostic", nullptr, 1},
    {"fac_min", "Grid Frequency Min", "Hz", "frequency", nullptr, "diagnostic", nullptr, 2},
    {"fac_max", "Grid Frequency Max", "Hz", "frequency", nullptr, "diagnostic", nullptr, 2},
};

constexpr int kExpireAfterS = 600;

// From the goodwe Python library (marcelblijleven/goodwe, MIT, const.py SAFETY_COUNTRIES).
// 0-13 match the order of EzExplorer 1.0.1.8's country list; the GW3600-DS reports 20.
struct Country {
    uint8_t index;
    const char* name;
};
const Country kCountries[] = {
    {0, "IT CEI 0-21"}, {1, "CZ-A1"}, {2, "DE LV with PV"}, {3, "ES-A"}, {4, "GR"}, {5, "DK2"},
    {6, "BE"}, {7, "RO-A"}, {8, "GB G98"}, {9, "Australia A"}, {10, "FR mainland"},
    {11, "China"}, {12, "60Hz 230Vac Default"}, {13, "PL LV"}, {14, "South Africa"},
    {16, "Brazil 220Vac"}, {17, "Thailand MEA"}, {18, "Thailand PEA"}, {19, "Mauritius"},
    {20, "NL-A"}, {21, "G98/NI"}, {22, "China Higher"}, {23, "FR island 50Hz"},
    {24, "FR island 60Hz"}, {25, "Australia Ergon"}, {26, "Australia Energex"},
    {27, "NL 16/20A"}, {28, "Korea"}, {29, "China Utility"}, {30, "AT-A"}, {31, "India"},
    {32, "50Hz 230Vac Default"}, {33, "Warehouse"}, {34, "Philippines"}, {35, "IE-16/25A"},
    {36, "Taiwan"}, {37, "BG"}, {38, "Barbados"}, {39, "China Highest"}, {40, "GB G99-A"},
    {41, "SE LV"}, {42, "Chile BT"}, {43, "Brazil 127Vac"}, {44, "Newzealand"},
    {45, "IEEE1547 208Vac"}, {46, "IEEE1547 220Vac"}, {47, "IEEE1547 240Vac"},
    {48, "60Hz 127Vac Default"}, {49, "50Hz 127Vac Default"}, {50, "Australia WAPN"},
    {51, "Australia MicroGrid"}, {52, "JP 50Hz"}, {53, "JP 60Hz"}, {54, "India Higher"},
    {55, "DEWA LV"}, {56, "DEWA MV"}, {57, "SK"}, {58, "NZ GreenGrid"}, {59, "HU"},
    {60, "Sri Lanka"}, {61, "ES island"}, {62, "Ergon30K"}, {63, "Energex30K"},
    {64, "IEEE1547 230/400Vac"}, {65, "IEC61727 60Hz"}, {66, "CH"}, {67, "IT CEI 0-16"},
    {68, "Australia Horizon"}, {69, "CY"}, {70, "Australia SAPN"}, {71, "Australia Ausgrid"},
    {72, "Australia Essential"}, {73, "Australia Victoria"}, {74, "Hong Kong"}, {75, "PL MV"},
    {76, "NL-B"}, {77, "SE MV"}, {78, "DE MV"}, {79, "DE LV without PV"}, {80, "ES-D"},
    {81, "Australia Endeavour"}, {82, "Argentina"}, {83, "Israel LV"}, {84, "IEC61727 50Hz"},
    {85, "Australia B"}, {86, "Australia C"}, {87, "Chile MT-A"}, {88, "Chile MT-B"},
    {89, "Vietnam"}, {90, "reserve14"}, {91, "Israel-HV"}, {93, "NewZealand:2015"},
    {94, "RO-D"}, {96, "US 208Vac Default"}, {97, "US 240Vac Default"}, {98, "US CA 208Vac"},
    {99, "US CA 240Vac"}, {100, "cUSA_208VacCA_SDGE"}, {101, "cUSA_240VacCA_SDGE"},
    {102, "cUSA_208VacCA_PGE"}, {103, "cUSA_240VacCA_PGE"}, {104, "US HI 208Vac"},
    {105, "US HI 240Vac"}, {106, "USA_208VacHECO_14HM"}, {107, "USA_240VacHECO_14HM"},
    {108, "US 480Vac"}, {109, "US CA 480Vac"}, {110, "US HI 480Vac"}, {111, "US Kauai 208Vac"},
    {112, "US Kauai 240Vac"}, {113, "US Kauai 480Vac"}, {114, "US ISO-NE 208Vac"},
    {115, "US ISO-NE 240Vac"}, {116, "US ISO-NE 480Vac"}, {118, "PR 208Vac"},
    {119, "PR 240Vac"}, {120, "PR 480Vac"}, {128, "Poland_B"}, {129, "EE"}, {135, "CZ-A2"},
    {136, "CZ-B1"}, {146, "Brazil 208Vac"}, {147, "Brazil 230Vac"}, {148, "Brazil 240Vac"},
    {149, "Brazil 254Vac"},
};

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

// Keeps inverter-supplied text safe inside a JSON string.
std::string json_safe(const char* s) {
    std::string out;
    for (; *s; s++)
        if (*s >= 0x20 && *s < 0x7f && *s != '"' && *s != '\\') out += *s;
    return out;
}

std::string device_json(const char* serial, const char* sw_version) {
    // The proxy derives the model from the serial: 1 3600 DS ... -> GW-3600-DS.
    std::string s(serial);
    std::string model = s.size() >= 7 ? "GW-" + s.substr(1, 4) + "-" + s.substr(5, 2) : "GW";
    std::string json = format("{\"identifiers\":[\"%s\"],\"name\":\"GoodWe Inverter\","
                              "\"manufacturer\":\"GoodWe\",\"model\":\"%s\"",
                              serial, model.c_str());
    if (sw_version && *sw_version)
        json += format(",\"sw_version\":\"%s\"", json_safe(sw_version).c_str());
    return json + "}";
}

std::string sensor_config(const Sensor& s, const char* serial, const std::string& state,
                          const std::string& device, bool expire) {
    std::string json =
        format("{\"name\":\"%s\",\"state_topic\":\"%s\",", s.name, state.c_str()) +
        format("\"unique_id\":\"%s_%s\",\"value_template\":\"{{ value_json.%s }}\"", s.tag,
               serial, s.tag);
    json += ",\"device\":" + device;
    add_field(json, "unit_of_measurement", s.uom);
    add_field(json, "device_class", s.device_class);
    add_field(json, "state_class", s.state_class);
    add_field(json, "entity_category", s.entity_category);
    add_field(json, "icon", s.icon);
    if (s.precision >= 0) json += format(",\"suggested_display_precision\":%d", s.precision);
    if (expire) json += format(",\"expire_after\":%d", kExpireAfterS);
    return json + "}";
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

std::string safety_country_name(uint8_t index) {
    for (const Country& c : kCountries)
        if (c.index == index) return c.name;
    return format("Unknown (%u)", index);
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

std::string details_topic(const Config& c, const char* serial) {
    return node(c, "sensor", serial) + "/details";
}

std::string details_json(const aa55::IdInfo* id, const aa55::Settings* st) {
    std::string json;
    if (id)
        json += format(",\"firmware\":\"%s\",\"safety_country\":\"%s\"",
                       json_safe(id->firmware).c_str(),
                       safety_country_name(id->safety_country).c_str());
    if (st)
        json += format(",\"pv_start_voltage\":%.1f,\"start_delay\":%u,\"vac_min\":%.1f,"
                       "\"vac_max\":%.1f,\"fac_min\":%.2f,\"fac_max\":%.2f",
                       st->pv_start_voltage, st->start_delay_s, st->vac_min, st->vac_max,
                       st->fac_min, st->fac_max);
    if (json.empty()) return "{}";
    json[0] = '{';
    return json + "}";
}

std::vector<Message> discovery(const Config& c, const char* serial, const char* sw_version) {
    std::vector<Message> out;
    const std::string device = device_json(serial, sw_version);
    const std::string state = state_topic(c, serial);
    for (const Sensor& s : kSensors) {
        // Totals keep their last value overnight; everything else is real-time.
        bool expire = !s.state_class || strncmp(s.state_class, "total", 5) != 0;
        out.push_back({node(c, "sensor", serial) + "/" + s.tag + "/config",
                       sensor_config(s, serial, state, device, expire), true});
    }
    const std::string details = details_topic(c, serial);
    for (const Sensor& s : kDetailSensors)
        out.push_back({node(c, "sensor", serial) + "/" + s.tag + "/config",
                       sensor_config(s, serial, details, device, false), true});

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
