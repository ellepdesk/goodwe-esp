import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_GOODWE_ID, HUB_SCHEMA, goodwe_ns

TextId = goodwe_ns.enum("TextId", is_class=True)


def _diagnostic(icon):
    return text_sensor.text_sensor_schema(
        icon=icon, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    )


TEXT_SENSORS = {
    "work_mode": text_sensor.text_sensor_schema(icon="mdi:solar-power"),
    "errors": text_sensor.text_sensor_schema(icon="mdi:alert-circle-outline"),
    "firmware": _diagnostic("mdi:chip"),
    "model": _diagnostic("mdi:solar-panel"),
    "serial": _diagnostic("mdi:identifier"),
    "safety_country": _diagnostic("mdi:earth"),
}

CONFIG_SCHEMA = HUB_SCHEMA.extend({cv.Optional(k): s for k, s in TEXT_SENSORS.items()})


async def to_code(config):
    hub = await cg.get_variable(config[CONF_GOODWE_ID])
    for key in TEXT_SENSORS:
        if conf := config.get(key):
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(hub.set_text_sensor(getattr(TextId, key.upper()), sens))
