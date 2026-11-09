import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_HOUR,
    UNIT_KILOWATT_HOURS,
    UNIT_SECOND,
    UNIT_VOLT,
    UNIT_WATT,
)

from . import CONF_GOODWE_ID, HUB_SCHEMA, goodwe_ns

SensorId = goodwe_ns.enum("SensorId", is_class=True)


def _live(unit, device_class, decimals, **kw):
    return sensor.sensor_schema(
        unit_of_measurement=unit,
        device_class=device_class,
        accuracy_decimals=decimals,
        state_class=STATE_CLASS_MEASUREMENT,
        **kw,
    )


def _setting(unit, device_class, decimals):
    return sensor.sensor_schema(
        unit_of_measurement=unit,
        device_class=device_class,
        accuracy_decimals=decimals,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


SENSORS = {
    "pv1_voltage": _live(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1),
    "pv2_voltage": _live(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1),
    "pv1_current": _live(UNIT_AMPERE, DEVICE_CLASS_CURRENT, 1),
    "pv2_current": _live(UNIT_AMPERE, DEVICE_CLASS_CURRENT, 1),
    "ac_voltage": _live(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1),
    "ac_current": _live(UNIT_AMPERE, DEVICE_CLASS_CURRENT, 1),
    "ac_frequency": _live(UNIT_HERTZ, DEVICE_CLASS_FREQUENCY, 2),
    "ac_power": _live(UNIT_WATT, DEVICE_CLASS_POWER, 0),
    "temperature": _live(UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, 1),
    # The DC bus, see docs/protocol.md.
    "bus_voltage": _live(
        UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    ),
    "energy_total": sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOWATT_HOURS,
        device_class=DEVICE_CLASS_ENERGY,
        accuracy_decimals=1,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    ),
    "energy_daily": sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOWATT_HOURS,
        device_class=DEVICE_CLASS_ENERGY,
        accuracy_decimals=1,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    ),
    "hours_total": sensor.sensor_schema(
        unit_of_measurement=UNIT_HOUR,
        device_class=DEVICE_CLASS_DURATION,
        accuracy_decimals=0,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    ),
    "error_code": sensor.sensor_schema(
        accuracy_decimals=0,
        icon="mdi:alert-circle-outline",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    "pv_start_voltage": _setting(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1),
    "start_delay": _setting(UNIT_SECOND, DEVICE_CLASS_DURATION, 0),
    "vac_min": _setting(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1),
    "vac_max": _setting(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1),
    "fac_min": _setting(UNIT_HERTZ, DEVICE_CLASS_FREQUENCY, 2),
    "fac_max": _setting(UNIT_HERTZ, DEVICE_CLASS_FREQUENCY, 2),
}

CONFIG_SCHEMA = HUB_SCHEMA.extend({cv.Optional(k): s for k, s in SENSORS.items()})


async def to_code(config):
    hub = await cg.get_variable(config[CONF_GOODWE_ID])
    for key in SENSORS:
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(hub.set_sensor(getattr(SensorId, key.upper()), sens))
