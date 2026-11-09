#pragma once

#include <memory>

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif

#include "aa55.h"
#include "ha.h"
#include "master.h"
#include "usb_hid.h"

// The firmware in src/usb as an ESPHome component: same USB host transport and AA55 master,
// but entities through ESPHome instead of MQTT discovery.

namespace esphome::goodwe {

enum class SensorId : uint8_t {
    PV1_VOLTAGE,
    PV2_VOLTAGE,
    PV1_CURRENT,
    PV2_CURRENT,
    AC_VOLTAGE,
    AC_CURRENT,
    AC_FREQUENCY,
    AC_POWER,
    TEMPERATURE,
    BUS_VOLTAGE,
    ENERGY_TOTAL,
    ENERGY_DAILY,
    HOURS_TOTAL,
    ERROR_CODE,
    PV_START_VOLTAGE,
    START_DELAY,
    VAC_MIN,
    VAC_MAX,
    FAC_MIN,
    FAC_MAX,
    COUNT,
};

enum class TextId : uint8_t { WORK_MODE, ERRORS, FIRMWARE, MODEL, SERIAL, SAFETY_COUNTRY, COUNT };

class GoodWe : public Component {
public:
    void setup() override;
    void loop() override;
    void dump_config() override;
    float get_setup_priority() const override { return setup_priority::DATA; }

    void set_address(uint8_t address) { address_ = address; }
    void set_poll_interval(uint32_t ms) { poll_ms_ = ms; }
    void set_sensor(SensorId id, sensor::Sensor* s) { sensors_[size_t(id)] = s; }
    void set_text_sensor(TextId id, text_sensor::TextSensor* s) { texts_[size_t(id)] = s; }
#ifdef USE_NUMBER
    void set_power_limit_number(number::Number* n) { power_limit_number_ = n; }
    void set_power_limit_watts_number(number::Number* n, int rated) {
        power_limit_watts_number_ = n;
        rated_power_ = rated;
    }
#endif

    // Kept in flash: the inverter can't report its limit and may forget it overnight, so it
    // is sent again after every registration.
    void set_power_limit(int pct);
    void set_power_limit_watts(float watts);
    void press(uint8_t func) { button_ = func; }  // aa55::kStop or kStart

protected:
    // Live values go unknown this long after the last running info, like expire_after in the
    // MQTT version; the totals keep their value through the night.
    static constexpr uint32_t kStaleMs = 600000;
    // A hung SET_REPORT can't be cancelled; see src/usb/main.cpp.
    static constexpr uint32_t kWriteFailRebootMs = 60000;
    static constexpr uint32_t kMinUptimeMs = 600000;

    void open_port();
    void send(const uint8_t* frame, size_t len);
    void on_info(const aa55::RunningInfo& info);
    void on_details(const aa55::IdInfo* id, const aa55::Settings* st);
    void on_execute(uint8_t func, bool ack);
    void publish(SensorId id, float value);
    void publish(TextId id, const std::string& value);
    void expire();

    uint8_t address_ = 0x11;
    uint32_t poll_ms_ = 10000;
    UsbHid usb_;
    std::unique_ptr<aa55::Master> master_;
    ::ha::PowerLimit limit_;
    ESPPreferenceObject limit_pref_;
    uint8_t button_ = 0;
    uint32_t last_info_ = 0;
    bool live_ = false;  // live sensors hold values
    uint32_t write_failing_since_ = 0;

    sensor::Sensor* sensors_[size_t(SensorId::COUNT)] = {};
    text_sensor::TextSensor* texts_[size_t(TextId::COUNT)] = {};
#ifdef USE_NUMBER
    number::Number* power_limit_number_ = nullptr;
    number::Number* power_limit_watts_number_ = nullptr;
#endif
    int rated_power_ = 0;  // W, for the watts number
};

}  // namespace esphome::goodwe
