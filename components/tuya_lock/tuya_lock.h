#pragma once
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "tuya_protocol.h"
#include <deque>
#include <vector>
#include <string>

#ifdef USE_ESP32

namespace esphome {
namespace tuya_lock {

namespace espbt = esphome::esp32_ble_tracker;

// GATT characteristics we talk to. On the ZX-5330 these live under vendor service 0x1910,
// but we resolve them by characteristic UUID across all services, so the service doesn't
// matter.
static const uint16_t TL_CHAR_WRITE = 0x2B11;
static const uint16_t TL_CHAR_NOTIFY = 0x2B10;

static const uint32_t TL_HANDSHAKE_TIMEOUT_MS = 15000;

enum TLAction { TL_NONE, TL_UNLOCK, TL_LOCK, TL_STATUS };

class TuyaLock : public Component, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_local_key(const std::string &k) { local_key_ = k; }
  void set_uuid(const std::string &u) { uuid_ = u; }
  void set_device_id(const std::string &d) { device_id_ = d; }
  void set_passcode(const std::string &p) { passcode_ = p; }

  // triggered from HA buttons
  void unlock() { request_(TL_UNLOCK); }
  void lock() { request_(TL_LOCK); }
  void status() { request_(TL_STATUS); }

  // HA can hook these (on_success / on_error automations)
  Trigger<> *get_success_trigger() { return &success_trigger_; }
  Trigger<> *get_error_trigger() { return &error_trigger_; }

 protected:
  void request_(TLAction a);
  void reset_session_();
  void finish_(bool success, const char *reason);
  void finish_when_flushed_();
  void queue_message_(uint8_t sec_flag, uint16_t code, const std::vector<uint8_t> &data);
  void pump_tx_();
  void on_notify_(const uint8_t *data, uint16_t len);
  void handle_frame_(const std::vector<uint8_t> &frame);
  void send_pair_();
  void send_action_();

  std::string local_key_, uuid_, device_id_, passcode_;
  uint8_t login_key_[16]{}, session_key_[16]{};
  bool login_key_ok_{false}, has_session_{false};
  uint32_t seq_{1};
  uint16_t write_handle_{0}, notify_handle_{0};

  proto::Reassembler reasm_;

  // non-blocking multi-packet send queue, drained as the controller allows
  std::deque<std::vector<uint8_t>> tx_queue_;
  bool congested_{false};

  TLAction pending_{TL_NONE};
  bool in_flight_{false};
  const char *action_done_msg_{nullptr};  // set once the action is queued; success on flush

  Trigger<> success_trigger_;
  Trigger<> error_trigger_;
};

}  // namespace tuya_lock
}  // namespace esphome

#endif
