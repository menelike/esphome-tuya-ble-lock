#include "tuya_protocol.h"
#include "mbedtls/md5.h"
#include "mbedtls/aes.h"
#include <cstring>

namespace esphome {
namespace tuya_lock {
namespace proto {

// ---- primitives ----
void md5(const uint8_t *in, size_t len, uint8_t out[16]) {
  mbedtls_md5_context c;
  mbedtls_md5_init(&c);
  mbedtls_md5_starts(&c);
  mbedtls_md5_update(&c, in, len);
  mbedtls_md5_finish(&c, out);
  mbedtls_md5_free(&c);
}

void aes128_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t *in,
                        size_t n, uint8_t *out) {
  mbedtls_aes_context c;
  mbedtls_aes_init(&c);
  mbedtls_aes_setkey_enc(&c, key, 128);
  uint8_t ivc[16];
  std::memcpy(ivc, iv, 16);
  mbedtls_aes_crypt_cbc(&c, MBEDTLS_AES_ENCRYPT, n, ivc, in, out);
  mbedtls_aes_free(&c);
}

void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t *in,
                        size_t n, uint8_t *out) {
  mbedtls_aes_context c;
  mbedtls_aes_init(&c);
  mbedtls_aes_setkey_dec(&c, key, 128);
  uint8_t ivc[16];
  std::memcpy(ivc, iv, 16);
  mbedtls_aes_crypt_cbc(&c, MBEDTLS_AES_DECRYPT, n, ivc, in, out);
  mbedtls_aes_free(&c);
}

uint16_t crc16(const uint8_t *d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 1) ? (uint16_t) ((crc >> 1) ^ 0xA001) : (uint16_t) (crc >> 1);
  }
  return crc;
}

bool derive_login_key(const std::string &local_key, uint8_t out[16]) {
  if (local_key.size() < 6)
    return false;
  md5(reinterpret_cast<const uint8_t *>(local_key.data()), 6, out);
  return true;
}

bool derive_session_key(const std::string &local_key, const uint8_t srand[6], uint8_t out[16]) {
  if (local_key.size() < 6)
    return false;
  uint8_t buf[12];
  std::memcpy(buf, local_key.data(), 6);
  std::memcpy(buf + 6, srand, 6);
  md5(buf, sizeof(buf), out);
  return true;
}

// ---- varints (Tuya "pack_int": 7 bits/byte, LSB first, high bit = continue) ----
static void write_varint(uint32_t v, std::vector<uint8_t> &out) {
  while (true) {
    uint8_t b = v & 0x7F;
    v >>= 7;
    if (v)
      b |= 0x80;
    out.push_back(b);
    if (!v)
      break;
  }
}

// Reads a varint from data[*pos..len). Returns false (and leaves *pos) if truncated.
static bool read_varint(const uint8_t *data, size_t len, size_t *pos, uint32_t *out) {
  uint32_t r = 0;
  int shift = 0;
  size_t p = *pos;
  while (true) {
    if (p >= len || shift > 28)  // truncated, or would overflow 32 bits
      return false;
    uint8_t b = data[p++];
    r |= (uint32_t) (b & 0x7F) << shift;
    if (!(b & 0x80))
      break;
    shift += 7;
  }
  *pos = p;
  *out = r;
  return true;
}

// ---- framing ----
std::vector<std::vector<uint8_t>> build_packets(uint8_t sec_flag, const uint8_t key[16],
                                                const uint8_t iv[16], uint32_t seq,
                                                uint16_t code, const std::vector<uint8_t> &data,
                                                size_t chunk, uint8_t proto) {
  // inner frame
  std::vector<uint8_t> raw;
  auto p32 = [&](uint32_t v) {
    raw.push_back(v >> 24); raw.push_back(v >> 16); raw.push_back(v >> 8); raw.push_back(v);
  };
  auto p16 = [&](uint16_t v) { raw.push_back(v >> 8); raw.push_back(v); };
  p32(seq);
  p32(0);  // response_to
  p16(code);
  p16((uint16_t) data.size());
  raw.insert(raw.end(), data.begin(), data.end());
  p16(crc16(raw.data(), raw.size()));
  while (raw.size() % 16)
    raw.push_back(0);

  // encrypt
  std::vector<uint8_t> ct(raw.size());
  aes128_cbc_encrypt(key, iv, raw.data(), raw.size(), ct.data());

  // frame = [sec_flag][iv][ct]
  std::vector<uint8_t> enc;
  enc.reserve(1 + 16 + ct.size());
  enc.push_back(sec_flag);
  enc.insert(enc.end(), iv, iv + 16);
  enc.insert(enc.end(), ct.begin(), ct.end());

  // chunk into GATT packets
  std::vector<std::vector<uint8_t>> packets;
  size_t pos = 0;
  uint32_t pnum = 0;
  while (pos < enc.size()) {
    std::vector<uint8_t> pk;
    write_varint(pnum, pk);
    if (pnum == 0) {
      write_varint((uint32_t) enc.size(), pk);
      pk.push_back((uint8_t) (proto << 4));
    }
    size_t take = (chunk > pk.size()) ? chunk - pk.size() : 0;
    if (take == 0)
      break;  // chunk too small to make progress — guard against an infinite loop
    if (take > enc.size() - pos)
      take = enc.size() - pos;
    pk.insert(pk.end(), enc.begin() + pos, enc.begin() + pos + take);
    packets.push_back(std::move(pk));
    pos += take;
    pnum++;
  }
  return packets;
}

