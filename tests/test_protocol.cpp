// Unit tests for the pure Tuya BLE protocol layer.
// Vectors are SYNTHETIC (a fabricated device-info frame built with a fake key) so no real
// credentials appear here — but they exercise the exact same code paths and framing as the
// real lock. Run: `make test` (see tests/README.md). No ESP / no external crypto needed.

#include "../components/tuya_lock/tuya_protocol.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace esphome::tuya_lock::proto;

static int g_checks = 0, g_fails = 0;

static std::string hex(const std::vector<uint8_t> &v) {
  static const char *H = "0123456789abcdef";
  std::string s;
  for (auto b : v) { s += H[b >> 4]; s += H[b & 0xF]; }
  return s;
}
static std::string hex(const uint8_t *v, size_t n) {
  return hex(std::vector<uint8_t>(v, v + n));
}
static std::vector<uint8_t> unhex(const std::string &s) {
  std::vector<uint8_t> v;
  for (size_t i = 0; i + 1 < s.size(); i += 2)
    v.push_back((uint8_t) std::stoi(s.substr(i, 2), nullptr, 16));
  return v;
}

#define CHECK_EQ(actual, expected, name)                                         \
  do {                                                                           \
    g_checks++;                                                                  \
    if ((actual) != (expected)) {                                               \
      g_fails++;                                                                  \
      std::printf("FAIL %s\n  got:      %s\n  expected: %s\n", name,             \
                  std::string(actual).c_str(), std::string(expected).c_str());  \
    } else {                                                                     \
      std::printf("ok   %s\n", name);                                            \
    }                                                                            \
  } while (0)

#define CHECK_TRUE(cond, name)                                                   \
  do { g_checks++; if (!(cond)) { g_fails++; std::printf("FAIL %s\n", name); }   \
       else std::printf("ok   %s\n", name); } while (0)

// SYNTHETIC test credentials — fabricated, not a real device. Expected values below were
// computed from these with a reference implementation.
static const std::string LOCAL_KEY = "ABCDEF0123456789";
static const std::string UUID = "00112233aabbccdd";
static const std::string DEVICE_ID = "test_device_0001";
static const std::string PASSCODE = "00000000";

int main() {
  // --- crc16 ---
  {
    const char *s = "123456789";
    CHECK_TRUE(crc16((const uint8_t *) s, 9) == 0x4B37, "crc16('123456789')==0x4B37");
    CHECK_TRUE(crc16(nullptr, 0) == 0xFFFF, "crc16('')==0xFFFF");
  }

  // --- key derivation (verified against the real lock) ---
  {
    uint8_t login[16];
    CHECK_TRUE(derive_login_key(LOCAL_KEY, login), "derive_login_key ok");
    CHECK_EQ(hex(login, 16), "8827a41122a5028b9808c7bf84b9fcf6", "login_key value");
    CHECK_TRUE(!derive_login_key("short", login), "derive_login_key rejects <6 chars");

    uint8_t srand[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t sess[16];
    CHECK_TRUE(derive_session_key(LOCAL_KEY, srand, sess), "derive_session_key ok");
    CHECK_EQ(hex(sess, 16), "84968a8ed0a95020633483481152e74e", "session_key value");
  }

  // --- decode a device-info reply frame (synthetic, but exact Tuya format) ---
  {
    auto frame = unhex(
        "04000102030405060708090a0b0c0d0e0ff83d4f5b7111d8b01a75b6ba4cec037965186352d474a89"
        "d4140609357b7e1b12ddb91ae4a77a95a9932d75caae43de9");
    uint8_t login[16], sess[16] = {0};
    derive_login_key(LOCAL_KEY, login);
    auto d = decode_frame(frame, login, sess);
    CHECK_TRUE(d.ok, "decode_frame(device-info) ok (CRC valid)");
    CHECK_TRUE(d.sec_flag == 0x04, "device-info sec_flag==0x04");
    CHECK_TRUE(d.code == CODE_DEVICE_INFO, "device-info code==0x0000");
    CHECK_TRUE(d.data.size() == 24, "device-info dlen==24");
    // srand = data[6:12]
    CHECK_EQ(hex(std::vector<uint8_t>(d.data.begin() + 6, d.data.begin() + 12)),
             "112233445566", "srand extracted from device-info");

    // wrong key -> CRC mismatch -> rejected
    uint8_t wrong[16] = {0};
    CHECK_TRUE(!decode_frame(frame, wrong, sess).ok, "decode_frame rejects wrong key");
  }

  // --- payload builders (exact bytes) ---
  {
    CHECK_EQ(hex(build_unlock_dp(PASSCODE, 0x11223344)),
             "470013ffff0001303030303030303001112233440001", "build_unlock_dp bytes");
    CHECK_EQ(hex(build_lock_dp()), "2e010101", "build_lock_dp bytes");
    CHECK_EQ(hex(build_pair_payload(UUID, LOCAL_KEY, DEVICE_ID)),
             "30303131323233336161626263636464414243444546746573745f6465766963655f30303031"
             "000000000000",
             "build_pair_payload bytes (44, zero-padded)");
  }

  // --- build_packets round-trips through decode_frame ---
  {
    uint8_t login[16];
    derive_login_key(LOCAL_KEY, login);
    uint8_t iv[16];
    for (int i = 0; i < 16; i++) iv[i] = (uint8_t) (i * 7 + 1);  // deterministic IV
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
    auto packets = build_packets(SEC_LOGIN, login, iv, 42, CODE_DPS, data);
    // reassemble
    Reassembler r;
    std::vector<uint8_t> frame;
    bool done = false;
    for (auto &p : packets)
      done = r.feed(p.data(), p.size(), frame) || done;
    CHECK_TRUE(done, "build_packets -> reassembler completes");
    uint8_t sess[16] = {0};
    auto d = decode_frame(frame, login, sess);
    CHECK_TRUE(d.ok, "round-trip decode ok");
    CHECK_TRUE(d.seq == 42 && d.code == CODE_DPS && d.data == data, "round-trip fields match");
  }

  // --- reassembler safety on hostile input ---
  {
    Reassembler r;
    std::vector<uint8_t> out;
    // all-continuation varint (0x80...) must not read past the buffer / must fail safely
    std::vector<uint8_t> nasty(64, 0x80);
    CHECK_TRUE(!r.feed(nasty.data(), nasty.size(), out), "reassembler rejects runaway varint");
    // absurd declared length must be rejected (pkt0: pnum=0, len=varint(2000000), proto)
    std::vector<uint8_t> big = {0x00};
    // varint for 2,000,000
    uint32_t v = 2000000; while (v) { uint8_t b = v & 0x7F; v >>= 7; if (v) b |= 0x80; big.push_back(b); }
    big.push_back(0x30);
    big.push_back(0xAA);
    CHECK_TRUE(!r.feed(big.data(), big.size(), out), "reassembler rejects absurd length");
  }

  std::printf("\n%d checks, %d failures\n", g_checks, g_fails);
  return g_fails ? 1 : 0;
}
