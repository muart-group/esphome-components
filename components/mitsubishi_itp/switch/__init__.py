import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID
from esphome.core import coroutine

from ...mitsubishi_itp import CONF_MITSUBISHI_ITP_ID, MitsubishiUART, mitsubishi_itp_ns

MITPZoneSwitch = mitsubishi_itp_ns.class_("MITPZoneSwitch", switch.Switch)

ZONE_SWITCH_SCHEMA = switch.switch_schema(
    MITPZoneSwitch,
    icon="mdi:hvac",
)

ZONES = {f"zone_{i}": ZONE_SWITCH_SCHEMA for i in range(1, 9)}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MITSUBISHI_ITP_ID): cv.use_id(MitsubishiUART),
    }
).extend(
    {cv.Optional(zone_key): ZONE_SWITCH_SCHEMA for zone_key in ZONES}
)


@coroutine
async def to_code(config):
    mitp_component = await cg.get_variable(config[CONF_MITSUBISHI_ITP_ID])

    zones_registered = False

    for zone_key in ZONES:
        if zone_conf := config.get(zone_key):
            zone_component = cg.new_Pvariable(zone_conf[CONF_ID])

            # Derive 0-based zone index from key name (zone_1 -> 0, zone_2 -> 1, ...)
            zone_number = int(zone_key.split("_")[1]) - 1
            cg.add(getattr(zone_component, "set_zone_number")(zone_number))

            await cg.register_parented(zone_component, mitp_component)
            await switch.register_switch(zone_component, zone_conf)

            # TODO: Unclear if MITPListener just for publish() is really needed
            cg.add(getattr(mitp_component, "register_listener")(zone_component))
            zones_registered = True

    if zones_registered:
        cg.add(getattr(mitp_component, "set_zones_enabled")(True))
