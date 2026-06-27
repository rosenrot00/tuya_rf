import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome import pins
from esphome.components import remote_base
from esphome.const import (
    CONF_BUFFER_SIZE,
    CONF_DUMP,
    CONF_FILTER,
    CONF_ID,
    CONF_TOLERANCE,
    CONF_TYPE,
    CONF_VALUE,
)

CONF_RECEIVER_DISABLED = "receiver_disabled"
CONF_RX_PIN = "rx_pin"
CONF_TX_PIN = "tx_pin"
CONF_START_PULSE_MIN = "start_pulse_min"
CONF_START_PULSE_MAX = "start_pulse_max"
CONF_END_PULSE = "end_pulse"
CONF_MAX_PAUSE = "max_pause"
CONF_FRAME_GAP = "frame_gap"
CONF_MIN_PULSES = "min_pulses"
CONF_MAX_PULSES = "max_pulses"
CONF_MAX_FRAME_DURATION = "max_frame_duration"
CONF_SCLK_PIN = "sclk_pin"
CONF_MOSI_PIN = "mosi_pin"
CONF_CSB_PIN = "csb_pin"
CONF_FCSB_PIN = "fcsb_pin"
CONF_RECEIVER_ID = "receiver_id"
CONF_QUEUE_MAX_SIZE = "queue_max_size"
CONF_QUEUE_DELAY = "queue_delay"
CONF_DATA = "data"
CONF_SINGLE_RAW_DUMP = "single_raw_dump"
CONF_ACCEPT_ON_RESTART = "accept_on_restart"
CONF_DEDUPE_WINDOW = "dedupe_window"
CONF_DOUT_MUTE = "dout_mute"
CONF_FREQUENCY = "frequency"
CONF_RSSI_AVG_MODE = "rssi_avg_mode"
CONF_AGC_OOK_REGISTERS = "agc_ook_registers"
CONF_DUMP_SUPPRESSED_RAW = "dump_suppressed_raw"
CONF_MODULATION = "modulation"
CONF_FSK_PROFILE = "fsk_profile"
CONF_FSK_DIRECT_MODE = "fsk_direct_mode"
CONF_FSK_FILTER = "fsk_filter"
CONF_FSK_FRAME_GAP = "fsk_frame_gap"
CONF_FSK_MAX_EDGES = "fsk_max_edges"
CONF_FSK_MIN_RSSI = "fsk_min_rssi"
CONF_FSK_DATA_RATE_REGISTERS = "fsk_data_rate_registers"
CONF_FSK_BASEBAND_REGISTERS = "fsk_baseband_registers"

from esphome.core import CORE, TimePeriod

AUTO_LOAD = ["remote_base"]
DEPENDENCIES = ["libretiny"]

tuya_rf_ns = cg.esphome_ns.namespace("tuya_rf")
remote_base_ns = cg.esphome_ns.namespace("remote_base")

ToleranceMode = remote_base_ns.enum("ToleranceMode")

TYPE_PERCENTAGE = "percentage"
TYPE_TIME = "time"

TOLERANCE_MODE = {
    TYPE_PERCENTAGE: ToleranceMode.TOLERANCE_MODE_PERCENTAGE,
    TYPE_TIME: ToleranceMode.TOLERANCE_MODE_TIME,
}

TOLERANCE_SCHEMA = cv.typed_schema(
    {
        TYPE_PERCENTAGE: cv.Schema(
            {cv.Required(CONF_VALUE): cv.All(cv.percentage_int, cv.uint32_t)}
        ),
        TYPE_TIME: cv.Schema(
            {
                cv.Required(CONF_VALUE): cv.All(
                    cv.positive_time_period_microseconds,
                    cv.Range(max=TimePeriod(microseconds=4294967295)),
                )
            }
        ),
    },
    lower=True,
    enum=TOLERANCE_MODE,
)

MODULATION = {
    "ook": 0,
    "ask": 0,
    "2fsk": 1,
    "fsk": 1,
    "gfsk": 2,
}

FSK_PROFILE = {
    "rfdk_868_2k4_dev5": 0,
    "velux_86895_2k4_dev5": 1,
}

