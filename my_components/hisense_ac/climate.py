import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import climate, switch, uart
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import HisenseAC, hisense_ac_ns

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["switch"]

CONF_FLOW_CONTROL_PIN = "flow_control_pin"
CONF_DISPLAY = "display"
CONF_BEEP = "beep"

HisenseDisplaySwitch = hisense_ac_ns.class_("HisenseDisplaySwitch", switch.Switch)
HisenseBeepSwitch = hisense_ac_ns.class_("HisenseBeepSwitch", switch.Switch)

CONFIG_SCHEMA = (
    climate.climate_schema(HisenseAC)
    .extend(
        {
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_DISPLAY): switch.switch_schema(
                HisenseDisplaySwitch,
                icon="mdi:numeric",
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="DISABLED",
            ),
            cv.Optional(CONF_BEEP): switch.switch_schema(
                HisenseBeepSwitch,
                icon="mdi:volume-high",
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="RESTORE_DEFAULT_ON",
            ),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "hisense_ac", baud_rate=9600, require_rx=True, require_tx=True
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_FLOW_CONTROL_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
        cg.add(var.set_flow_control_pin(pin))

    if CONF_DISPLAY in config:
        sw = await switch.new_switch(config[CONF_DISPLAY])
        await cg.register_parented(sw, var)
        cg.add(var.set_display_switch(sw))

    if CONF_BEEP in config:
        sw = await switch.new_switch(config[CONF_BEEP])
        await cg.register_parented(sw, var)
        cg.add(var.set_beep_switch(sw))
