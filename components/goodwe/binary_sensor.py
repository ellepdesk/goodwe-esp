import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_CONNECTIVITY, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_GOODWE_ID, HUB_SCHEMA

CONF_REGISTERED = "registered"

CONFIG_SCHEMA = HUB_SCHEMA.extend(
    {
        # On while the inverter answers polls; off at night, unplugged, or while it
        # re-registers.
        cv.Optional(CONF_REGISTERED): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_GOODWE_ID])
    if conf := config.get(CONF_REGISTERED):
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(hub.set_registered_binary_sensor(sens))