TuyaRfComponent = tuya_rf_ns.class_(
    "TuyaRfComponent", remote_base.RemoteReceiverBase, remote_base.RemoteTransmitterBase, cg.Component
)

TurnOffReceiverAction = tuya_rf_ns.class_("TurnOffReceiverAction", automation.Action)
TurnOnReceiverAction = tuya_rf_ns.class_("TurnOnReceiverAction", automation.Action)
SetTransmitFrequencyAction = tuya_rf_ns.class_("SetTransmitFrequencyAction", automation.Action)
QueueTransmitAction = tuya_rf_ns.class_("QueueTransmitAction", automation.Action)

def validate_frequency(value):
    if isinstance(value, str):
        value = value.lower().replace("mhz", "").strip()
    frequency = cv.int_(value)
    if frequency not in (315, 433, 868):
        raise cv.Invalid("frequency must be 315, 433, or 868 MHz")
    return frequency

def validate_uint8(value):
    if isinstance(value, str):
        try:
            value = int(value.strip(), 0)
        except ValueError as exc:
            raise cv.Invalid("value must be an 8-bit integer, for example 0x4B") from exc
    else:
        value = cv.int_(value)
    if value < 0 or value > 0xFF:
        raise cv.Invalid("value must be between 0x00 and 0xFF")
    return value

def validate_rssi_avg_mode(value):
    value = validate_uint8(value)
    if value > 7:
        raise cv.Invalid("rssi_avg_mode must be between 0 and 7")
    return value

def validate_register_list(count, name):
    def validator(value):
        value = cv.ensure_list(validate_uint8)(value)
        if len(value) != count:
            raise cv.Invalid(f"{name} must contain exactly {count} register values")
        return value
    return validator

AGC_OOK_REGISTER_KEYS = {
    "agc1": 0,
    "agc2": 1,
    "agc3": 2,
    "agc4": 3,
    "ook1": 4,
    "ook2": 5,
    "ook3": 6,
    "ook4": 7,
    "ook5": 8,
}

AGC_OOK_REGISTER_SCHEMA = cv.Schema(
    {
        cv.Optional(name): validate_uint8
        for name in AGC_OOK_REGISTER_KEYS
    }
)

def validate_agc_ook_registers(value):
    if isinstance(value, list):
        if len(value) != len(AGC_OOK_REGISTER_KEYS):
            raise cv.Invalid("agc_ook_registers list must contain exactly 9 values: agc1..agc4, ook1..ook5")
        return {
            name: validate_uint8(register_value)
            for name, register_value in zip(AGC_OOK_REGISTER_KEYS, value)
        }
    return AGC_OOK_REGISTER_SCHEMA(value)

TUYA_RF_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_RECEIVER_ID): cv.use_id(TuyaRfComponent),
    }
)

TUYA_RF_QUEUE_TRANSMIT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_RECEIVER_ID): cv.use_id(TuyaRfComponent),
        cv.Required(CONF_DATA): cv.templatable(cv.ensure_list(cv.int_)),
    }
)

TUYA_RF_SET_TRANSMIT_FREQUENCY_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.use_id(TuyaRfComponent),
        cv.Required(CONF_FREQUENCY): validate_frequency,
    }
)


@automation.register_action(
    "tuya_rf.turn_on_receiver",
    TurnOnReceiverAction,
    TUYA_RF_ACTION_SCHEMA,
    synchronous=True,
)
async def tuya_rf_turn_on_receiver_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_RECEIVER_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)

@automation.register_action(
    "tuya_rf.turn_off_receiver",
    TurnOffReceiverAction,
    TUYA_RF_ACTION_SCHEMA,
    synchronous=True,
)
async def tuya_rf_turn_off_receiver_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_RECEIVER_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)

@automation.register_action(
    "tuya_rf.queue_transmit",
    QueueTransmitAction,
    TUYA_RF_QUEUE_TRANSMIT_SCHEMA,
    synchronous=True,
)
async def tuya_rf_queue_transmit_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_RECEIVER_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(
        config[CONF_DATA], args, cg.std_vector.template(cg.int32)
    )
    cg.add(var.set_data(template_))
    return var

