# tuya_rf
Custom component to integrate a tuya rf433 hub into esphome.

The tuya device I'm using is a Moes ufo-r2-rf, it's both an IR and RF bridge.

It uses a [CBU module](https://docs.libretiny.eu/boards/cbu/) (BK7231N) and an [SH4 RF module](https://developer.tuya.com/en/docs/iot/sh4-module-datasheet?id=Ka04qyuydvubw) (which appears to be using a CMT2300A).

For more hardware details see [this forum post](https://www.elektroda.com/rtvforum/topic3975921.html).

There are several devices using the same CBU/SH4 combo.

My code is based on the remote_receiver/remote_transmitter components, adding the initialization sequence to put the CMT2300A in direct RX or TX mode.

The transmitter works, I got the codes using the original firmware and the tinytuya [RFRemoteControlDevice.py](https://github.com/jasonacox/tinytuya/blob/master/tinytuya/Contrib/RFRemoteControlDevice.py), the gencodes.py script uses the data to generate the `remote_transmitter.transmit_raw` codes.

The codes have been captured from a ceiling fan remote, the only rf remote I have.
I also used the codes dumped from the receiver to confirm that they also
work.

The receiver (both ir using the standard remote_receiver and rf using this component) should work once this [pull request](https://github.com/libretiny-eu/libretiny/pull/290) lands in libretiny.

For the IR part (to be used with the standard remote_receiver/remote_transmitter) the receiver is on P8 and the transmitter on P7.

Keep in mind that the CMT2300A cannot transmit and receive at the same time, so it is normally set in RX mode (unless the receiver has been disabled). To transmit a code the CMT2300A is switched
to TX mode, the code is sent, then it is switched back to RX mode (or to standby if the receiver has been disabled).

## rf filtering

The rf signal is quite noisy, so I do some filtering to receive the codes:

1. the starting pulse must be longer than 6ms but shorter than 10ms.
2. any time I see a starting pulse I discard the data and start again (usually the same code is sent more than once).
3. the pauses cannot last more than 6ms, if I see a pause longer than that I discard the data and start again.
4. the end pulse is around 90ms, I look for a pulse of at least 50ms to detect the end of the data.
5. if the end pulse never arrives, a configurable gap timeout can end the frame.
6. very short or very long frames are discarded as noise before the raw dumpers/listeners are called.
7. if a frame exceeds the maximum pulse count or duration it is discarded before the buffer is near overflow.
8. if the receiving buffer is about to overflow I discard the data and start again.

I don't know if these values are good only for my remote, but there are configuration variables to tune them.
Set the logger to `DEBUG` to see frame start, restart, reject, accept, and periodic RF stats messages while tuning.
When `dump: raw` is enabled, this component logs accepted RF frames as one `tuya_rf.raw` line instead of using ESPHome's default raw dumper, which splits long frames into 256-byte chunks.

## configuration variables

You can use the same configuration variables of the [remote_transmitter](https://esphome.io/components/remote_transmitter) and of the [remote_receiver](https://esphome.io/components/remote_receiver) (with the exception of the pin, here you can set the tx_pin and the rx_pin, but if you don't set them the defaults are used, idle also isn't used).

| variable | default | description |
|--|--|--|
|receiver_disabled|false|set it to true to disable the receiver|
|tx_pin|P20|pin used to transmit data, note that by default it is inverted, so a pulse is 1 and a space is 0 (the signal on my device is reversed: 0 for a pulse, 1 for a space)|
|rx_pin|P22|pin used to receive the data, note that by default it is inverted just like the tx_pin|
|start_pulse_min|6ms|the minimun duration of the starting pulse|
|start_pulse_max|10ms|the maximum duration of the starting pulse, must be greater than start_pulse_min|
|end_pulse|50ms|the minimum duration of the ending pulse, must be greater than start_pulse_max|
|max_pause|6ms|the maximum pause allowed inside a frame before it is rejected|
|frame_gap|12ms|the quiet gap that ends a frame if no explicit end pulse arrives, must be greater than max_pause|
|min_pulses|12|the minimum number of raw pulse/space entries required before a frame is accepted|
|max_pulses|200|the maximum number of raw pulse/space entries allowed before a frame is rejected|
|max_frame_duration|250ms|the maximum time a frame may stay open before it is rejected, must be greater than frame_gap|
|single_raw_dump|true|when `dump: raw` is configured, log accepted raw frames as a single `tuya_rf.raw` line instead of using the default split `remote.raw` dumper|
|accept_on_restart|true|accept a plausible previous frame when another start pulse arrives, instead of always discarding it and keeping only the last repeat|
|dedupe_window|200ms|suppress accepted frames that arrive shortly after a previous accepted frame, useful when accept_on_restart exposes repeated copies inside one burst|
|frequency|433|default CMT2300A frequency in MHz. Supported values are 433 (433.920 MHz register bank), 315 (315.000 MHz register bank), and 868 (868.000 MHz register bank)|
|modulation|ook|receiver modulation path. Supported values are `ook`, `2fsk`/`fsk`, and `gfsk`. OOK is the normal raw pulse path; FSK/GFSK use the experimental direct-DOUT debug path|
|fsk_profile|velux_86895_2k4_dev5|FSK debug profile. `velux_86895_2k4_dev5` tunes the RF bank to 868.950 MHz and loads a 2-FSK RFPDK seed profile; `rfdk_868_2k4_dev5` keeps the standard 868 MHz bank|
|fsk_direct_mode|true|force the FSK baseband into direct mode so DOUT edges can be logged without known sync/packet settings|
|fsk_filter|2us|edge filter used only for FSK/GFSK debug; keep it low because FSK bit periods are much shorter than OOK pulses|
|fsk_frame_gap|5ms|quiet gap used to close an FSK debug burst|
|fsk_data_rate_registers||optional 24-byte override for CMT2300A registers 0x20..0x37, useful for pasting RFPDK FSK/GFSK exports|
|fsk_baseband_registers||optional 29-byte override for CMT2300A registers 0x38..0x54, useful for pasting RFPDK FSK/GFSK exports|
|sclk_pin|P14|clock of the spi communication with the CMT2300A. Only the number of the pin is used, the remaining parameters of the pin schema are ignored|
|mosi_pin|P16|the bidirectional data pin of the spi communication. Only the number of the pin is used, the remaining parameters of the pin schema are ignored|
|csb_pin|P6|spi chip select pin to read/write registers. Only the number of the pin is used, the remaining parameters of the pin schema are ignored|
|fcsb_pin|P26|spi chip select pin to read/write the fifo. It is not used|

### Experimental FSK/GFSK receive debug

The FSK/GFSK path is intended for discovery work, for example VELUX/io-homecontrol tests. It does not decode io-homecontrol and it does not know the real packet format. It only configures the CMT2300A for FSK demodulation and logs DOUT edge bursts with RSSI so you can see whether a demodulated bitstream is present.

```yaml
tuya_rf:
  id: rf
  receiver_disabled: true
  dump: raw
  frequency: 868
  modulation: 2fsk
  fsk_profile: velux_86895_2k4_dev5
  fsk_direct_mode: true
  fsk_filter: 2us
  fsk_frame_gap: 5ms
  min_pulses: 8
  max_pulses: 900
```

Expected debug output is `FSK burst accepted ...` followed by one `tuya_rf.raw` line prefixed with `FSK Raw`. If you get no bursts, the frequency or FSK data-rate/deviation profile is probably wrong. Use `fsk_data_rate_registers` and `fsk_baseband_registers` to test exact RFPDK exports without changing code.

## actions

Since the receiver is mostly useful to learn new buttons, it's better to
leave it off and turn it on only when you want to start learning.
There are two actions, `tuya_rf.turn_on_receiver` (to turn the receiver on)
and `tuya_rf.turn_off_receiver` (to turn it off).
Once the code is fixed to work with multiple instances of tuya_rf (currently
it only allows one instance) you shuould specify the `receiver_id` in the
action.

To use a different RF frequency for one normal ESPHome raw transmit, set the
next transmit frequency before `remote_transmitter.transmit_raw`:

```yaml
on_press:
  - tuya_rf.set_transmit_frequency:
      id: rf
      frequency: 868
  - remote_transmitter.transmit_raw:
      transmitter_id: rf
      code: [9188, -559, 1960]
```

To switch the default RX/TX frequency from ESPHome's web UI or Home Assistant,
add a template select. If the receiver is active, changing the select restarts
RX on the selected frequency. The YAML `frequency` remains the boot default:

```yaml
tuya_rf:
  id: rf
  frequency: 433

select:
  - platform: template
    name: RF Frequency
    id: rf_frequency
    optimistic: true
    initial_option: "433 MHz"
    options:
      - "315 MHz"
      - "433 MHz"
      - "868 MHz"
    set_action:
      - lambda: |-
          if (x == "315 MHz") {
            id(rf).set_frequency_mhz(315);
          } else if (x == "433 MHz") {
            id(rf).set_frequency_mhz(433);
          } else if (x == "868 MHz") {
            id(rf).set_frequency_mhz(868);
          }
```
