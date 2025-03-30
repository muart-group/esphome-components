from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_OUTDOOR_TEMPERATURE,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_KILOWATT_HOURS,
    UNIT_PERCENT,
    UNIT_WATT
)
from esphome.core import coroutine

from ...mitsubishi_itp import (
    mitsubishi_itp_ns,
    sensors_to_code,
    sensors_to_config_schema,
)

CONF_THERMOSTAT_HUMIDITY = "thermostat_humidity"
CONF_THERMOSTAT_TEMPERATURE = "thermostat_temperature"
CONF_INPUT_WATTS = "input_watts"
CONF_LIFETIME_KWH = "lifetime_kwh"

CompressorFrequencySensor = mitsubishi_itp_ns.class_(
    "CompressorFrequencySensor", sensor.Sensor
)
InputWattsSensor = mitsubishi_itp_ns.class_(
    "InputWattsSensor", sensor.Sensor
)
LifetimeKwhSensor = mitsubishi_itp_ns.class_(
    "LifetimeKwhSensor", sensor.Sensor
)
OutdoorTemperatureSensor = mitsubishi_itp_ns.class_(
    "OutdoorTemperatureSensor", sensor.Sensor
)
ThermostatHumiditySensor = mitsubishi_itp_ns.class_(
    "ThermostatHumiditySensor", sensor.Sensor
)
ThermostatTemperatureSensor = mitsubishi_itp_ns.class_(
    "ThermostatTemperatureSensor", sensor.Sensor
)

# TODO Storing the registration function here seems weird, but I can't figure out how to determine schema type later
SENSORS = dict[str, cv.Schema](
    {
        "compressor_frequency": sensor.sensor_schema(
            CompressorFrequencySensor,
            unit_of_measurement=UNIT_HERTZ,
            device_class=DEVICE_CLASS_FREQUENCY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        CONF_INPUT_WATTS: sensor.sensor_schema(
            InputWattsSensor,
            unit_of_measurement=UNIT_WATT,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        CONF_LIFETIME_KWH: sensor.sensor_schema(
            LifetimeKwhSensor,
            unit_of_measurement=UNIT_KILOWATT_HOURS,
            device_class=DEVICE_CLASS_ENERGY,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            accuracy_decimals=1,
        ),
        CONF_OUTDOOR_TEMPERATURE: sensor.sensor_schema(
            OutdoorTemperatureSensor,
            unit_of_measurement=UNIT_CELSIUS,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
            icon="mdi:sun-thermometer-outline",
        ),
        CONF_THERMOSTAT_HUMIDITY: sensor.sensor_schema(
            ThermostatHumiditySensor,
            unit_of_measurement=UNIT_PERCENT,
            device_class=DEVICE_CLASS_HUMIDITY,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
        ),
        CONF_THERMOSTAT_TEMPERATURE: sensor.sensor_schema(
            ThermostatTemperatureSensor,
            unit_of_measurement=UNIT_CELSIUS,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),
    }
)

CONFIG_SCHEMA = sensors_to_config_schema(SENSORS)


@coroutine
async def to_code(config):
    await sensors_to_code(config, SENSORS, sensor.register_sensor)
