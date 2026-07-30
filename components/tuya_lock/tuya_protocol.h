// Pure Tuya BLE protocol logic — NO ESPHome / NO BLE dependencies.
// Everything here is deterministic and unit-testable on a host (see tests/).
// Crypto uses mbedTLS (available both on-device and on the host for tests).
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace esphome {
namespace tuya_lock {
namespace proto {

// Tuya opcodes / DP ids used by the payload builders.
static const uint16_t CODE_DEVICE_INFO = 0x0000;
static const uint16_t CODE_PAIR = 0x0001;
static const uint16_t CODE_DPS = 0x0002;
static const uint16_t CODE_DEVICE_STATUS = 0x0003;
static const uint8_t SEC_LOGIN = 0x04;
static const uint8_t SEC_SESSION = 0x05;
static const uint8_t TL_DP_UNLOCK_ID = 71;  // ble_unlock_check
static const uint8_t TL_DP_LOCK_ID = 46;    // manual_lock

// ---- primitives ----
void md5(const uint8_t *in, size_t len, uint8_t out[16]);
void aes128_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16],
                        const uint8_t *in, size_t n, uint8_t *out);
void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                        const uint8_t *in, size_t n, uint8_t *out);
uint16_t crc16(const uint8_t *d, size_t n);  // poly 0xA001, init 0xFFFF

// login_key = md5(local_key[:6]); returns false if local_key too short.
bool derive_login_key(const std::string &local_key, uint8_t out[16]);
// session_key = md5(local_key[:6] + srand)
bool derive_session_key(const std::string &local_key, const uint8_t srand[6], uint8_t out[16]);

// ---- framing ----
// Build the encrypted transport packets for one Tuya message.
//   inner  = [seq:4 BE][resp:4 BE][code:2 BE][len:2 BE][data][crc16][zero-pad to 16]
//   frame  = [sec_flag:1][iv:16][AES-CBC(inner)]
//   packet = [varint pkt_num]([varint total_len][proto<<4] if pkt 0)[<=chunk bytes of frame]
// `iv` is caller-supplied (16 bytes) so tests are deterministic.
std::vector<std::vector<uint8_t>> build_packets(uint8_t sec_flag, const uint8_t key[16],
                                                const uint8_t iv[16], uint32_t seq,
                                                uint16_t code, const std::vector<uint8_t> &data,
                                                size_t chunk = 20, uint8_t proto = 3);

// Decode a reassembled frame ([sec_flag][iv:16][ciphertext]).
struct DecodedFrame {
  bool ok{false};
  uint8_t sec_flag{0};
  uint32_t seq{0};
  uint32_t response_to{0};
  uint16_t code{0};
  std::vector<uint8_t> data;
};
// key_login/key_session select by the frame's sec_flag. Verifies CRC16. `ok=false` on any
// malformed / CRC-mismatch input (safe on attacker-controlled bytes).
DecodedFrame decode_frame(const std::vector<uint8_t> &frame,
                          const uint8_t key_login[16], const uint8_t key_session[16]);

// ---- reassembly ----
// Feeds one BLE notification chunk; accumulates until a full frame is ready.
// Bounded and safe against malformed/hostile input.
class Reassembler {
 public:
  // Returns true and fills `out` when a complete frame is assembled; else false.
  bool feed(const uint8_t *data, size_t len, std::vector<uint8_t> &out);
  void reset() { buf_.clear(); expected_ = 0; }
 private:
  std::vector<uint8_t> buf_;
  uint32_t expected_{0};
  static constexpr uint32_t MAX_FRAME = 1024;
};

// ---- payloads ----
// Pairing request: uuid + local_key[:6] + device_id, zero-padded to 44 bytes.
std::vector<uint8_t> build_pair_payload(const std::string &uuid, const std::string &local_key,
                                        const std::string &device_id);
// Unlock DP71: ffff0001 + passcode + 01 + <ts:4 BE> + 0001, wrapped as [id][type][len][value].
std::vector<uint8_t> build_unlock_dp(const std::string &passcode, uint32_t timestamp);
// Lock DP46 bool true: 2e 01 01 01.
std::vector<uint8_t> build_lock_dp();

}  // namespace proto
}  // namespace tuya_lock
}  // namespace esphome
