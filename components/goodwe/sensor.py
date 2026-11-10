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


# Explicit icons: HA would derive them from the device class, but ESPHome's web page doesn't.
def _live(unit, device_class, decimals, icon, **kw):
    return sensor.sensor_schema(
        unit_of_measurement=unit,
        icon=icon,
        device_class=device_class,
        accuracy_decimals=decimals,
        state_class=STATE_CLASS_MEASUREMENT,
        **kw,
    )


def _setting(unit, device_class, decimals, icon):
    return sensor.sensor_schema(
        unit_of_measurement=unit,
        icon=icon,
        device_class=device_class,
        accuracy_decimals=decimals,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


SENSORS = {
    "pv1_voltage": _live(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1, "mdi:solar-panel"),
    "pv2_voltage": _live(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1, "mdi:solar-panel"),
    "pv1_current": _live(UNIT_AMPERE, DEVICE_CLASS_CURRENT, 1, "mdi:current-dc"),
    "pv2_current": _live(UNIT_AMPERE, DEVICE_CLASS_CURRENT, 1, "mdi:current-dc"),
    "ac_voltage": _live(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1, "mdi:sine-wave"),
    "ac_current": _live(UNIT_AMPERE, DEVICE_CLASS_CURRENT, 1, "mdi:current-ac"),
    "ac_frequency": _live(UNIT_HERTZ, DEVICE_CLASS_FREQUENCY, 2, "mdi:waveform"),
    "ac_power": _live(UNIT_WATT, DEVICE_CLASS_POWER, 0, "mdi:flash"),
    "temperature": _live(UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, 1, "mdi:thermometer"),
    # The DC bus, see docs/protocol.md.
    "bus_voltage": _live(
        UNIT_VOLT,
        DEVICE_CLASS_VOLTAGE,
        1,
        "mdi:current-dc",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    "energy_total": sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOWATT_HOURS,
        icon="mdi:solar-power-variant",
        device_class=DEVICE_CLASS_ENERGY,
        accuracy_decimals=1,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    ),
    "energy_daily": sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOWATT_HOURS,
        icon="mdi:solar-power-variant",
        device_class=DEVICE_CLASS_ENERGY,
        accuracy_decimals=1,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    ),
    "hours_total": sensor.sensor_schema(
        unit_of_measurement=UNIT_HOUR,
        icon="mdi:timer-outline",
        device_class=DEVICE_CLASS_DURATION,
        accuracy_decimals=0,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    ),
    "error_code": sensor.sensor_schema(
        accuracy_decimals=0,
        icon="mdi:alert-circle-outline",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    "pv_start_voltage": _setting(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1, "mdi:solar-panel"),
    "start_delay": _setting(UNIT_SECOND, DEVICE_CLASS_DURATION, 0, "mdi:timer-sand"),
    "vac_min": _setting(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1, "mdi:sine-wave"),
    "vac_max": _setting(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1, "mdi:sine-wave"),
    "fac_min": _setting(UNIT_HERTZ, DEVICE_CLASS_FREQUENCY, 2, "mdi:waveform"),
    "fac_max": _setting(UNIT_HERTZ, DEVICE_CLASS_FREQUENCY, 2, "mdi:waveform"),
}

CONFIG_SCHEMA = HUB_SCHEMA.extend({cv.Optional(k): s for k, s in SENSORS.items()})


async def to_code(config):
    hub = await cg.get_variable(config[CONF_GOODWE_ID])
    for key in SENSORS:
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(hub.set_sensor(getattr(SensorId, key.upper()), sens))
