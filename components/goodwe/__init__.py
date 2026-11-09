"""GoodWe inverter on its USB HID port (AA55 protocol), for an ESP32-S2/S3 as USB host."""

import esphome.codegen as cg
from esphome.components import esp32
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS, CONF_ID

CODEOWNERS = ["@ellepdesk"]
AUTO_LOAD = ["sensor", "text_sensor"]
CONF_GOODWE_ID = "goodwe_id"
CONF_POLL_INTERVAL = "poll_interval"

goodwe_ns = cg.esphome_ns.namespace("goodwe")
GoodWe = goodwe_ns.class_("GoodWe", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(GoodWe),
            cv.Optional(CONF_ADDRESS, default=0x11): cv.int_range(min=1, max=0x7E),
            cv.Optional(
                CONF_POLL_INTERVAL, default="10s"
            ): cv.positive_time_period_milliseconds,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    esp32.only_on_variant(
        supported=[esp32.VARIANT_ESP32S2, esp32.VARIANT_ESP32S3, esp32.VARIANT_ESP32P4]
    ),
)

HUB_SCHEMA = cv.Schema({cv.GenerateID(CONF_GOODWE_ID): cv.use_id(GoodWe)})


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
