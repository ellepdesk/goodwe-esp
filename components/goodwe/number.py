import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_POWER,
    ENTITY_CATEGORY_CONFIG,
    UNIT_PERCENT,
    UNIT_WATT,
)

from . import CONF_GOODWE_ID, HUB_SCHEMA, GoodWe, goodwe_ns

PowerLimitNumber = goodwe_ns.class_(
    "PowerLimitNumber", number.Number, cg.Parented.template(GoodWe)
)
PowerLimitWattsNumber = goodwe_ns.class_(
    "PowerLimitWattsNumber", number.Number, cg.Parented.template(GoodWe)
)

CONF_POWER_LIMIT = "power_limit"
CONF_POWER_LIMIT_WATTS = "power_limit_watts"
# Fixed in the config, not taken from the serial at runtime: HA reads a number's range once.
CONF_RATED_POWER = "rated_power"

CONFIG_SCHEMA = HUB_SCHEMA.extend(
    {
        cv.Optional(CONF_POWER_LIMIT): number.number_schema(
            PowerLimitNumber,
            unit_of_measurement=UNIT_PERCENT,
            icon="mdi:solar-power-variant",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Optional(CONF_POWER_LIMIT_WATTS): number.number_schema(
            PowerLimitWattsNumber,
            unit_of_measurement=UNIT_WATT,
            device_class=DEVICE_CLASS_POWER,
            icon="mdi:solar-power-variant",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ).extend({cv.Required(CONF_RATED_POWER): cv.int_range(min=100, max=100000)}),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_GOODWE_ID])
    if conf := config.get(CONF_POWER_LIMIT):
        num = await number.new_number(conf, min_value=0, max_value=100, step=1)
        await cg.register_parented(num, hub)
        cg.add(hub.set_power_limit_number(num))
    if conf := config.get(CONF_POWER_LIMIT_WATTS):
        rated = conf[CONF_RATED_POWER]
        num = await number.new_number(conf, min_value=0, max_value=rated, step=1)
        await cg.register_parented(num, hub)
        cg.add(hub.set_power_limit_watts_number(num, rated))
