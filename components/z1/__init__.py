"""z1: reads a Makera Z1 over the LAN (UDP announcements + TCP status polls).

Read-only: the only frame it sends is the status query '?'. See docs/Z1-PROTOCOL.md.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    CONF_PORT,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_RUNNING,
    ENTITY_CATEGORY_DIAGNOSTIC,
    UNIT_MILLIMETER,
    UNIT_SECOND,
)

DEPENDENCIES = ["network"]
AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor"]

CONF_HOST = "host"
CONF_MACHINE_NAME = "machine_name"
CONF_STATE = "state"
CONF_HALT_REASON = "halt_reason"
CONF_MACHINE_IP = "machine_ip"
CONF_TOOL = "tool"
CONF_NEXT_TOOL = "next_tool"
CONF_TOOL_LENGTH = "tool_length"
CONF_JOB_TIME = "job_time"
CONF_ONLINE = "online"
CONF_TOOL_CHANGE = "tool_change"
CONF_JOB_ACTIVE = "job_active"
CONF_ALARM = "alarm"

z1_ns = cg.esphome_ns.namespace("z1")
Z1Component = z1_ns.class_("Z1Component", cg.Component)

TEXT_SENSORS = {
    CONF_STATE: text_sensor.text_sensor_schema(icon="mdi:state-machine"),
    CONF_HALT_REASON: text_sensor.text_sensor_schema(icon="mdi:alert-octagon"),
    CONF_MACHINE_IP: text_sensor.text_sensor_schema(
        icon="mdi:ip-network", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    ),
}
SENSORS = {
    CONF_TOOL: sensor.sensor_schema(icon="mdi:screwdriver", accuracy_decimals=0),
    CONF_NEXT_TOOL: sensor.sensor_schema(icon="mdi:swap-horizontal", accuracy_decimals=0),
    CONF_TOOL_LENGTH: sensor.sensor_schema(
        unit_of_measurement=UNIT_MILLIMETER,
        accuracy_decimals=3,
        icon="mdi:ruler",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_JOB_TIME: sensor.sensor_schema(
        unit_of_measurement=UNIT_SECOND, device_class=DEVICE_CLASS_DURATION, accuracy_decimals=0
    ),
}
BINARY_SENSORS = {
    CONF_ONLINE: binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_CONNECTIVITY, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    ),
    CONF_TOOL_CHANGE: binary_sensor.binary_sensor_schema(icon="mdi:hand-back-right"),
    CONF_JOB_ACTIVE: binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_RUNNING),
    CONF_ALARM: binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Z1Component),
        # Without host, the first Z1 that announces itself is used.
        cv.Optional(CONF_HOST): cv.ipv4address,
        cv.Optional(CONF_PORT, default=2222): cv.port,
        # Only follow this machine (e.g. Makera_Z1_000000) when there is more than one.
        cv.Optional(CONF_MACHINE_NAME): cv.string_strict,
        **{cv.Optional(k): v for k, v in TEXT_SENSORS.items()},
        **{cv.Optional(k): v for k, v in SENSORS.items()},
        **{cv.Optional(k): v for k, v in BINARY_SENSORS.items()},
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if CONF_HOST in config:
        cg.add(var.set_host(str(config[CONF_HOST]), config[CONF_PORT]))
    if CONF_MACHINE_NAME in config:
        cg.add(var.set_machine_name(config[CONF_MACHINE_NAME]))
    for key in TEXT_SENSORS:
        if key in config:
            ts = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(var, f"set_{key}_text_sensor")(ts))
    for key in SENSORS:
        if key in config:
            s = await sensor.new_sensor(config[key])
            cg.add(getattr(var, f"set_{key}_sensor")(s))
    for key in BINARY_SENSORS:
        if key in config:
            bs = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(var, f"set_{key}_binary_sensor")(bs))
