#include "tuya_rf.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "radio.h"
#ifdef USE_LIBRETINY

namespace esphome {
namespace tuya_rf {

static const char *const TAG = "tuya_rf";
static const char *const RAW_TAG = "tuya_rf.raw";
static const char *const AGC_OOK_REGISTER_NAMES[TUYA_RF_AGC_OOK_REGISTER_COUNT] = {
    "agc1", "agc2", "agc3", "agc4", "ook1", "ook2", "ook3", "ook4", "ook5"};
static const uint8_t AGC_OOK_REGISTER_ADDRS[TUYA_RF_AGC_OOK_REGISTER_COUNT] = {
    CMT2300A_CUS_AGC1, CMT2300A_CUS_AGC2, CMT2300A_CUS_AGC3, CMT2300A_CUS_AGC4, CMT2300A_CUS_OOK1,
    CMT2300A_CUS_OOK2, CMT2300A_CUS_OOK3, CMT2300A_CUS_OOK4, CMT2300A_CUS_OOK5};

static const char *modulation_name(uint8_t modulation) {
  switch (modulation) {
    case TUYA_RF_MODULATION_2FSK:
      return "2-FSK";
    case TUYA_RF_MODULATION_GFSK:
      return "GFSK";
    case TUYA_RF_MODULATION_OOK:
    default:
      return "OOK/ASK";
  }
}

static const char *fsk_profile_name(uint8_t profile) {
  switch (profile) {
    case TUYA_RF_FSK_PROFILE_RFPDK_868_2K4_DEV5:
      return "RFPDK 868 MHz 2-FSK 2.4 kbps dev=5 kHz";
    case TUYA_RF_FSK_PROFILE_VELUX_86895_2K4_DEV5:
    default:
      return "VELUX/io-homecontrol debug 868.95 MHz, 2-FSK seed 2.4 kbps dev=5 kHz";
  }
}

void IRAM_ATTR HOT RemoteReceiverComponentStore::gpio_intr(RemoteReceiverComponentStore *arg) {
  const uint32_t now = micros();
  // If the lhs is 1 (rising edge) we should write to an uneven index and vice versa
  const uint32_t next = (arg->buffer_write_at + 1) % arg->buffer_size;
  const bool level = !arg->pin.digital_read();
  if (level != next % 2)
    return;

  // If next is buffer_read, we have hit an overflow
  if (next == arg->buffer_read_at)
    return;

  const uint32_t last_change = arg->buffer[arg->buffer_write_at];
  const uint32_t time_since_change = now - last_change;
  if (time_since_change <= arg->filter_us)
    return;

  arg->buffer[arg->buffer_write_at = next] = now;
}

void TuyaRfComponent::turn_on_receiver() {
  if (this->receiver_disabled_) {
    this->receiver_disabled_=false;
    this->set_receiver(true);
   } else {
     ESP_LOGD(TAG,"receiver already active");
   }
}

void TuyaRfComponent::turn_off_receiver() {
  if (!this->receiver_disabled_) {
    this->receiver_disabled_=true;
    this->set_receiver(false);
   } else {
     ESP_LOGD(TAG,"receiver already disabled");
  }
}

void TuyaRfComponent::reset_receiver_buffer_() {
  auto &s = this->store_;
  if (!this->RemoteReceiverBase::pin_->digital_read()) {
    s.buffer_write_at = s.buffer_read_at = 1;
  } else {
    s.buffer_write_at = s.buffer_read_at = 0;
  }
  if (s.buffer != nullptr) {
    s.buffer[s.buffer_read_at] = micros();
  }
  this->old_write_at_ = s.buffer_read_at;
  this->receive_started_ = false;
  this->fsk_receive_started_ = false;
  this->receive_start_time_ = 0;
  this->receive_start_pulse_us_ = 0;
}

void TuyaRfComponent::restart_receiver_if_active_() {
  if (!this->setup_done_ || this->receiver_disabled_ || this->transmitting_) {
    return;
  }

  this->RemoteReceiverBase::pin_->detach_interrupt();
  this->high_freq_.stop();
  int res = StartRx(this->frequency_mhz_, this->dout_mute_, this->rssi_avg_mode_,
                    this->agc_ook_register_mask_, this->agc_ook_registers_,
                    this->modulation_, this->fsk_profile_, this->fsk_direct_mode_,
                    this->fsk_data_rate_register_mask_, this->fsk_data_rate_registers_,
                    this->fsk_baseband_register_mask_, this->fsk_baseband_registers_);
  if (res != 0) {
    ESP_LOGE(TAG, "Error restarting RX at %u MHz, modulation=%s (%d)",
             static_cast<unsigned>(this->frequency_mhz_), modulation_name(this->modulation_), res);
    return;
  }
  this->store_.filter_us = this->modulation_ == TUYA_RF_MODULATION_OOK ? this->filter_us_ : this->fsk_filter_us_;
  this->reset_receiver_buffer_();
  this->RemoteReceiverBase::pin_->attach_interrupt(RemoteReceiverComponentStore::gpio_intr, &this->store_, gpio::INTERRUPT_ANY_EDGE);
  this->high_freq_.start();
}

void TuyaRfComponent::set_frequency_mhz(uint16_t frequency_mhz) {
  if (frequency_mhz != 315 && frequency_mhz != 433 && frequency_mhz != 868) {
    ESP_LOGW(TAG, "Unsupported frequency %u MHz", static_cast<unsigned>(frequency_mhz));
    return;
  }

  if (this->frequency_mhz_ == frequency_mhz) {
    return;
  }

  ESP_LOGD(TAG, "RF frequency changed: %u MHz -> %u MHz",
           static_cast<unsigned>(this->frequency_mhz_), static_cast<unsigned>(frequency_mhz));
  this->frequency_mhz_ = frequency_mhz;
  this->last_emit_time_ = 0;

  this->restart_receiver_if_active_();
}

void TuyaRfComponent::set_modulation(uint8_t modulation) {
  modulation = modulation > TUYA_RF_MODULATION_GFSK ? TUYA_RF_MODULATION_OOK : modulation;
  if (this->modulation_ == modulation) {
    return;
  }
  ESP_LOGD(TAG, "RF modulation changed: %s -> %s", modulation_name(this->modulation_), modulation_name(modulation));
  this->modulation_ = modulation;
  this->last_emit_time_ = 0;
  this->restart_receiver_if_active_();
}

void TuyaRfComponent::set_fsk_profile(uint8_t fsk_profile) {
  fsk_profile = fsk_profile > TUYA_RF_FSK_PROFILE_VELUX_86895_2K4_DEV5
                    ? TUYA_RF_FSK_PROFILE_VELUX_86895_2K4_DEV5
                    : fsk_profile;
  if (this->fsk_profile_ == fsk_profile) {
    return;
  }
  ESP_LOGD(TAG, "RF FSK profile changed: %s -> %s", fsk_profile_name(this->fsk_profile_),
           fsk_profile_name(fsk_profile));
  this->fsk_profile_ = fsk_profile;
  this->last_emit_time_ = 0;
  if (this->modulation_ != TUYA_RF_MODULATION_OOK) {
    this->restart_receiver_if_active_();
  }
}

void TuyaRfComponent::set_fsk_direct_mode(bool fsk_direct_mode) {
  if (this->fsk_direct_mode_ == fsk_direct_mode) {
    return;
  }
  ESP_LOGD(TAG, "RF FSK direct mode changed: %s -> %s", YESNO(this->fsk_direct_mode_), YESNO(fsk_direct_mode));
  this->fsk_direct_mode_ = fsk_direct_mode;
  this->last_emit_time_ = 0;
  if (this->modulation_ != TUYA_RF_MODULATION_OOK) {
    this->restart_receiver_if_active_();
  }
}

void TuyaRfComponent::set_fsk_filter_us(uint32_t fsk_filter_us) {
  this->fsk_filter_us_ = fsk_filter_us;
  if (this->setup_done_ && this->modulation_ != TUYA_RF_MODULATION_OOK) {
    this->store_.filter_us = this->fsk_filter_us_;
    this->reset_receiver_buffer_();
  }
}

void TuyaRfComponent::set_fsk_frame_gap_us(uint32_t fsk_frame_gap_us) {
  this->fsk_frame_gap_us_ = fsk_frame_gap_us;
}

void TuyaRfComponent::set_receiver(bool on) {
  if (on) {
    ESP_LOGD(TAG, "starting receiver at %u MHz, modulation=%s", static_cast<unsigned>(this->frequency_mhz_),
             modulation_name(this->modulation_));
    auto &s = this->store_;
    if (s.buffer==NULL) {
      ESP_LOGD(TAG,"allocating receiver buffer");
      s.buffer = new uint32_t[s.buffer_size];
      void *buf = (void *) s.buffer;
      memset(buf, 0, s.buffer_size * sizeof(uint32_t));
    }
    s.filter_us = this->modulation_ == TUYA_RF_MODULATION_OOK ? this->filter_us_ : this->fsk_filter_us_;
    if (!this->transmitting_) {
      int res = StartRx(this->frequency_mhz_, this->dout_mute_, this->rssi_avg_mode_,
                        this->agc_ook_register_mask_, this->agc_ook_registers_,
                        this->modulation_, this->fsk_profile_, this->fsk_direct_mode_,
                        this->fsk_data_rate_register_mask_, this->fsk_data_rate_registers_,
                        this->fsk_baseband_register_mask_, this->fsk_baseband_registers_);
      if (res != 0) {
        ESP_LOGE(TAG, "Error starting RX (%d)", res);
        return;
      }
    }
    // First index is a space (signal is inverted)
    this->reset_receiver_buffer_();
    this->RemoteReceiverBase::pin_->attach_interrupt(RemoteReceiverComponentStore::gpio_intr, &this->store_, gpio::INTERRUPT_ANY_EDGE);
    this->high_freq_.start();
  } else {
    ESP_LOGD(TAG, "stopping receiver");
    if (!this->transmitting_) {
      if(CMT2300A_GoStby()) {
        //ESP_LOGD(TAG,"go stby ok");
      } else {
        ESP_LOGE(TAG,"go stby error");
      }
    }
    this->RemoteReceiverBase::pin_->detach_interrupt();
    this->high_freq_.stop();
  }
}

void TuyaRfComponent::setup() {
  this->RemoteTransmitterBase::pin_->setup();
  this->RemoteTransmitterBase::pin_->digital_write(false);
  this->RemoteReceiverBase::pin_->setup();
  
  auto &s = this->store_;
  s.filter_us = this->modulation_ == TUYA_RF_MODULATION_OOK ? this->filter_us_ : this->fsk_filter_us_;
  s.pin = this->RemoteReceiverBase::pin_->to_isr();
  s.buffer_size = this->buffer_size_;

  if (s.buffer_size % 2 != 0) {
    // Make sure divisible by two. This way, we know that every 0bxxx0 index is a space and every 0bxxx1 index is a mark
    s.buffer_size++;
  }
  if (s.buffer==NULL) {
    s.buffer = new uint32_t[s.buffer_size];
    void *buf = (void *) s.buffer;
    memset(buf, 0, s.buffer_size * sizeof(uint32_t));
  }

  // Initialize CMT2300A radio chip once at boot.
  // StartTx()/StartRx() only switch modes — no full reinit per call.
  if (RF_Init() != 0) {
    ESP_LOGE(TAG, "CMT2300A initialization failed!");
  }

  this->setup_done_ = true;
  this->set_receiver(!this->receiver_disabled_);
}

void TuyaRfComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Tuya Rf:");
  LOG_PIN("  Sclk Pin: ",this->sclk_pin_);
  LOG_PIN("  Mosi Pin: ",this->mosi_pin_);
  LOG_PIN("  Csb Pin: ",this->csb_pin_);
  LOG_PIN("  Fcsb Pin: ",this->fcsb_pin_);
  LOG_PIN("  Tx Pin: ",this->RemoteTransmitterBase::pin_);
  LOG_PIN("  Rx Pin: ", this->RemoteReceiverBase::pin_);
  //probably the warning isn't useful due to the noisy signal
  if (!this->RemoteReceiverBase::pin_->digital_read()) {
    ESP_LOGW(TAG, "Remote Receiver Signal starts with a HIGH value. Usually this means you have to "
                  "invert the signal using 'inverted: True' in the pin schema!");
    ESP_LOGW(TAG, "It could also be that the signal is noisy.");
  }
  ESP_LOGCONFIG(TAG, "  Buffer Size: %u", this->buffer_size_);
  ESP_LOGCONFIG(TAG, "  Tolerance: %u%s", this->tolerance_,
                (this->tolerance_mode_ == remote_base::TOLERANCE_MODE_TIME) ? " us" : "%");
  ESP_LOGCONFIG(TAG, "  Filter out pulses shorter than: %u us", this->filter_us_);
  ESP_LOGCONFIG(TAG, "  Signal start with a pulse between %u and %u us", this->start_pulse_min_us_, this->start_pulse_max_us_);
  ESP_LOGCONFIG(TAG, "  Signal is done after a pulse of %u us", this->end_pulse_us_);
  ESP_LOGCONFIG(TAG, "  Max pause during a frame: %u us", this->max_pause_us_);
  ESP_LOGCONFIG(TAG, "  Gap timeout ends a frame after: %u us", this->frame_gap_us_);
  ESP_LOGCONFIG(TAG, "  Minimum frame pulses: %u", this->min_pulses_);
  ESP_LOGCONFIG(TAG, "  Maximum frame pulses: %u", this->max_pulses_);
  ESP_LOGCONFIG(TAG, "  Maximum frame duration: %u us", this->max_frame_duration_us_);
  ESP_LOGCONFIG(TAG, "  Single-line raw dump: %s", YESNO(this->single_raw_dump_));
  ESP_LOGCONFIG(TAG, "  Suppressed raw dump: %s", YESNO(this->dump_suppressed_raw_));
  ESP_LOGCONFIG(TAG, "  Accept previous frame on repeated start: %s", YESNO(this->accept_on_restart_));
  ESP_LOGCONFIG(TAG, "  Dedupe accepted frames within: %u us", this->dedupe_window_us_);
  ESP_LOGCONFIG(TAG, "  DOUT mute: %s", YESNO(this->dout_mute_));
  if (this->rssi_avg_mode_ >= 0) {
    ESP_LOGCONFIG(TAG, "  RSSI avg mode override: %d", this->rssi_avg_mode_);
  } else {
    ESP_LOGCONFIG(TAG, "  RSSI avg mode override: disabled");
  }
  if (this->agc_ook_register_mask_ == 0) {
    ESP_LOGCONFIG(TAG, "  AGC/OOK register overrides: none");
  } else {
    for (uint8_t i = 0; i < TUYA_RF_AGC_OOK_REGISTER_COUNT; i++) {
      if ((this->agc_ook_register_mask_ & (1U << i)) != 0) {
        ESP_LOGCONFIG(TAG, "  AGC/OOK override %s (0x%02X): 0x%02X", AGC_OOK_REGISTER_NAMES[i],
                      AGC_OOK_REGISTER_ADDRS[i], this->agc_ook_registers_[i]);
      }
    }
  }
  ESP_LOGCONFIG(TAG, "  Frequency: %u MHz", this->frequency_mhz_);
  ESP_LOGCONFIG(TAG, "  Modulation: %s", modulation_name(this->modulation_));
  if (this->modulation_ != TUYA_RF_MODULATION_OOK) {
    ESP_LOGCONFIG(TAG, "  FSK profile: %s", fsk_profile_name(this->fsk_profile_));
    ESP_LOGCONFIG(TAG, "  FSK direct DOUT debug: %s", YESNO(this->fsk_direct_mode_));
    ESP_LOGCONFIG(TAG, "  FSK edge filter: %u us", this->fsk_filter_us_);
    ESP_LOGCONFIG(TAG, "  FSK burst gap: %u us", this->fsk_frame_gap_us_);
    ESP_LOGCONFIG(TAG, "  FSK max edges: %u", this->fsk_max_edges_);
    ESP_LOGCONFIG(TAG, "  FSK minimum RSSI: %d dBm", this->fsk_min_rssi_dbm_);
    if (this->fsk_data_rate_register_mask_ != 0) {
      ESP_LOGCONFIG(TAG, "  FSK data-rate register overrides: mask=0x%06X", this->fsk_data_rate_register_mask_);
    }
    if (this->fsk_baseband_register_mask_ != 0) {
      ESP_LOGCONFIG(TAG, "  FSK baseband register overrides: mask=0x%08X", this->fsk_baseband_register_mask_);
    }
    if (this->modulation_ == TUYA_RF_MODULATION_GFSK &&
        this->fsk_data_rate_register_mask_ == 0 && this->fsk_baseband_register_mask_ == 0) {
      ESP_LOGW(TAG, "  GFSK selected without custom RFPDK registers; using the built-in 2-FSK seed profile");
    }
  }
  ESP_LOGCONFIG(TAG, "  Transmit Queue Max Size: %u", this->queue_max_size_);
  ESP_LOGCONFIG(TAG, "  Transmit Queue Delay: %u ms", this->queue_delay_ms_);
  if (this->receiver_disabled_) {
    ESP_LOGCONFIG(TAG, "  Receiver disabled");
  } else {
    ESP_LOGCONFIG(TAG, "  Receiver enabled");
  }
}

void TuyaRfComponent::log_frame_stats_(const char *event, uint32_t pulses, uint32_t duration_us) {
  const int rssi_dbm = CMT2300A_GetRssiDBm();
  ESP_LOGD(TAG, "RF stats: event=%s accepted=%u rejected=%u pulses=%u duration=%u us start=%u us rssi=%d dBm",
           event, this->received_frames_, this->rejected_frames_, pulses, duration_us,
           this->receive_start_pulse_us_, rssi_dbm);
}

void TuyaRfComponent::log_raw_frame_(const char *prefix) {
  const size_t size = this->RemoteReceiverBase::temp_.size();
  const size_t raw_size = size == 0 ? 0 : size - 1;
  std::string raw;
  raw.reserve(16 + raw_size * 8);
  raw = prefix;
  raw += ": ";
  for (size_t i = 0; i < raw_size; i++) {
    if (i != 0) {
      raw += ", ";
    }
    raw += std::to_string(this->RemoteReceiverBase::temp_[i]);
  }
  ESP_LOGI(RAW_TAG, "%s", raw.c_str());
}

void TuyaRfComponent::log_buffer_raw_(const char *prefix, uint32_t end_index, uint32_t appended_us) {
  auto &s = this->store_;
  uint32_t prev = s.buffer_read_at;
  uint32_t current = (prev + 1) % s.buffer_size;
  int32_t multiplier = current % 2 == 0 ? 1 : -1;
  std::string raw;
  raw.reserve(16 + this->max_pulses_ * 8);
  raw = prefix;
  raw += ": ";
  bool first = true;

  while (prev != end_index) {
    const int32_t delta = s.buffer[current] - s.buffer[prev];
    if (!first) {
      raw += ", ";
    }
    raw += std::to_string(multiplier * delta);
    first = false;
    prev = current;
    current = (current + 1) % s.buffer_size;
    multiplier *= -1;
  }

  if (appended_us != 0) {
    if (!first) {
      raw += ", ";
    }
    raw += std::to_string(multiplier * static_cast<int32_t>(appended_us));
  }

  ESP_LOGI(RAW_TAG, "%s", raw.c_str());
}

uint32_t TuyaRfComponent::candidate_pulse_count_(uint32_t candidate_end) const {
  auto &s = this->store_;
  const uint32_t first = (s.buffer_read_at + 1) % s.buffer_size;
  return 1 + (s.buffer_size + candidate_end - first) % s.buffer_size;
}

void TuyaRfComponent::process_fsk_debug_() {
  auto &s = this->store_;
  const uint32_t write_at = s.buffer_write_at;
  uint32_t dist = (s.buffer_size + write_at - s.buffer_read_at) % s.buffer_size;
  const uint32_t now = micros();

  if (!this->fsk_receive_started_ && dist > 1) {
    s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
    old_write_at_ = s.buffer_read_at;
    this->fsk_receive_started_ = true;
    this->receive_start_time_ = s.buffer[s.buffer_read_at];
    this->receive_start_pulse_us_ = 0;
    dist = (s.buffer_size + write_at - s.buffer_read_at) % s.buffer_size;
  }

  if (!this->fsk_receive_started_) {
    return;
  }

  const uint32_t gap = now - s.buffer[write_at];
  const bool forced_end = dist > this->fsk_max_edges_ || dist >= s.buffer_size - 5;
  if (!forced_end && gap < this->fsk_frame_gap_us_) {
    return;
  }

  const uint32_t edge_count = dist;
  const uint32_t duration_us = now - this->receive_start_time_;
  if (edge_count < this->min_pulses_) {
    this->rejected_frames_++;
    ESP_LOGD(TAG, "FSK burst rejected: edges=%u below min_pulses=%u duration=%u us gap=%u us rejected=%u",
             edge_count, this->min_pulses_, duration_us, gap, this->rejected_frames_);
    this->fsk_receive_started_ = false;
    s.buffer_read_at = write_at;
    old_write_at_ = write_at;
    return;
  }

  const int rssi_dbm = CMT2300A_GetRssiDBm();
  if (rssi_dbm < this->fsk_min_rssi_dbm_) {
    this->rejected_frames_++;
    ESP_LOGD(TAG,
             "FSK burst ignored: rssi=%d dBm below min_rssi=%d dBm edges=%u duration=%u us gap=%u us rejected=%u",
             rssi_dbm, this->fsk_min_rssi_dbm_, edge_count, duration_us, gap, this->rejected_frames_);
    this->fsk_receive_started_ = false;
    s.buffer_read_at = write_at;
    old_write_at_ = write_at;
    return;
  }

  this->received_frames_++;
  ESP_LOGD(TAG, "FSK burst accepted #%u: edges=%u duration=%u us gap=%u us rssi=%d dBm%s",
           this->received_frames_, edge_count, duration_us, gap, rssi_dbm,
           forced_end ? " forced" : "");
  if (this->single_raw_dump_) {
    this->log_buffer_raw_("FSK Raw", write_at, gap);
  }

  this->fsk_receive_started_ = false;
  s.buffer_read_at = write_at;
  old_write_at_ = write_at;
}

void TuyaRfComponent::await_target_time_() {
  const uint32_t current_time = micros();
  if (this->target_time_ == 0) {
    this->target_time_ = current_time;
  } else {
    while (this->target_time_ > micros()) {
      // busy loop that ensures micros is constantly called
    }
  }
}

void TuyaRfComponent::mark_(uint32_t usec) {
  this->await_target_time_();
  this->RemoteTransmitterBase::pin_->digital_write(true);
  this->target_time_ += usec;
}

void TuyaRfComponent::space_(uint32_t usec) {
  this->await_target_time_();
  this->RemoteTransmitterBase::pin_->digital_write(false);
  this->target_time_ += usec;
}

void IRAM_ATTR TuyaRfComponent::send_internal(uint32_t send_times, uint32_t send_wait) {
  ESP_LOGD(TAG, "Sending remote code...");
  const uint32_t tx_requested_ms = millis();
  const uint32_t tx_requested_us = micros();

  this->transmitting_=true;
  this->RemoteTransmitterBase::pin_->digital_write(false);
  const bool restore_receiver = !this->receiver_disabled_;
  if (restore_receiver) {
    this->RemoteReceiverBase::pin_->detach_interrupt();
    this->high_freq_.stop();
    this->receive_started_ = false;
  }

  // StartTx runs WITHOUT InterruptLock — it does SPI communication and
  // polls AutoSwitchStatus which needs working timers (millis/micros).
  // With interrupts disabled, the timeout loop behaved unpredictably.
  const uint16_t frequency_mhz = this->next_transmit_frequency_mhz_ != 0 ? this->next_transmit_frequency_mhz_ : this->frequency_mhz_;
  this->next_transmit_frequency_mhz_ = 0;
  ESP_LOGD(TAG, "Transmit frequency: %u MHz", frequency_mhz);
  if (frequency_mhz == 868) {
    ESP_LOGD(TAG, "Transmit uses 868 MHz TX bank: %s, tx_power=%d dBm",
             this->tx_profile_868_ == TUYA_RF_TX_PROFILE_868_RFPDK ? "RFPDK profile" : "Ahoy/OpenDTU profile",
             this->tx_power_868_dbm_);
    if (this->tx_profile_868_ == TUYA_RF_TX_PROFILE_868_RFPDK && this->tx_power_868_dbm_ == 20) {
      ESP_LOGW(TAG, "RFPDK 868 profile with 20 dBm uses the Ahoy/OpenDTU PA override; test RFPDK with 13 dBm first");
    }
  }

  const auto &data = this->RemoteTransmitterBase::temp_.get_data();
  uint32_t estimated_waveform_us = (4700 - 2200) + 2000;
  for (uint32_t i = 0; i < send_times; i++) {
    for (int32_t item : data) {
      estimated_waveform_us += item > 0 ? uint32_t(item) : uint32_t(-item);
    }
    if (i + 1 < send_times && send_wait > 0) {
      estimated_waveform_us += send_wait;
    }
  }
  ESP_LOGD(TAG, "TX request: repeat=%u wait=%u us pulses=%u estimated_waveform=%u us",
           send_times, send_wait, static_cast<unsigned>(data.size()), estimated_waveform_us);

  int res=StartTx(frequency_mhz, this->tx_profile_868_, this->tx_power_868_dbm_);
  switch(res) {
    case 0:
      //ESP_LOGD(TAG,"StartTx ok");
      break;
    case 1:
      ESP_LOGE(TAG,"Error configuring RF registers after %u us", micros() - tx_requested_us);
      this->transmitting_=false;
      if (restore_receiver) {
        this->set_receiver(true);
      }
      return;
    case 2:
      ESP_LOGE(TAG,"Error go tx after %u us", micros() - tx_requested_us);
      this->transmitting_=false;
      if (restore_receiver) {
        this->set_receiver(true);
      }
      return;
    case 3:
      ESP_LOGE(TAG,"Unsupported frequency %u MHz after %u us", frequency_mhz, micros() - tx_requested_us);
      this->transmitting_=false;
      if (restore_receiver) {
        this->set_receiver(true);
      }
      return;
    default:
      ESP_LOGE(TAG,"Unknown error %d after %u us",res, micros() - tx_requested_us);
      this->transmitting_=false;
      if (restore_receiver) {
        this->set_receiver(true);
      }
      return;
  }
  const uint32_t radio_ready_us = micros();
  ESP_LOGD(TAG, "TX radio ready after %u us", radio_ready_us - tx_requested_us);

  this->RemoteTransmitterBase::pin_->digital_write(false);
  this->target_time_ = 0;

  {
    // InterruptLock ONLY for timing-critical RF bit-bang.
    // mark_() and space_() use busy-loop microsecond timing that
    // must not be disrupted by ISRs.
    InterruptLock lock;

    this->space_(4700-2200);
    for (uint32_t i = 0; i < send_times; i++) {
      for (int32_t item : data) {
        if (item > 0) {
          const auto length = uint32_t(item);
          this->mark_(length);
        } else {
          const auto length = uint32_t(-item);
          this->space_(length);
        }
        App.feed_wdt();
      }
      if (i + 1 < send_times && send_wait>0)
        this->space_(send_wait);
    }
    this->space_(2000);
    this->await_target_time_();
    this->RemoteTransmitterBase::pin_->digital_write(false);
  }  // InterruptLock released — timers work again for mode switching below

  const uint32_t waveform_done_us = micros();
  ESP_LOGD(TAG, "TX waveform done after %u us", waveform_done_us - radio_ready_us);

  this->transmitting_=false;
  if (!restore_receiver) {
    if(CMT2300A_GoStby()) {
      //ESP_LOGD(TAG,"go stby ok");
    } else {
      ESP_LOGE(TAG,"go stby error");
    }
  } else {
    //Go back to rx mode
    int rx_res = StartRx(this->frequency_mhz_, this->dout_mute_, this->rssi_avg_mode_,
                         this->agc_ook_register_mask_, this->agc_ook_registers_,
                         this->modulation_, this->fsk_profile_, this->fsk_direct_mode_,
                         this->fsk_data_rate_register_mask_, this->fsk_data_rate_registers_,
                         this->fsk_baseband_register_mask_, this->fsk_baseband_registers_);
    if (rx_res != 0) {
      ESP_LOGE(TAG, "Error returning to RX (%d)", rx_res);
    } else {
      this->reset_receiver_buffer_();
      this->RemoteReceiverBase::pin_->attach_interrupt(RemoteReceiverComponentStore::gpio_intr, &this->store_, gpio::INTERRUPT_ANY_EDGE);
      this->high_freq_.start();
    }
  }
  ESP_LOGD(TAG, "TX complete after %u ms", millis() - tx_requested_ms);
}

/*
The rf input is quite noisy, so some heavy filtering must be done:

  - the signal is inverted (the pin gives 0 for mark, 1 for
    space, with the inversion the space is 0 and pulse is 1).

  - the starting pulse must be more than a specified duration
    (start_pulse_min_us_) but less than start_pulse_max_us_
    The default values work with my remote, I don't know if 
    they are valid for other remotes.

	  - the cmt2300a gives a very long pulse (90ms) at the end of a
	    reception, the parameter to detect it is end_pulse_us_.

	  - if no end pulse arrives, frame_gap_us_ closes the frame after a
	    quiet period, and min_pulses_ rejects short noise bursts.
	*/
void TuyaRfComponent::loop() {
  // Process transmit queue first
  this->process_transmit_queue();

  if (this->receiver_disabled_) {
    return;
  }  
  if (this->modulation_ != TUYA_RF_MODULATION_OOK) {
    this->process_fsk_debug_();
    return;
  }
  auto &s = this->store_;

  // copy write at to local variables, as it's volatile
  const uint32_t write_at = s.buffer_write_at;
  const uint32_t dist = (s.buffer_size + write_at - s.buffer_read_at) % s.buffer_size;
  const uint32_t now = micros();

  bool receive_end = false;
  uint32_t receive_end_time = now;
  uint32_t end_marker_us = this->end_pulse_us_;
  const char *end_reason = "end pulse";
  uint32_t new_write_at = old_write_at_;

  if (receive_started_ && receive_start_time_ != 0) {
    const uint32_t frame_duration_us = now - receive_start_time_;
    if (frame_duration_us > this->max_frame_duration_us_) {
      if (this->dedupe_window_us_ > 0 && this->last_emit_time_ != 0 &&
          now - this->last_emit_time_ < this->dedupe_window_us_) {
        const uint32_t last_gap_us = now - s.buffer[write_at];
        const bool open_segment_is_pulse = ((write_at + 1) % s.buffer_size) % 2 == 0;
        const int rssi_dbm = CMT2300A_GetRssiDBm();
        ESP_LOGD(TAG, "RF frame duplicate tail suppressed: duration=%u us exceeds max_frame_duration=%u us, dist=%u, start=%u us, last_gap=%u us, open=%s, tail_rssi=%d dBm, since_last=%u us",
                 frame_duration_us, this->max_frame_duration_us_, dist, this->receive_start_pulse_us_,
                 last_gap_us, open_segment_is_pulse ? "pulse" : "space", rssi_dbm, now - this->last_emit_time_);
        if (this->dump_suppressed_raw_) {
          this->log_buffer_raw_("Suppressed Tail Raw", write_at, last_gap_us);
        }
        this->last_emit_time_ = now;
        receive_started_=false;
        s.buffer_read_at = write_at;
        old_write_at_ = write_at;
        return;
      }
      this->rejected_frames_++;
      ESP_LOGW(TAG, "RF frame rejected: duration=%u us exceeds max_frame_duration=%u us (dist=%u, rejected=%u)",
               frame_duration_us, this->max_frame_duration_us_, dist, this->rejected_frames_);
      if (this->rejected_frames_ % 10 == 0) {
        this->log_frame_stats_("reject-duration", dist, frame_duration_us);
      }
      receive_started_=false;
      s.buffer_read_at = write_at;
      old_write_at_ = write_at;
      return;
    }
  }

  if (receive_started_ && dist > this->max_pulses_) {
    const uint32_t frame_duration_us = receive_start_time_ == 0 ? 0 : now - receive_start_time_;
    if (this->dedupe_window_us_ > 0 && this->last_emit_time_ != 0 &&
        now - this->last_emit_time_ < this->dedupe_window_us_) {
      const uint32_t last_gap_us = now - s.buffer[write_at];
      const bool open_segment_is_pulse = ((write_at + 1) % s.buffer_size) % 2 == 0;
      const int rssi_dbm = CMT2300A_GetRssiDBm();
      ESP_LOGD(TAG, "RF frame duplicate tail suppressed: buffered pulses=%u exceeds max_pulses=%u, start=%u us, duration=%u us, last_gap=%u us, open=%s, tail_rssi=%d dBm, since_last=%u us",
               dist, this->max_pulses_, this->receive_start_pulse_us_, frame_duration_us, last_gap_us,
               open_segment_is_pulse ? "pulse" : "space", rssi_dbm, now - this->last_emit_time_);
      if (this->dump_suppressed_raw_) {
        this->log_buffer_raw_("Suppressed Tail Raw", write_at, last_gap_us);
      }
      this->last_emit_time_ = now;
      receive_started_=false;
      s.buffer_read_at = write_at;
      old_write_at_ = write_at;
      return;
    }
    this->rejected_frames_++;
    ESP_LOGW(TAG, "RF frame rejected: buffered pulses=%u exceeds max_pulses=%u (rejected=%u)",
             dist, this->max_pulses_, this->rejected_frames_);
    if (this->rejected_frames_ % 10 == 0) {
      this->log_frame_stats_("reject-buffered-pulses", dist, frame_duration_us);
    }
    receive_started_=false;
    s.buffer_read_at = write_at;
    old_write_at_ = write_at;
    return;
  }

  //stop the reception if the end pulse never arrives
  if (receive_started_ && dist >= s.buffer_size - 5) {
    const uint32_t frame_duration_us = receive_start_time_ == 0 ? 0 : now - receive_start_time_;
    this->rejected_frames_++;
    ESP_LOGW(TAG, "RF frame rejected: RX buffer nearly full (dist=%u, rejected=%u)", dist, this->rejected_frames_);
    if (this->rejected_frames_ % 10 == 0) {
      this->log_frame_stats_("reject-buffer-full", dist, frame_duration_us);
    }
    receive_started_=false;
    #if 0
    uint32_t prev = s.buffer_read_at;
    s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
    int32_t multiplier = s.buffer_read_at % 2 == 0 ? 1 : -1;
    for (uint32_t i = 0; prev != write_at; i++) {
      int32_t delta = s.buffer[s.buffer_read_at] - s.buffer[prev];

      ESP_LOGVV(TAG, "  i=%u buffer[%u]=%u - buffer[%u]=%u -> %d", i, s.buffer_read_at, s.buffer[s.buffer_read_at], prev,
                s.buffer[prev], delta * multiplier);
      prev = s.buffer_read_at;
      s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
      multiplier *= -1;
    }
    #endif
    s.buffer_read_at = s.buffer_write_at;
    old_write_at_ = s.buffer_read_at;
    return;
  }

  // signals must at least one rising and one leading edge before scanning
  if (dist > 1) {
    //now we check all the available data in the buffer from the old position read
    while (new_write_at != write_at) {
      uint32_t prev;
      if (new_write_at==0) {
        prev=s.buffer_size-1;
      } else {
        prev=new_write_at-1;
      }
      uint32_t diff=s.buffer[new_write_at]-s.buffer[prev];
      //reception starts and ends with a pulse (transition to low, new value is 0)
      if (new_write_at % 2 == 0) {
        //check if it's a start or end pulse
        if (diff>=this->start_pulse_min_us_) {
          if (diff >= this->end_pulse_us_) {
            //it's a probable end pulse
            if (receive_started_) {
              receive_end=true;
              receive_end_time=s.buffer[new_write_at];
              end_marker_us=this->end_pulse_us_;
              end_reason="end pulse";
              new_write_at=prev;
              break;
            } else {
              ESP_LOGVV(TAG, "Not receiving, ignoring ending pulse (%u)",diff);
            }
          } else if (diff<this->start_pulse_max_us_) {
            //it's a new start pulse, discard old data and start again
            if (receive_started_) {
              const uint32_t candidate_pulses = this->candidate_pulse_count_(prev);
              const uint32_t candidate_duration_us = receive_start_time_ == 0 ? 0 : s.buffer[prev] - receive_start_time_;
              if (this->accept_on_restart_ && candidate_pulses >= this->min_pulses_ &&
                  candidate_pulses <= this->max_pulses_ &&
                  candidate_duration_us <= this->max_frame_duration_us_) {
                receive_end=true;
                receive_end_time=s.buffer[prev];
                end_marker_us=this->frame_gap_us_;
                end_reason="restart boundary";
                new_write_at=prev;
                break;
              }
              ESP_LOGVV(TAG, "RF frame restart: new start pulse=%u us", diff);
            } else {
              ESP_LOGVV(TAG, "RF frame start: pulse=%u us", diff);
            }
            s.buffer_read_at=prev;
            receive_started_=true;
            receive_start_time_=s.buffer[prev];
            receive_start_pulse_us_=diff;
          } else {
            ESP_LOGVV(TAG, "Starting pulse (%u) too long, ignored",diff);
          }
        }
      } else if (receive_started_ && diff>=this->max_pause_us_) {
        if (diff > this->max_frame_duration_us_) {
          const uint32_t frame_duration_us = receive_start_time_ == 0 ? 0 : now - receive_start_time_;
          this->rejected_frames_++;
          ESP_LOGW(TAG, "RF frame rejected: segment=%u us exceeds max_frame_duration=%u us (possible stale edge, rejected=%u)",
                   diff, this->max_frame_duration_us_, this->rejected_frames_);
          if (this->rejected_frames_ % 10 == 0) {
            this->log_frame_stats_("reject-stale-segment", dist, frame_duration_us);
          }
          receive_started_=false;
          s.buffer_read_at=write_at;
          old_write_at_=write_at;
          return;
        }
        if (diff>=this->frame_gap_us_) {
          receive_end=true;
          receive_end_time=s.buffer[new_write_at];
          end_marker_us=diff;
          end_reason="gap edge";
          new_write_at=prev;
          break;
        }
        this->rejected_frames_++;
        ESP_LOGD(TAG, "RF frame rejected: pause=%u us exceeds max_pause=%u us (rejected=%u)",
                 diff, this->max_pause_us_, this->rejected_frames_);
        if (this->rejected_frames_ % 10 == 0) {
          const uint32_t frame_duration_us = receive_start_time_ == 0 ? 0 : now - receive_start_time_;
          this->log_frame_stats_("reject-pause", dist, frame_duration_us);
        }
        receive_started_=false;
      }
      if (!receive_started_) {
        s.buffer_read_at=prev;
      }
      new_write_at = (new_write_at + 1) % s.buffer_size;
    }
    old_write_at_ = new_write_at;
  } else if (!receive_started_) {
    return;
  }

  if (!receive_end && receive_started_) {
    const uint32_t gap = now - s.buffer[write_at];
    const uint32_t next = (write_at + 1) % s.buffer_size;
    const bool open_segment_is_pulse = next % 2 == 0;
    if (open_segment_is_pulse && gap>=this->end_pulse_us_) {
      receive_end=true;
      receive_end_time=now;
      end_marker_us=this->end_pulse_us_;
      end_reason="end pulse timeout";
      new_write_at=write_at;
      old_write_at_=write_at;
    } else if (!open_segment_is_pulse && gap>=this->frame_gap_us_) {
      receive_end=true;
      receive_end_time=now;
      end_marker_us=gap;
      end_reason="gap timeout";
      new_write_at=write_at;
      old_write_at_=write_at;
    }
  }

  if (!receive_end) {
    return;
  }

  //here we have a supposedly valid sequence
  const uint32_t frame_duration_us = receive_start_time_ == 0 ? 0 : receive_end_time - receive_start_time_;

  receive_started_=false;

  ESP_LOGVV(TAG, "read_at=%u write_at=%u dist=%u now=%u end=%u", s.buffer_read_at, new_write_at, dist, now,
            s.buffer[new_write_at]);

  uint32_t prev = s.buffer_read_at;
  s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
  const uint32_t pulse_count = 1 + (s.buffer_size + new_write_at - s.buffer_read_at) % s.buffer_size;
  if (pulse_count < this->min_pulses_) {
    this->rejected_frames_++;
    ESP_LOGD(TAG, "RF frame rejected: pulses=%u below min_pulses=%u (start=%u us, end=%s/%u us, duration=%u us, rejected=%u)",
             pulse_count, this->min_pulses_, this->receive_start_pulse_us_, end_reason, end_marker_us,
             frame_duration_us, this->rejected_frames_);
    if (this->rejected_frames_ % 10 == 0) {
      this->log_frame_stats_("reject-short", pulse_count, frame_duration_us);
    }
    s.buffer_read_at = new_write_at;
    old_write_at_ = new_write_at;
    return;
  }
  if (pulse_count > this->max_pulses_) {
    this->rejected_frames_++;
    ESP_LOGD(TAG, "RF frame rejected: pulses=%u above max_pulses=%u (start=%u us, end=%s/%u us, duration=%u us, rejected=%u)",
             pulse_count, this->max_pulses_, this->receive_start_pulse_us_, end_reason, end_marker_us,
             frame_duration_us, this->rejected_frames_);
    if (this->rejected_frames_ % 10 == 0) {
      this->log_frame_stats_("reject-long", pulse_count, frame_duration_us);
    }
    s.buffer_read_at = new_write_at;
    old_write_at_ = new_write_at;
    return;
  }
  this->RemoteReceiverBase::temp_.clear();
  this->RemoteReceiverBase::temp_.reserve(pulse_count + 1);
  int32_t multiplier = s.buffer_read_at % 2 == 0 ? 1 : -1;

  for (uint32_t i = 0; prev != new_write_at; i++) {
    int32_t delta = s.buffer[s.buffer_read_at] - s.buffer[prev];

    ESP_LOGVV(TAG, "  i=%u buffer[%u]=%u - buffer[%u]=%u -> %d", i, s.buffer_read_at, s.buffer[s.buffer_read_at], prev,
              s.buffer[prev], multiplier * delta);
    this->RemoteReceiverBase::temp_.push_back(multiplier * delta);
    prev = s.buffer_read_at;
    s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
    multiplier *= -1;
  }
  s.buffer_read_at = (s.buffer_size + s.buffer_read_at - 1) % s.buffer_size;
  this->RemoteReceiverBase::temp_.push_back(end_marker_us * multiplier);

  if (this->dedupe_window_us_ > 0 && this->last_emit_time_ != 0 &&
      now - this->last_emit_time_ < this->dedupe_window_us_) {
    ESP_LOGD(TAG, "RF frame duplicate suppressed: pulses=%u, start=%u us, end=%s/%u us, duration=%u us, since_last=%u us",
             pulse_count, this->receive_start_pulse_us_, end_reason, end_marker_us, frame_duration_us,
             now - this->last_emit_time_);
    if (this->dump_suppressed_raw_) {
      this->log_raw_frame_("Suppressed Raw");
    }
    this->last_emit_time_ = now;
    return;
  }
  this->last_emit_time_ = now;

  this->received_frames_++;
  ESP_LOGD(TAG, "RF frame accepted #%u: pulses=%u, start=%u us, end=%s/%u us, duration=%u us, rejected=%u",
           this->received_frames_, pulse_count, this->receive_start_pulse_us_, end_reason, end_marker_us,
           frame_duration_us, this->rejected_frames_);
  this->log_frame_stats_("accept", pulse_count, frame_duration_us);
  if (this->single_raw_dump_) {
    this->log_raw_frame_();
  }

  this->call_listeners_dumpers_();
}

}  // namespace tuya_rf
}  // namespace esphome

#endif
