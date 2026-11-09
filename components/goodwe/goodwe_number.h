#pragma once

#include "esphome/components/number/number.h"

#include "goodwe.h"

namespace esphome::goodwe {

// The states show the limit last acknowledged by the inverter, not the one asked for.
class PowerLimitNumber : public number::Number, public Parented<GoodWe> {
protected:
    void control(float value) override { parent_->set_power_limit(int(value)); }
};

class PowerLimitWattsNumber : public number::Number, public Parented<GoodWe> {
protected:
    void control(float value) override { parent_->set_power_limit_watts(value); }
};

}  // namespace esphome::goodwe
