# Unit tests

Host-side tests for the pure protocol layer (`components/tuya_lock/tuya_protocol.{h,cpp}`) —
crypto, framing, decode, reassembly, and payload builders. They assert against **real bytes
captured from a Tuya ZX-5330 lock**, so they lock in the exact on-wire behaviour that works.

No ESP hardware and no external crypto library are needed — a minimal MD5 + AES-128 is
vendored in `host_crypto.cpp` for the host build only. (On-device, the same protocol code
links against ESP-IDF's mbedTLS.)

## Run

```
make test
```

Expected: `21 checks, 0 failures`.

## What's covered

- CRC16 (known vectors)
- `login_key` / `session_key` derivation (exact values from the real lock)
- Decoding the real device-info reply frame (CRC-verified) + srand extraction
- Rejecting frames that fail CRC (wrong key)
- Exact unlock (DP71) / lock (DP46) / pair payload bytes
- `build_packets` → `Reassembler` → `decode_frame` round-trip
- Reassembler safety on hostile input (runaway varints, absurd declared lengths)
