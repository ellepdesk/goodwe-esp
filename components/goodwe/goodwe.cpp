#include "goodwe.h"

#include <cmath>
#include <string>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::goodwe {

static const char* const TAG = "goodwe";

static constexpr SensorId kLive[] = {
    SensorId::PV1_VOLTAGE, SensorId::PV2_VOLTAGE, SensorId::PV1_CURRENT, SensorId::PV2_CURRENT,
    SensorId::AC_VOLTAGE,  SensorId::AC_CURRENT,  SensorId::AC_FREQUENCY, SensorId::AC_POWER,
    SensorId::TEMPERATURE, SensorId::BUS_VOLTAGE,
};

void GoodWe::setup() {
    if (!usb_.begin(0x0084, 0x0041)) {
        ESP_LOGE(TAG, "USB host install failed");
        mark_failed();
    }
    limit_pref_ = global_preferences->make_preference<int>(fnv1_hash("goodwe_power_limit"));
    int pct;
    if (limit_pref_.load(&pct)) limit_.command(std::to_string(pct).c_str());
}

void GoodWe::dump_config() {
    ESP_LOGCONFIG(TAG, "GoodWe AA55 over USB HID:\n  Address: 0x%02X\n  Poll interval: %u ms",
                  address_, unsigned(poll_ms_));
}

void GoodWe::open_port() {
    aa55::Master::Callbacks cb;
    cb.send = [this](const uint8_t* f, size_t n) { send(f, n); };
    cb.on_registered = [this](const char* s) {
        ESP_LOGI(TAG, "Registered inverter %s at address 0x%02X", s, address_);
        limit_.on_registered();
        publish(TextId::SERIAL, s);
    };
    cb.on_info = [this](const aa55::RunningInfo& info) { on_info(info); };
    cb.on_details = [this](const aa55::IdInfo* id, const aa55::Settings* st) { on_details(id, st); };
    cb.on_execute = [this](uint8_t func, bool ack) { on_execute(func, ack); };
    master_.reset(new aa55::Master(cb, address_, poll_ms_));
    ESP_LOGI(TAG, "Inverter port open");
}

void GoodWe::send(const uint8_t* f, size_t n) {
    if (usb_.write(f, n)) {
        write_failing_since_ = 0;
        return;
    }
    if (write_failing_since_) return;
    write_failing_since_ = millis() | 1;
    ESP_LOGW(TAG, "USB write failed: %s", usb_.write_error());
}

void GoodWe::loop() {
    usb_.poll();
    if (usb_.connected() && !master_) open_port();
    if (!usb_.connected() && master_) {
        master_.reset();
        button_ = 0;
        write_failing_since_ = 0;
        ESP_LOGI(TAG, "Inverter port closed");
    }
    uint32_t now = millis();
    if (live_ && now - last_info_ > kStaleMs) expire();
    if (!master_) return;

    if (write_failing_since_ && now - write_failing_since_ > kWriteFailRebootMs &&
        now > kMinUptimeMs) {
        ESP_LOGE(TAG, "USB writes failing for 60 s, rebooting");
        App.safe_reboot();
    }

    uint8_t buf[64];
    size_t n;
    while ((n = usb_.read(buf, sizeof buf)) > 0)
        for (size_t i = 0; i < n; i++) master_->feed(buf[i], now);

    bool registered = master_->state() == aa55::Master::State::kRunning;
    master_->tick(now);
    if (registered && master_->state() != aa55::Master::State::kRunning)
        ESP_LOGI(TAG, "Inverter stopped answering, registering again");
    if (button_ && !master_->exec_pending()) {
        ESP_LOGI(TAG, "%s requested", button_ == aa55::kStop ? "Stop" : "Start");
        master_->execute(button_, nullptr, 0);
        button_ = 0;
    }
    uint8_t pct;
    if (!master_->exec_pending() && limit_.due(pct))
        master_->execute(aa55::kRealPowerAdjust, &pct, 1);
}