@automation.register_action(
    "tuya_rf.set_transmit_frequency",
    SetTransmitFrequencyAction,
    TUYA_RF_SET_TRANSMIT_FREQUENCY_SCHEMA,
    synchronous=True,
)
async def tuya_rf_set_transmit_frequency_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    cg.add(var.set_frequency(config[CONF_FREQUENCY]))
    return var


def validate_tolerance(value):
    if isinstance(value, dict):
        return TOLERANCE_SCHEMA(value)

    if "%" in str(value):
        type_ = TYPE_PERCENTAGE
    else:
        try:
            cv.positive_time_period_microseconds(value)
            type_ = TYPE_TIME
        except cv.Invalid as exc:
            raise cv.Invalid(
                "Tolerance must be a percentage or time. Configurations made before 2024.5.0 treated the value as a percentage."
            ) from exc

    return TOLERANCE_SCHEMA(
        {
            CONF_VALUE: value,
            CONF_TYPE: type_,
        }
    )

#MULTI_CONF will be possible once the cmt2300a code is refactored
#to use different spi pins for each instance
#MULTI_CONF = True
CONFIG_SCHEMA = remote_base.validate_triggers(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TuyaRfComponent),
            cv.Optional(CONF_SCLK_PIN, default='P14'): cv.All(pins.internal_gpio_output_pin_schema),
            cv.Optional(CONF_MOSI_PIN, default='P16'): cv.All(pins.internal_gpio_output_pin_schema),
            cv.Optional(CONF_CSB_PIN, default='P6'): cv.All(pins.internal_gpio_output_pin_schema),
            cv.Optional(CONF_FCSB_PIN, default='P26'): cv.All(pins.internal_gpio_output_pin_schema),
            cv.Optional(CONF_TX_PIN, default='P20'): cv.All(pins.internal_gpio_output_pin_schema),
            cv.Optional(CONF_RX_PIN, default='P22'): cv.All(pins.internal_gpio_input_pin_schema),
            cv.Optional(CONF_RECEIVER_DISABLED, default=False): cv.boolean,
            cv.Optional(CONF_DUMP, default=[]): remote_base.validate_dumpers,
            cv.Optional(CONF_TOLERANCE, default="25%"): validate_tolerance,
            cv.Optional(CONF_BUFFER_SIZE, default="1000b"): cv.validate_bytes,
            cv.Optional(CONF_FILTER, default="50us"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(max=TimePeriod(microseconds=4294967295)),
            ),
            cv.Optional(CONF_START_PULSE_MIN, default="6000us"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(max=TimePeriod(microseconds=4294967295)),
            ),
            cv.Optional(CONF_START_PULSE_MAX, default="10000us"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(max=TimePeriod(microseconds=4294967295)),
            ),
            cv.Optional(CONF_END_PULSE, default="50ms"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(max=TimePeriod(microseconds=4294967295)),
            ),
            cv.Optional(CONF_MAX_PAUSE, default="6000us"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(max=TimePeriod(microseconds=4294967295)),
            ),
            cv.Optional(CONF_FRAME_GAP, default="12ms"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(max=TimePeriod(microseconds=4294967295)),
            ),
            cv.Optional(CONF_MIN_PULSES, default=12): cv.All(
                cv.uint32_t,
                cv.Range(min=2, max=1000),
            ),
            cv.Optional(CONF_MAX_PULSES, default=200): cv.All(
                cv.uint32_t,
                cv.Range(min=2, max=1000),
            ),
            cv.Optional(CONF_MAX_FRAME_DURATION, default="250ms"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(max=TimePeriod(microseconds=4294967295)),
            ),
            cv.Optional(CONF_QUEUE_MAX_SIZE, default=10): cv.All(
                cv.uint32_t,
                cv.Range(min=1, max=100),
            ),
            cv.Optional(CONF_QUEUE_DELAY, default="100ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(max=TimePeriod(milliseconds=10000)),
            ),
            cv.Optional(CONF_SINGLE_RAW_DUMP, default=True): cv.boolean,
            cv.Optional(CONF_ACCEPT_ON_RESTART, default=True): cv.boolean,
            cv.Optional(CONF_DEDUPE_WINDOW, default="200ms"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(max=TimePeriod(microseconds=4294967295)),
            ),
            cv.Optional(CONF_DOUT_MUTE, default=False): cv.boolean,
            cv.Optional(CONF_FREQUENCY, default=433): validate_frequency,
            cv.Optional(CONF_RSSI_AVG_MODE): validate_rssi_avg_mode,
            cv.Optional(CONF_AGC_OOK_REGISTERS): validate_agc_ook_registers,
            cv.Optional(CONF_DUMP_SUPPRESSED_RAW, default=False): cv.boolean,
            cv.Optional(CONF_MODULATION, default="ook"): cv.enum(MODULATION, lower=True),
            cv.Optional(CONF_FSK_PROFILE, default="velux_86895_2k4_dev5"): cv.enum(FSK_PROFILE, lower=True),
            cv.Optional(CONF_FSK_DIRECT_MODE, default=True): cv.boolean,
            cv.Optional(CONF_FSK_FILTER, default="2us"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(max=TimePeriod(microseconds=4294967295)),
            ),
            cv.Optional(CONF_FSK_FRAME_GAP, default="5ms"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(max=TimePeriod(microseconds=4294967295)),
            ),
            cv.Optional(CONF_FSK_MAX_EDGES, default=900): cv.All(
                cv.uint32_t,
                cv.Range(min=2, max=1000),
            ),
            cv.Optional(CONF_FSK_MIN_RSSI, default=-128): cv.All(
                cv.int_,
                cv.Range(min=-128, max=0),
            ),
            cv.Optional(CONF_FSK_DATA_RATE_REGISTERS): validate_register_list(24, CONF_FSK_DATA_RATE_REGISTERS),
            cv.Optional(CONF_FSK_BASEBAND_REGISTERS): validate_register_list(29, CONF_FSK_BASEBAND_REGISTERS),
        }
    ).extend(cv.COMPONENT_SCHEMA)
)

