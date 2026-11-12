"""z1_screen: draws the z1 component's view of the machine on any ESPHome display.

The layout comes from the display's own width and height and the three fonts
(components/z1/z1_screen.h), so one config serves every size of screen; the
presets in screens/ pick the display and font sizes. Made for e-paper: it
redraws only when what it shows has changed (see Refresher).
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, font
from esphome.const import CONF_DISPLAY_ID, CONF_ID

DEPENDENCIES = ["z1", "display", "font"]

CONF_Z1_ID = "z1_id"
CONF_FONT_SMALL = "font_small"
CONF_FONT_MEDIUM = "font_medium"
CONF_FONT_BIG = "font_big"
CONF_SETTLE = "settle"
CONF_CALM_REFRESH = "calm_refresh"
CONF_MINUTE_STEP = "minute_step"

z1_ns = cg.esphome_ns.namespace("z1")
Z1Component = z1_ns.class_("Z1Component", cg.Component)
z1_screen_ns = cg.esphome_ns.namespace("z1_screen")
Z1Screen = z1_screen_ns.class_("Z1Screen", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Z1Screen),
        cv.GenerateID(CONF_Z1_ID): cv.use_id(Z1Component),
        # Give the display `update_interval: never`; this component asks for each redraw.
        cv.Required(CONF_DISPLAY_ID): cv.use_id(display.Display),
        cv.Required(CONF_FONT_SMALL): cv.use_id(font.Font),
        cv.Required(CONF_FONT_MEDIUM): cv.use_id(font.Font),
        cv.Required(CONF_FONT_BIG): cv.use_id(font.Font),
        # A change that matters (mode, bit, alarm) is drawn once it has held this long.
        cv.Optional(CONF_SETTLE, default="1500ms"): cv.positive_time_period_milliseconds,
        # Anything else (the job clock) at most this often.
        cv.Optional(CONF_CALM_REFRESH, default="5min"): cv.positive_time_period_milliseconds,
        # The job clock moves in steps of this many minutes.
        cv.Optional(CONF_MINUTE_STEP, default=5): cv.int_range(min=1, max=60),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_z1(await cg.get_variable(config[CONF_Z1_ID])))
    cg.add(
        var.set_fonts(
            await cg.get_variable(config[CONF_FONT_SMALL]),
            await cg.get_variable(config[CONF_FONT_MEDIUM]),
            await cg.get_variable(config[CONF_FONT_BIG]),
        )
    )
    cg.add(
        var.set_refresh(
            config[CONF_SETTLE].total_milliseconds,
            config[CONF_CALM_REFRESH].total_milliseconds,
            config[CONF_MINUTE_STEP],
        )
    )
    # Last, so the writer is in place before the display draws for the first time.
    cg.add(var.set_display(await cg.get_variable(config[CONF_DISPLAY_ID])))
