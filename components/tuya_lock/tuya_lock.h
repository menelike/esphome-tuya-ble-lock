#pragma once
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/defines.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "tuya_protocol.h"
#include <deque>
#include <vector>
#include <string>

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#ifdef USE_ESP32

namespace esphome {
namespace tuya_lock {

// GATT characteristics we talk to. On the ZX-5330 these live under vendor service 0x1910,
// but we resolve them by characteristic UUID across all services, so the service doesn't
// matter.
static const uint16_t TL_CHAR_WRITE = 0x2B11;
static const uint16_t TL_CHAR_NOTIFY = 0x2B10;

// Two-phase timeout, split so the retry can NEVER re-send an action that may already have been
// sent:
//   * DISCOVERY — armed until the BLE link opens (ESP_GATTC_OPEN_EVT). If it expires we
//     genuinely never connected, so no action byte can have left the device — it is SAFE to
//     tear down and retry once. Kept short so a cold miss recovers quickly at the door.
//   * HANDSHAKE — armed only AFTER the link is open, for the encrypted handshake + action. If
//     it expires we may already have connected and sent the unlock, so we FAIL (no retry)
//     rather than risk firing unlock a second time.
static const uint32_t TL_DISCOVERY_TIMEOUT_MS = 8000;
static const uint32_t TL_HANDSHAKE_TIMEOUT_MS = 12000;

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
  // Do one status read shortly after boot (on by default). Purpose: the FIRST cold connect is
  // the worst case (fresh BLE stack + sleepy lock mid-advertising-gap → most likely to need the
  // discovery retry), so spend it here, at boot, when nobody is waiting — the user's first
  // button press at the door then connects on the first try instead of eating that penalty. Also
  // fills the battery / last-status entities immediately. Set false for strictly on-demand.
  void set_status_on_boot(bool b) { status_on_boot_ = b; }

  // triggered from Home Assistant buttons
  void unlock() { request_(TL_UNLOCK); }
  void lock() { request_(TL_LOCK); }
  void status() { request_(TL_STATUS); }

  // Home Assistant can hook these. The success/error triggers carry a string:
  //   - success of unlock/lock: the action name ("unlock" / "lock")
  //   - success of status: "status:<space-separated DPid=value ...>" (battery is DP 8)
  //   - error: a short reason
  // This lets an on_success automation forward the value to a Home Assistant event -> logbook.
  Trigger<std::string> *get_success_trigger() { return &success_trigger_; }
  Trigger<std::string> *get_error_trigger() { return &error_trigger_; }

  // optional battery sensor (registered by the sensor platform)
#ifdef USE_SENSOR
  void set_battery_sensor(sensor::Sensor *s) { battery_sensor_ = s; }
#endif
  // optional "last status" text sensor (registered by the text_sensor platform):
  // a summary published on each status read, e.g. "battery 70% · @342s".
#ifdef USE_TEXT_SENSOR
  void set_status_text_sensor(text_sensor::TextSensor *s) { status_text_sensor_ = s; }
#endif

 protected:
  void request_(TLAction a);
  void start_attempt_();  // (re)start discover+connect+handshake for the pending action
  void reset_session_();
  void finish_(bool success, const char *reason);
  void finish_when_flushed_();
  void queue_message_(uint8_t sec_flag, uint16_t code, const std::vector<uint8_t> &data);
  void pump_tx_();
  void on_notify_(const uint8_t *data, uint16_t len);
  void handle_frame_(const std::vector<uint8_t> &frame);
  void publish_status_(const std::vector<uint8_t> &dp_body);
  void finish_status_();  // called after DP frames settle: build summary + finish
  void send_pair_();
  void send_action_();

  std::string local_key_, uuid_, device_id_, passcode_;
  bool status_on_boot_{false};
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
  bool retried_{false};  // one automatic retry on connect/handshake timeout (BLE stack warmup)
  const char *action_done_msg_{nullptr};  // set once the action is queued; success on flush

  Trigger<std::string> success_trigger_;
  Trigger<std::string> error_trigger_;
  std::string status_result_;  // last status DP dump, forwarded via on_success

  // The lock streams its status DPs across SEVERAL frames (battery, lock state, door — each in
  // its own frame). We accumulate them and finish once, after a short settle window, so the
  // summary/battery reflect the WHOLE reply rather than just the first frame.
  std::string status_dump_;         // raw "id=value ..." accumulated across frames (for logging)
  bool have_battery_{false};
  int battery_pct_{0};

#ifdef USE_SENSOR
  sensor::Sensor *battery_sensor_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *status_text_sensor_{nullptr};
#endif
};

}  // namespace tuya_lock
}  // namespace esphome

#endif