def validate_pulses(config):
    start_pulse_min=config[CONF_START_PULSE_MIN]
    start_pulse_max=config[CONF_START_PULSE_MAX]
    end_pulse=config[CONF_END_PULSE]
    max_pause=config[CONF_MAX_PAUSE]
    frame_gap=config[CONF_FRAME_GAP]
    min_pulses=config[CONF_MIN_PULSES]
    max_pulses=config[CONF_MAX_PULSES]
    max_frame_duration=config[CONF_MAX_FRAME_DURATION]
    if start_pulse_max < start_pulse_min:
        raise cv.Invalid("start_pulse_max must be greater than start_pulse_min")
    if end_pulse < start_pulse_max:
        raise cv.Invalid("end_pulse must be greater than start_pulse_max")
    if frame_gap <= max_pause:
        raise cv.Invalid("frame_gap must be greater than max_pause")
    if max_pulses < min_pulses:
        raise cv.Invalid("max_pulses must be greater than or equal to min_pulses")
    if max_frame_duration <= frame_gap:
        raise cv.Invalid("max_frame_duration must be greater than frame_gap")

async def to_code(config):
    sclk_pin = await cg.gpio_pin_expression(config[CONF_SCLK_PIN])
    mosi_pin = await cg.gpio_pin_expression(config[CONF_MOSI_PIN])
    csb_pin = await cg.gpio_pin_expression(config[CONF_CSB_PIN])
    fcsb_pin = await cg.gpio_pin_expression(config[CONF_FCSB_PIN])
    rx_pin = await cg.gpio_pin_expression(config[CONF_RX_PIN])
    tx_pin = await cg.gpio_pin_expression(config[CONF_TX_PIN])
    var = cg.new_Pvariable(config[CONF_ID],sclk_pin,mosi_pin,csb_pin,fcsb_pin,tx_pin,rx_pin)
    #await cg.register_component(var, config)
    raw_dump_requested = any("raw" in dumper for dumper in config[CONF_DUMP])
    use_single_raw_dump = config[CONF_SINGLE_RAW_DUMP] and raw_dump_requested
    cg.add(var.set_single_raw_dump(use_single_raw_dump))
    dumpers_config = [
        dumper for dumper in config[CONF_DUMP] if not (use_single_raw_dump and "raw" in dumper)
    ]
    dumpers = await remote_base.build_dumpers(dumpers_config)
    for dumper in dumpers:
        cg.add(var.register_dumper(dumper))

    triggers = await remote_base.build_triggers(config)
    for trigger in triggers:
        cg.add(var.register_listener(trigger))
    await cg.register_component(var, config)

    cg.add(
        var.set_tolerance(
            config[CONF_TOLERANCE][CONF_VALUE], config[CONF_TOLERANCE][CONF_TYPE]
        )
    )
    cg.add(var.set_receiver_disabled(config[CONF_RECEIVER_DISABLED]))
    cg.add(var.set_buffer_size(config[CONF_BUFFER_SIZE]))
    cg.add(var.set_filter_us(config[CONF_FILTER]))
    cg.add(var.set_start_pulse_min_us(config[CONF_START_PULSE_MIN]))
    cg.add(var.set_start_pulse_max_us(config[CONF_START_PULSE_MAX]))
    cg.add(var.set_end_pulse_us(config[CONF_END_PULSE]))
    cg.add(var.set_max_pause_us(config[CONF_MAX_PAUSE]))
    cg.add(var.set_frame_gap_us(config[CONF_FRAME_GAP]))
    cg.add(var.set_min_pulses(config[CONF_MIN_PULSES]))
    cg.add(var.set_max_pulses(config[CONF_MAX_PULSES]))
    cg.add(var.set_max_frame_duration_us(config[CONF_MAX_FRAME_DURATION]))
    cg.add(var.set_queue_max_size(config[CONF_QUEUE_MAX_SIZE]))
    cg.add(var.set_queue_delay_ms(config[CONF_QUEUE_DELAY]))
    cg.add(var.set_accept_on_restart(config[CONF_ACCEPT_ON_RESTART]))
    cg.add(var.set_dedupe_window_us(config[CONF_DEDUPE_WINDOW]))
    cg.add(var.set_dout_mute(config[CONF_DOUT_MUTE]))
    cg.add(var.set_frequency_mhz(config[CONF_FREQUENCY]))
    cg.add(var.set_dump_suppressed_raw(config[CONF_DUMP_SUPPRESSED_RAW]))
    cg.add(var.set_modulation(config[CONF_MODULATION]))
    cg.add(var.set_fsk_profile(config[CONF_FSK_PROFILE]))
    cg.add(var.set_fsk_direct_mode(config[CONF_FSK_DIRECT_MODE]))
    cg.add(var.set_fsk_filter_us(config[CONF_FSK_FILTER]))
    cg.add(var.set_fsk_frame_gap_us(config[CONF_FSK_FRAME_GAP]))
    cg.add(var.set_fsk_max_edges(config[CONF_FSK_MAX_EDGES]))
    cg.add(var.set_fsk_min_rssi_dbm(config[CONF_FSK_MIN_RSSI]))
    if CONF_RSSI_AVG_MODE in config:
        cg.add(var.set_rssi_avg_mode(config[CONF_RSSI_AVG_MODE]))
    if CONF_AGC_OOK_REGISTERS in config:
        for name, offset in AGC_OOK_REGISTER_KEYS.items():
            if name in config[CONF_AGC_OOK_REGISTERS]:
                cg.add(var.set_agc_ook_register(offset, config[CONF_AGC_OOK_REGISTERS][name]))
    if CONF_FSK_DATA_RATE_REGISTERS in config:
        for offset, value in enumerate(config[CONF_FSK_DATA_RATE_REGISTERS]):
            cg.add(var.set_fsk_data_rate_register(offset, value))
    if CONF_FSK_BASEBAND_REGISTERS in config:
        for offset, value in enumerate(config[CONF_FSK_BASEBAND_REGISTERS]):
            cg.add(var.set_fsk_baseband_register(offset, value))
    validate_pulses(config)
