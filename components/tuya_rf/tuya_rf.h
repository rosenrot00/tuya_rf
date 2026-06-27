#pragma once

#include "esphome/components/remote_base/remote_base.h"
#include "esphome/core/component.h"
#include "globals.h"
#include "radio.h"
#include <vector>
#include <deque>
#include <string>
#include <cstdint>

namespace esphome {
namespace tuya_rf {

struct TransmitQueueItem {
  std::vector<int32_t> data;
  uint32_t queued_at;
};

#if defined(USE_LIBRETINY)
struct RemoteReceiverComponentStore {
  static void gpio_intr(RemoteReceiverComponentStore *arg);

  /// Stores the time (in micros) that the leading/falling edge happened at
  ///  * An even index means a falling edge appeared at the time stored at the index
  ///  * An uneven index means a rising edge appeared at the time stored at the index
  volatile uint32_t *buffer{nullptr};
  /// The position last written to
  volatile uint32_t buffer_write_at;
  /// The position last read from
  uint32_t buffer_read_at{0};
  bool overflow{false};
  uint32_t buffer_size{1000};
  uint32_t filter_us{10};
  ISRInternalGPIOPin pin;
};
#endif

class TuyaRfComponent : public remote_base::RemoteTransmitterBase,
                        public remote_base::RemoteReceiverBase,
                                   public Component
{
 public:
  explicit TuyaRfComponent(InternalGPIOPin *sclk_pin, InternalGPIOPin *mosi_pin, InternalGPIOPin *csb_pin, InternalGPIOPin *fcsb_pin,
     InternalGPIOPin *tx_pin, InternalGPIOPin *rx_pin) : remote_base::RemoteTransmitterBase(tx_pin), remote_base::RemoteReceiverBase(rx_pin) {
      this->sclk_pin_=sclk_pin;
      this->mosi_pin_=mosi_pin;
      this->csb_pin_=csb_pin;
      this->fcsb_pin_=fcsb_pin;
      tuya_sclk = this->sclk_pin_->get_pin();
      tuya_mosi = this->mosi_pin_->get_pin();
      tuya_csb = this->csb_pin_->get_pin();
      tuya_fcsb = this->fcsb_pin_->get_pin();
  }
  void setup() override;

  void dump_config() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_receiver_disabled(bool receiver_disabled) { this->receiver_disabled_ = receiver_disabled; }
  void set_buffer_size(uint32_t buffer_size) { this->buffer_size_ = buffer_size; }
  void set_filter_us(uint32_t filter_us) { this->filter_us_ = filter_us; }
  void set_start_pulse_min_us(uint32_t start_pulse_min_us) { this->start_pulse_min_us_ = start_pulse_min_us; }
  void set_start_pulse_max_us(uint32_t start_pulse_max_us) { this->start_pulse_max_us_ = start_pulse_max_us; }
  void set_end_pulse_us(uint32_t end_pulse_us) { this->end_pulse_us_ = end_pulse_us; }
  void set_max_pause_us(uint32_t max_pause_us) { this->max_pause_us_ = max_pause_us; }
  void set_frame_gap_us(uint32_t frame_gap_us) { this->frame_gap_us_ = frame_gap_us; }
  void set_min_pulses(uint32_t min_pulses) { this->min_pulses_ = min_pulses; }
  void set_max_pulses(uint32_t max_pulses) { this->max_pulses_ = max_pulses; }
  void set_max_frame_duration_us(uint32_t max_frame_duration_us) { this->max_frame_duration_us_ = max_frame_duration_us; }
  void set_single_raw_dump(bool single_raw_dump) { this->single_raw_dump_ = single_raw_dump; }
  void set_dump_suppressed_raw(bool dump_suppressed_raw) { this->dump_suppressed_raw_ = dump_suppressed_raw; }
  void set_accept_on_restart(bool accept_on_restart) { this->accept_on_restart_ = accept_on_restart; }
  void set_dedupe_window_us(uint32_t dedupe_window_us) { this->dedupe_window_us_ = dedupe_window_us; }
  void set_dout_mute(bool dout_mute) { this->dout_mute_ = dout_mute; }
  void set_rssi_avg_mode(int8_t rssi_avg_mode) {
    if (rssi_avg_mode >= 0 && rssi_avg_mode <= 7) {
      this->rssi_avg_mode_ = rssi_avg_mode;
    }
  }
  void set_agc_ook_register(uint8_t offset, uint8_t value) {
    if (offset < 9) {
      this->agc_ook_register_mask_ |= 1U << offset;
      this->agc_ook_registers_[offset] = value;
    }
  }
  void set_frequency_mhz(uint16_t frequency_mhz);
  void set_tx_profile_868(uint8_t tx_profile_868) { this->tx_profile_868_ = tx_profile_868 > 1 ? 1 : tx_profile_868; }
  void set_tx_power_868_dbm(int8_t tx_power_868_dbm) { this->tx_power_868_dbm_ = tx_power_868_dbm == 20 ? 20 : 13; }
  void set_next_transmit_frequency_mhz(uint16_t frequency_mhz) { this->next_transmit_frequency_mhz_ = frequency_mhz; }
  void set_modulation(uint8_t modulation) { this->modulation_ = modulation > TUYA_RF_MODULATION_GFSK ? TUYA_RF_MODULATION_OOK : modulation; }
  void set_fsk_profile(uint8_t fsk_profile) { this->fsk_profile_ = fsk_profile > TUYA_RF_FSK_PROFILE_VELUX_86895_2K4_DEV5 ? TUYA_RF_FSK_PROFILE_VELUX_86895_2K4_DEV5 : fsk_profile; }
  void set_fsk_direct_mode(bool fsk_direct_mode) { this->fsk_direct_mode_ = fsk_direct_mode; }
  void set_fsk_filter_us(uint32_t fsk_filter_us) { this->fsk_filter_us_ = fsk_filter_us; }
  void set_fsk_frame_gap_us(uint32_t fsk_frame_gap_us) { this->fsk_frame_gap_us_ = fsk_frame_gap_us; }
  void set_fsk_data_rate_register(uint8_t offset, uint8_t value) {
    if (offset < TUYA_RF_FSK_DATA_RATE_REGISTER_COUNT) {
      this->fsk_data_rate_register_mask_ |= 1UL << offset;
      this->fsk_data_rate_registers_[offset] = value;
    }
  }
  void set_fsk_baseband_register(uint8_t offset, uint8_t value) {
    if (offset < TUYA_RF_FSK_BASEBAND_REGISTER_COUNT) {
      this->fsk_baseband_register_mask_ |= 1UL << offset;
      this->fsk_baseband_registers_[offset] = value;
    }
  }
  void turn_on_receiver();
  void turn_off_receiver();

  // Queue management
  void set_queue_max_size(uint32_t max_size) { this->queue_max_size_ = max_size; }
  void set_queue_delay_ms(uint32_t delay_ms) { this->queue_delay_ms_ = delay_ms; }
  bool queue_transmit(const std::vector<int32_t> &data);
  void process_transmit_queue();

 protected:
  void send_internal(uint32_t send_times, uint32_t send_wait) override;

  void mark_(uint32_t usec);

  void space_(uint32_t usec);

  void await_target_time_();
  void set_receiver(bool on);
  void reset_receiver_buffer_();
  void log_frame_stats_(const char *event, uint32_t pulses, uint32_t duration_us);
  void log_raw_frame_(const char *prefix = "Received Raw");
  void log_buffer_raw_(const char *prefix, uint32_t end_index, uint32_t appended_us = 0);
  void process_fsk_debug_();
  uint32_t candidate_pulse_count_(uint32_t candidate_end) const;
  uint32_t target_time_;
#if defined(USE_LIBRETINY)
  RemoteReceiverComponentStore store_;
  HighFrequencyLoopRequester high_freq_;
#endif

  InternalGPIOPin *sclk_pin_;
  InternalGPIOPin *mosi_pin_;
  InternalGPIOPin *csb_pin_;
  InternalGPIOPin *fcsb_pin_;

  bool receiver_disabled_{false};
  uint32_t buffer_size_{};
  uint32_t filter_us_{50};
  uint32_t start_pulse_min_us_{6000};
  uint32_t start_pulse_max_us_{10000};
  uint32_t end_pulse_us_{50000};
  uint32_t max_pause_us_{6000};
  uint32_t frame_gap_us_{12000};
  uint32_t min_pulses_{12};
  uint32_t max_pulses_{200};
  uint32_t max_frame_duration_us_{250000};
  bool single_raw_dump_{false};
  bool dump_suppressed_raw_{false};
  bool accept_on_restart_{true};
  uint32_t dedupe_window_us_{200000};
  bool dout_mute_{false};
  int8_t rssi_avg_mode_{-1};
  uint16_t agc_ook_register_mask_{0};
  uint8_t agc_ook_registers_[9]{};
  uint16_t frequency_mhz_{433};
  uint16_t next_transmit_frequency_mhz_{0};
  uint8_t tx_profile_868_{1};
  int8_t tx_power_868_dbm_{13};
  uint8_t modulation_{TUYA_RF_MODULATION_OOK};
  uint8_t fsk_profile_{TUYA_RF_FSK_PROFILE_VELUX_86895_2K4_DEV5};
  bool fsk_direct_mode_{true};
  uint32_t fsk_filter_us_{2};
  uint32_t fsk_frame_gap_us_{5000};
  uint32_t fsk_data_rate_register_mask_{0};
  uint8_t fsk_data_rate_registers_[TUYA_RF_FSK_DATA_RATE_REGISTER_COUNT]{};
  uint32_t fsk_baseband_register_mask_{0};
  uint8_t fsk_baseband_registers_[TUYA_RF_FSK_BASEBAND_REGISTER_COUNT]{};

  bool setup_done_{false};
  bool transmitting_{false};
  bool receive_started_{false};
  bool fsk_receive_started_{false};
  uint32_t old_write_at_{0};
  uint32_t receive_start_time_{0};
  uint32_t receive_start_pulse_us_{0};
  uint32_t received_frames_{0};
  uint32_t rejected_frames_{0};
  uint32_t last_emit_time_{0};

  // Queue members
  std::deque<TransmitQueueItem> transmit_queue_;
  uint32_t queue_max_size_{10};  // Default max 10 items in queue
  uint32_t queue_delay_ms_{100}; // Default 100ms delay between transmissions
  uint32_t last_transmit_time_{0};
};

}  // namespace tuya_rf
}  // namespace esphome
