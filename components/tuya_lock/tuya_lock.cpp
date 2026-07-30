#include "tuya_lock.h"
#ifdef USE_ESP32
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstring>

namespace esphome {
namespace tuya_lock {

static const char *const TAG = "tuya_lock";

void TuyaLock::setup() {
  login_key_ok_ = proto::derive_login_key(local_key_, login_key_);
  if (!login_key_ok_)
    ESP_LOGE(TAG, "local_key too short (need >= 6 chars) — lock will not work");
}

void TuyaLock::dump_config() {
  ESP_LOGCONFIG(TAG, "Tuya Lock:");
  ESP_LOGCONFIG(TAG, "  device_id: %s", device_id_.c_str());
  ESP_LOGCONFIG(TAG, "  local_key valid: %s", YESNO(login_key_ok_));
}

void TuyaLock::reset_session_() {
  has_session_ = false;
  seq_ = 1;
  reasm_.reset();
  tx_queue_.clear();
  congested_ = false;
}

void TuyaLock::request_(TLAction a) {
  if (!login_key_ok_) {
    ESP_LOGW(TAG, "ignoring action: invalid local_key");
    return;
  }
  if (in_flight_) {
    ESP_LOGW(TAG, "action already in flight, ignoring");
    return;
  }
  ESP_LOGI(TAG, "action requested: %d", (int) a);
  pending_ = a;
  in_flight_ = true;
  reset_session_();
  // fail-safe: if the handshake never completes, recover instead of hanging forever
  this->set_timeout("handshake", TL_HANDSHAKE_TIMEOUT_MS,
                    [this]() { this->finish_(false, "handshake timeout"); });
  this->parent()->set_enabled(true);  // framework connects; flow continues in the event handler
}

void TuyaLock::finish_(bool success, const char *reason) {
  this->cancel_timeout("handshake");
  if (success) {
    ESP_LOGI(TAG, "done: %s", reason);
    this->success_trigger_.trigger();
  } else {
    ESP_LOGW(TAG, "failed: %s", reason);
    this->error_trigger_.trigger();
  }
  pending_ = TL_NONE;
  in_flight_ = false;
  action_done_msg_ = nullptr;
  reset_session_();
  // disconnect (non-blocking) to free the radio
  this->set_timeout("disconnect", 200, [this]() { this->parent()->set_enabled(false); });
}

void TuyaLock::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                   esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      if (param->open.status == ESP_GATT_OK)
        ESP_LOGD(TAG, "connected");
      break;

    case ESP_GATTC_DISCONNECT_EVT:
      write_handle_ = 0;
      notify_handle_ = 0;
      ESP_LOGD(TAG, "disconnected");
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      // Resolve write/notify chars by 16-bit UUID across ALL services (they live under a
      // vendor service, not a standard one).
      write_handle_ = 0;
      notify_handle_ = 0;
      uint16_t conn = this->parent()->get_conn_id();
      esp_gattc_service_elem_t svcs[16];
      uint16_t scount = 16;
      // NOTE: these cache accessors return esp_gatt_status_t (ESP_GATT_OK), not esp_err_t.
      if (esp_ble_gattc_get_service(gattc_if, conn, nullptr, svcs, &scount, 0) != ESP_GATT_OK) {
        finish_(false, "service discovery failed");
        break;
      }
      for (uint16_t s = 0; s < scount; s++) {
        esp_gattc_char_elem_t chs[16];
        uint16_t ccount = 16;
        if (esp_ble_gattc_get_all_char(gattc_if, conn, svcs[s].start_handle,
                                       svcs[s].end_handle, chs, &ccount, 0) != ESP_GATT_OK)
          continue;
        for (uint16_t c = 0; c < ccount; c++) {
          if (chs[c].uuid.len != ESP_UUID_LEN_16)
            continue;
          uint16_t u = chs[c].uuid.uuid.uuid16;
          if (u == TL_CHAR_WRITE)
            write_handle_ = chs[c].char_handle;
          else if (u == TL_CHAR_NOTIFY)
            notify_handle_ = chs[c].char_handle;
          ESP_LOGV(TAG, "char 0x%04X handle=%u props=0x%02X", u, chs[c].char_handle,
                   (unsigned) chs[c].properties);
        }
      }
      ESP_LOGD(TAG, "chars: write=%u notify=%u", write_handle_, notify_handle_);
      if (!write_handle_ || !notify_handle_) {
        finish_(false, "lock characteristics not found");
        break;
      }
      esp_ble_gattc_register_for_notify(gattc_if, this->parent()->get_remote_bda(),
                                        notify_handle_);
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      // notifications on -> start the handshake with an (empty) device-info request
      reasm_.reset();
      queue_message_(proto::SEC_LOGIN, proto::CODE_DEVICE_INFO, {});
      break;

    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle == notify_handle_)
        on_notify_(param->notify.value, param->notify.value_len);
      break;

    case ESP_GATTC_WRITE_CHAR_EVT:
    case ESP_GATTC_CONGEST_EVT:
      // congestion cleared / a write completed -> continue draining the queue
      if (event == ESP_GATTC_CONGEST_EVT)
        congested_ = param->congest.congested;
      pump_tx_();
      break;