void GoodWe::set_power_limit(int pct) {
    std::string payload = std::to_string(pct);
    if (!limit_.command(payload.c_str())) {
        ESP_LOGW(TAG, "Ignoring power limit %d%%", pct);
        return;
    }
    ESP_LOGI(TAG, "Power limit %d%% requested", pct);
    limit_pref_.save(&pct);
}

void GoodWe::set_power_limit_watts(float watts) {
    int pct;
    if (!::ha::watts_to_percent(std::to_string(int(watts)).c_str(), rated_power_, pct)) {
        ESP_LOGW(TAG, "Ignoring power limit %.0f W", watts);
        return;
    }
    set_power_limit(pct);
}

void GoodWe::on_info(const aa55::RunningInfo& info) {
    last_info_ = millis();
    live_ = true;
    publish(SensorId::PV1_VOLTAGE, info.vpv1);
    publish(SensorId::PV2_VOLTAGE, info.vpv2);
    publish(SensorId::PV1_CURRENT, info.ipv1);
    publish(SensorId::PV2_CURRENT, info.ipv2);
    publish(SensorId::AC_VOLTAGE, info.vac);
    publish(SensorId::AC_CURRENT, info.iac);
    publish(SensorId::AC_FREQUENCY, info.fac);
    publish(SensorId::AC_POWER, info.pac);
    publish(SensorId::TEMPERATURE, info.temperature);
    publish(SensorId::BUS_VOLTAGE, info.vbus);
    publish(SensorId::ENERGY_TOTAL, info.e_total);
    publish(SensorId::ENERGY_DAILY, info.e_day);
    publish(SensorId::HOURS_TOTAL, info.h_total);
    publish(SensorId::ERROR_CODE, info.error_code);
    const char* mode = ::ha::work_mode_name(info.work_mode);
    publish(TextId::WORK_MODE, mode ? mode : "Unknown (" + std::to_string(info.work_mode) + ")");
    publish(TextId::ERRORS, ::ha::error_names(info.error_code));
    limit_.on_info(info);
}

void GoodWe::on_details(const aa55::IdInfo* id, const aa55::Settings* st) {
    ESP_LOGI(TAG, "Firmware %s, settings %s", id ? id->firmware : "not read",
             st ? "read" : "not read");
    if (id) {
        publish(TextId::FIRMWARE, id->firmware);
        publish(TextId::MODEL, id->model);
        publish(TextId::SAFETY_COUNTRY, ::ha::safety_country_name(id->safety_country));
    }
    if (st) {
        publish(SensorId::PV_START_VOLTAGE, st->pv_start_voltage);
        publish(SensorId::START_DELAY, st->start_delay_s);
        publish(SensorId::VAC_MIN, st->vac_min);
        publish(SensorId::VAC_MAX, st->vac_max);
        publish(SensorId::FAC_MIN, st->fac_min);
        publish(SensorId::FAC_MAX, st->fac_max);
    }
}

void GoodWe::on_execute(uint8_t func, bool ack) {
    if (func != aa55::kRealPowerAdjust) {
        ESP_LOGI(TAG, "%s: %s", func == aa55::kStop ? "Stop" : "Start",
                 ack ? "ACK" : "NAK or no reply");
        return;
    }
    limit_.on_result(ack);
    if (!ack) {
        ESP_LOGW(TAG, "Power limit not acknowledged");
        return;
    }
    ESP_LOGI(TAG, "Power limit %d%% applied", limit_.applied());
#ifdef USE_NUMBER
    if (power_limit_number_) power_limit_number_->publish_state(limit_.applied());
    if (power_limit_watts_number_)
        power_limit_watts_number_->publish_state(limit_.applied() * rated_power_ / 100);
#endif
}

void GoodWe::expire() {
    live_ = false;
    for (SensorId id : kLive) publish(id, NAN);
}

void GoodWe::publish(SensorId id, float value) {
    sensor::Sensor* s = sensors_[size_t(id)];
    if (s) s->publish_state(value);
}

void GoodWe::publish(TextId id, const std::string& value) {
    text_sensor::TextSensor* s = texts_[size_t(id)];
    if (s && s->get_state() != value) s->publish_state(value);
}

}  // namespace esphome::goodwe
