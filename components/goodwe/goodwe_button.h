#pragma once

#include "esphome/components/button/button.h"

#include "goodwe.h"

namespace esphome::goodwe {

class GoodWeButton : public button::Button, public Parented<GoodWe> {
public:
    explicit GoodWeButton(uint8_t func) : func_(func) {}

protected:
    void press_action() override { parent_->press(func_); }

    uint8_t func_;  // aa55::kStop or kStart
};

}  // namespace esphome::goodwe