DecodedFrame decode_frame(const std::vector<uint8_t> &frame, const uint8_t key_login[16],
                          const uint8_t key_session[16]) {
  DecodedFrame r;
  if (frame.size() < 1 + 16 + 16)  // sec_flag + iv + at least one cipher block
    return r;
  uint8_t sec = frame[0];
  const uint8_t *key = (sec == SEC_LOGIN) ? key_login : (sec == SEC_SESSION) ? key_session : nullptr;
  if (key == nullptr)
    return r;
  size_t ct_len = frame.size() - 17;
  ct_len -= ct_len % 16;
  if (ct_len == 0)
    return r;

  uint8_t iv[16];
  std::memcpy(iv, &frame[1], 16);
  std::vector<uint8_t> pt(ct_len);
  aes128_cbc_decrypt(key, iv, &frame[17], ct_len, pt.data());

  if (pt.size() < 12)
    return r;
  uint16_t code = (pt[8] << 8) | pt[9];
  uint16_t dlen = (pt[10] << 8) | pt[11];
  size_t end = (size_t) 12 + dlen;
  if (end + 2 > pt.size())  // need data + 2-byte CRC
    return r;
  uint16_t want = (pt[end] << 8) | pt[end + 1];
  if (crc16(pt.data(), end) != want)  // reject bad key / corrupt frame
    return r;

  r.ok = true;
  r.sec_flag = sec;
  r.seq = ((uint32_t) pt[0] << 24) | (pt[1] << 16) | (pt[2] << 8) | pt[3];
  r.response_to = ((uint32_t) pt[4] << 24) | (pt[5] << 16) | (pt[6] << 8) | pt[7];
  r.code = code;
  r.data.assign(pt.begin() + 12, pt.begin() + end);
  return r;
}

// ---- reassembly ----
bool Reassembler::feed(const uint8_t *data, size_t len, std::vector<uint8_t> &out) {
  size_t pos = 0;
  uint32_t pnum = 0;
  if (!read_varint(data, len, &pos, &pnum))
    return false;
  if (pnum == 0) {
    buf_.clear();
    if (!read_varint(data, len, &pos, &expected_)) {
      expected_ = 0;
      return false;
    }
    if (expected_ == 0 || expected_ > MAX_FRAME) {  // reject absurd/hostile length
      expected_ = 0;
      return false;
    }
    if (pos >= len) {  // no room for the proto byte
      expected_ = 0;
      return false;
    }
    pos += 1;  // skip proto<<4 byte
  } else if (expected_ == 0) {
    return false;  // continuation with no header seen -> ignore
  }
  for (size_t i = pos; i < len; i++) {
    if (buf_.size() >= expected_)
      break;  // never overrun the declared length
    buf_.push_back(data[i]);
  }
  if (expected_ && buf_.size() >= expected_) {
    out = buf_;
    buf_.clear();
    expected_ = 0;
    return true;
  }
  return false;
}

// ---- payloads ----
std::vector<uint8_t> build_pair_payload(const std::string &uuid, const std::string &local_key,
                                        const std::string &device_id) {
  std::vector<uint8_t> pr;
  pr.insert(pr.end(), uuid.begin(), uuid.end());
  pr.insert(pr.end(), local_key.begin(), local_key.begin() + (local_key.size() < 6 ? local_key.size() : 6));
  pr.insert(pr.end(), device_id.begin(), device_id.end());
  while (pr.size() < 44)
    pr.push_back(0);
  pr.resize(44);
  return pr;
}

std::vector<uint8_t> build_unlock_dp(const std::string &passcode, uint32_t timestamp) {
  std::vector<uint8_t> pl = {0xFF, 0xFF, 0x00, 0x01};
  pl.insert(pl.end(), passcode.begin(), passcode.end());
  pl.push_back(0x01);
  pl.push_back(timestamp >> 24);
  pl.push_back(timestamp >> 16);
  pl.push_back(timestamp >> 8);
  pl.push_back(timestamp);
  pl.push_back(0x00);
  pl.push_back(0x01);
  std::vector<uint8_t> dp = {TL_DP_UNLOCK_ID, 0x00, (uint8_t) pl.size()};
  dp.insert(dp.end(), pl.begin(), pl.end());
  return dp;
}

std::vector<uint8_t> build_lock_dp() {
  return {TL_DP_LOCK_ID, 0x01, 0x01, 0x01};
}

// ---- status parsing ----
uint32_t DataPoint::as_int() const {
  uint32_t v = 0;
  for (uint8_t b : value)
    v = (v << 8) | b;  // big-endian
  return v;
}

std::vector<DataPoint> parse_datapoints(const std::vector<uint8_t> &data) {
  std::vector<DataPoint> out;
  size_t i = 0;
  while (i + 3 <= data.size()) {
    DataPoint dp;
    dp.id = data[i];
    dp.type = data[i + 1];
    uint8_t len = data[i + 2];
    if (i + 3 + len > data.size())
      break;  // truncated — stop safely
    dp.value.assign(data.begin() + i + 3, data.begin() + i + 3 + len);
    out.push_back(std::move(dp));
    i += 3 + len;
  }
  return out;
}

}  // namespace proto
}  // namespace tuya_lock
}  // namespace esphome
