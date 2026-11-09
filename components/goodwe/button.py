import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import CONF_GOODWE_ID, HUB_SCHEMA, goodwe_ns

GoodWeButton = goodwe_ns.class_("GoodWeButton", button.Button)

# Execute functions 03/1C and 03/1B. Buttons, not a switch: a stopped inverter reports plain
# Wait, so there is no state to show.
BUTTONS = {"stop": 0x1C, "start": 0x1B}

CONFIG_SCHEMA = HUB_SCHEMA.extend(
    {
        cv.Optional(key): button.button_schema(
            GoodWeButton,
            icon="mdi:stop" if key == "stop" else "mdi:play",
            entity_category=ENTITY_CATEGORY_CONFIG,
        )
        for key in BUTTONS
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_GOODWE_ID])
    for key, func in BUTTONS.items():
        if conf := config.get(key):
            btn = await button.new_button(conf, func)
            await cg.register_parented(btn, hub)