    default:
      break;
  }
}

// ---- non-blocking send ----
void TuyaLock::queue_message_(uint8_t sec_flag, uint16_t code, const std::vector<uint8_t> &data) {
  const uint8_t *key = (sec_flag == proto::SEC_LOGIN) ? login_key_ : session_key_;
  // IV only needs to be unique per frame (it is sent in the clear); derive from time+seq.
  uint32_t seq = seq_++;
  uint8_t seed[8], iv[16];
  uint32_t t = millis();
  std::memcpy(seed, &t, 4);
  std::memcpy(seed + 4, &seq, 4);
  proto::md5(seed, sizeof(seed), iv);

  auto packets = proto::build_packets(sec_flag, key, iv, seq, code, data);
  for (auto &p : packets)
    tx_queue_.push_back(std::move(p));
  pump_tx_();
}

void TuyaLock::pump_tx_() {
  // Drain the queue non-blockingly. Writes are write-WITHOUT-response, which the controller
  // buffers; for our tiny handshake payloads (a few <=20-byte packets) sending them
  // back-to-back is what works in practice. We stop early only on real back-pressure:
  //   - ESP_ERR_INVALID_STATE -> the link is down; drop out (handshake timeout will recover)
  //   - CONGEST_EVT(true) sets congested_ -> we pause; CONGEST_EVT(false) resumes via pump_tx_
  if (congested_ || !write_handle_)
    return;
  while (!tx_queue_.empty()) {
    esp_err_t err = esp_ble_gattc_write_char(this->parent()->get_gattc_if(),
                                             this->parent()->get_conn_id(), write_handle_,
                                             tx_queue_.front().size(), tx_queue_.front().data(),
                                             ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    if (err == ESP_OK) {
      tx_queue_.pop_front();
      continue;
    }
    if (err == ESP_ERR_INVALID_STATE) {
      ESP_LOGW(TAG, "write: connection not established; aborting send");
      return;  // link gone — the handshake timeout will clean up
    }
    // transient (e.g. controller buffer full) — pause and let CONGEST_EVT resume us
    ESP_LOGD(TAG, "write busy (%d); pausing queue until congestion clears", err);
    congested_ = true;
    return;
  }
  // queue fully flushed — if an action was waiting on this, complete it now
  finish_when_flushed_();
}

// ---- receive ----
void TuyaLock::on_notify_(const uint8_t *data, uint16_t len) {
  std::vector<uint8_t> frame;
  if (reasm_.feed(data, len, frame))
    handle_frame_(frame);
}

void TuyaLock::handle_frame_(const std::vector<uint8_t> &frame) {
  auto d = proto::decode_frame(frame, login_key_, session_key_);
  if (!d.ok) {
    ESP_LOGD(TAG, "ignoring undecodable frame");
    return;
  }
  ESP_LOGD(TAG, "frame code=0x%04X len=%u", d.code, (unsigned) d.data.size());

  if (d.code == proto::CODE_DEVICE_INFO && d.data.size() >= 12) {
    uint8_t srand[6];
    std::memcpy(srand, &d.data[6], 6);  // srand = device_info[6:12]
    if (!proto::derive_session_key(local_key_, srand, session_key_)) {
      finish_(false, "session key derivation failed");
      return;
    }
    has_session_ = true;
    ESP_LOGD(TAG, "session established -> pairing");
    send_pair_();
  } else if (d.code == proto::CODE_PAIR) {
    ESP_LOGD(TAG, "paired -> sending action");
    send_action_();
  }
}

void TuyaLock::send_pair_() {
  queue_message_(proto::SEC_SESSION, proto::CODE_PAIR,
                 proto::build_pair_payload(uuid_, local_key_, device_id_));
}

void TuyaLock::send_action_() {
  const char *label;
  if (pending_ == TL_UNLOCK) {
    // The lock only checks the timestamp is fresh/monotonic, not real wall-clock time, so a
    // large monotonic value derived from uptime is fine. (Jumps back once every ~49.7 days
    // when millis() wraps — harmless; the lock accepts any newer-than-last value.)
    uint32_t ts = (uint32_t) (millis() / 1000) + 1700000000u;
    queue_message_(proto::SEC_SESSION, proto::CODE_DPS, proto::build_unlock_dp(passcode_, ts));
    label = "unlock sent";
  } else if (pending_ == TL_LOCK) {
    queue_message_(proto::SEC_SESSION, proto::CODE_DPS, proto::build_lock_dp());
    label = "lock sent";
  } else {  // TL_STATUS
    queue_message_(proto::SEC_SESSION, proto::CODE_DEVICE_STATUS, {});
    label = "status requested";
  }
  // Only report success + tear down once the action bytes have actually left the queue.
  // (If congestion paused the send, wait; the handshake timeout is the backstop.)
  action_done_msg_ = label;
  finish_when_flushed_();
}

void TuyaLock::finish_when_flushed_() {
  if (in_flight_ && action_done_msg_ != nullptr && tx_queue_.empty()) {
    const char *msg = action_done_msg_;
    action_done_msg_ = nullptr;
    finish_(true, msg);
  }
}

}  // namespace tuya_lock
}  // namespace esphome
#endif
