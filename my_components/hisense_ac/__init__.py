import esphome.codegen as cg
from esphome.components import climate, uart

CODEOWNERS = ["@taqee"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["climate", "switch"]

hisense_ac_ns = cg.esphome_ns.namespace("hisense_ac")
HisenseAC = hisense_ac_ns.class_(
    "HisenseAC", uart.UARTDevice, cg.Component, climate.Climate
)
