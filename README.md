# esphome-tuya-ble-lock

[![tests](https://github.com/menelike/esphome-tuya-ble-lock/actions/workflows/tests.yml/badge.svg)](https://github.com/menelike/esphome-tuya-ble-lock/actions/workflows/tests.yml)

**Local, cloud-free ESPHome control of the Tuya ZX-5330/-5377 BLE smart lock — the ESP32 runs the full unlock handshake on-device (no cloud, no phone, no gateway).**

An [ESPHome](https://esphome.io) external component for the **Tuya ZX-5330/-5377 Bluetooth (BLE) smart lock** (`jtmspro` category). It controls the lock **fully locally**: the entire encrypted Tuya handshake, pairing, and unlock run **on the ESP32 itself**, exposed to Home Assistant as `unlock` / `lock` / `status` buttons.

No cloud. No phone. No Tuya gateway.

## Scope

The goal of this project is — and always will be — **only lock and unlock** (plus a basic
status read). In particular **local unlock**, which was previously **not possible** for these
Tuya BLE locks: the cloud API deliberately doesn't expose remote unlock, HA Bluetooth proxies
can't hold the connection, and existing integrations only did lock/status. This component's
whole reason for existing is to make **local, cloud-free unlocking** work.

It intentionally does **not** aim to expose the lock's other features (fingerprints, PIN
management, logs, battery, etc.) — just reliable lock/unlock.

## Why on-device (and not a Bluetooth proxy)

A Home Assistant Bluetooth *proxy* connects to this lock but **can't hold the connection** — the timing-strict lock drops it (`reason 0x13`) and sends zero notifications, because the ESP32 shares one radio between WiFi and BLE and the proxy's constant WiFi traffic starves the BLE link.

This component sidesteps that entirely: it does the whole exchange as a **brief on-demand BLE burst on the ESP itself**, so WiFi/BLE coexistence isn't a problem. Verified working on a dedicated ESP32-C3 and on a re-flashed Shelly Plus 1 sitting next to the door.

## Supported hardware

- **The lock:** Tuya **ZX-5330 / ZX-5377** (`jtmspro`). Other Tuya BLE locks use the same crypto but may differ in DP mapping / characteristics — not supported/tested here.
- **The ESP:** any **BLE-capable ESP32** — classic ESP32, C3, S3, C6. (NOT ESP8266/ESP-12E — no Bluetooth. NOT ESP32-S2 — no Bluetooth.) Place it **within BLE range of the lock** (same room / a few metres). A Shelly Plus 1/Pro flashed with ESPHome works well if it's already near the door.

  **Tested on:**
  - **ESP32-C3** (dedicated board)
  - **Shelly Plus 1** (single-core ESP32) flashed with ESPHome — reused as it already sits at the front door

  Single-core ESP32s are fine: the unlock is a brief on-demand BLE burst, not continuous proxying, so WiFi/BLE coexistence isn't an issue in practice.

## ⚠️ Placement: keep the ESP32 CLOSE to the lock

**Put the ESP32 within a few metres of the lock, ideally same room / line of sight.**
Locks are timing-strict: with a **weak signal** (roughly worse than **−75 dBm**) we saw
**delays, dropped notifications, and failed unlocks** — the handshake stalls or the lock
terminates the connection mid-exchange. A strong, stable link is the single biggest factor
for reliability. Reusing a device that already sits *at the door* (like a Shelly) is ideal;
mounting the ESP in another room and hoping it reaches is not.

## Installation

1. Add this repo as an external component and configure your lock — see [`examples/tuya-lock.yaml`](examples/tuya-lock.yaml).
2. Fill in your credentials (below).
3. Flash to an ESP32 in range of the lock.
4. Home Assistant will show three buttons: **… Unlock / Lock / Status**.

The example below configures **one** lock for clarity. **One ESP32 can drive several locks** —
just add another `ble_client` + `tuya_lock` + buttons block per lock (see
[Multiple locks](#multiple-locks) and [`examples/multi-lock.yaml`](examples/multi-lock.yaml)).

The component is pulled straight from GitHub — you don't need to copy any files locally.
To protect against a future update changing behaviour, pin a released version with `ref:`.

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/menelike/esphome-tuya-ble-lock
    components: [tuya_lock]
    # ref: v1.0.0   # optional: pin to a release for a reproducible build

esp32_ble_tracker:

ble_client:
  - mac_address: <LOCK_MAC>
    id: my_lock_client

tuya_lock:
  - id: my_lock
    ble_client_id: my_lock_client
    local_key: "<LOCAL_KEY_V2>"
    uuid: "<UUID>"
    device_id: "<DEVICE_ID>"
    passcode: "<PASSCODE>"

button:
  - platform: tuya_lock
    action: unlock
    tuya_lock_id: my_lock
    name: "Front Door Unlock"
  - platform: tuya_lock
    action: lock
    tuya_lock_id: my_lock
    name: "Front Door Lock"
  - platform: tuya_lock
    action: status
    tuya_lock_id: my_lock
    name: "Front Door Status"
```

## In Home Assistant

Once the ESP32 is flashed and on your network, Home Assistant discovers it automatically via
the **ESPHome integration** (Settings → Devices & Services → ESPHome → add the device if not
auto-discovered; it needs the API encryption key from your YAML).

The device then exposes each button you defined as a standard **button entity**, e.g.:

- `button.front_door_unlock`
- `button.front_door_lock`
- `button.front_door_status`

You can:
- **Press them** from the dashboard or Developer Tools → Actions to lock/unlock.
- **Use them in automations** (see [below](#home-assistant-automation)) — e.g. unlock on arrival.
- Optionally react to results via the `on_success` / `on_error` triggers (see
  [Result feedback](#result-feedback-optional)).

With **multiple locks**, HA still sees a single ESPHome **device** (the one ESP32), and every
button from every lock appears as its own entity under it — e.g. `button.front_door_unlock`,
`button.back_door_unlock`, etc. They're distinguished by the `name:` you give each button, so
prefix them per lock (e.g. "Front Door …", "Back Door …").

What happens when you press **Unlock**: the ESP connects to the lock over BLE, runs the
encrypted handshake, sends the unlock, and disconnects — all on-device, typically in a couple
of seconds. There is no `lock` *entity* (with a lock/unlock toggle) — this component is
deliberately button-based (fire an action), matching its "just lock/unlock" scope. If you want
a lock entity, you can build a [template lock](https://www.home-assistant.io/integrations/lock.template/)
in HA that calls these buttons.

### Multiple locks

One ESP32 can drive several locks — give **each** its own `ble_client`, `tuya_lock`, and
buttons (unique `id`s), all within BLE range of that ESP, and reflash:

```yaml
ble_client:
  - mac_address: <FRONT_MAC>
    id: front_client
  - mac_address: <BACK_MAC>
    id: back_client

tuya_lock:
  - id: front
    ble_client_id: front_client
    local_key: "<FRONT_LOCAL_KEY>"
    uuid: "<FRONT_UUID>"
    device_id: "<FRONT_DEVICE_ID>"
    passcode: "<FRONT_PASSCODE>"
  - id: back
    ble_client_id: back_client
    local_key: "<BACK_LOCAL_KEY>"
    uuid: "<BACK_UUID>"
    device_id: "<BACK_DEVICE_ID>"
    passcode: "<BACK_PASSCODE>"

button:
  - platform: tuya_lock
    action: unlock
    tuya_lock_id: front
    name: "Front Door Unlock"
  - platform: tuya_lock
    action: unlock
    tuya_lock_id: back
    name: "Back Door Unlock"
  # ...add lock/status buttons per lock as needed
```

Note: all the locks must be in BLE range of the **one** ESP — see the placement warning above.

### Result feedback (optional)

Each `tuya_lock` fires `on_success` / `on_error` automations you can hook in HA:

```yaml
tuya_lock:
  - id: front
    ble_client_id: front_client
    local_key: "..."
    uuid: "..."
    device_id: "..."
    passcode: "..."
    on_success:
      - logger.log: "front door action ok"
    on_error:
      - logger.log: "front door action FAILED"
```

## Configuration fields

| Field         | What it is                        | Required |
|---------------|-----------------------------------|:--------:|
| `mac_address` | The lock's BLE MAC address        | yes |
| `local_key`   | BLE session crypto key (**v2.0**) | yes |
| `uuid`        | Device pairing identity           | yes |
| `device_id`   | Tuya device id                    | yes |
| `passcode`    | The lock's numeric passcode       | yes |

## Getting your credentials

You pull these **once** from the Tuya cloud (there's no other way to get the `local_key` — it's
a per-device secret the app is given at pairing). After setup the lock runs fully local.

First, set up a (free) Tuya IoT project and link your app account:
1. Create a project at <https://iot.tuya.com> (Cloud → create project, pick your data-center
   region). Note the **Access ID** and **Access Secret**.
2. Link your Tuya/Smart Life app account (Devices → Link App Account → scan QR).

### Easiest: the helper script

[`tools/fetch_credentials.py`](tools/fetch_credentials.py) does everything — pulls the correct
(**v2.0**) `local_key`, `uuid`, `device_id`, `mac`, and decodes the `passcode` — and prints a
ready-to-paste ESPHome config:

```
pip install tinytuya
python tools/fetch_credentials.py --api-key <ACCESS_ID> --api-secret <ACCESS_SECRET> --region eu --yaml
```

(Region is one of `eu` / `us` / `cn` / `in`. It auto-detects all `jtmspro` BLE locks on the
account.) You can also run it with no flags and it will prompt for each value (or read a
`tinytuya.json` from `python -m tinytuya wizard` if one is present).

**Example run:**

```console
$ python tools/fetch_credentials.py --api-key abcd1234... --api-secret 9f8e... --region eu --yaml
Connecting to Tuya cloud…
Authenticated. Listing devices…
Found 1 lock(s). Fetching credentials…
  • tuyaXXXXXXXXXXXX … ok

=== Credentials ===

# Front Door  (product_id XXXXXXXX)
  mac_address: AA:BB:CC:11:22:33
  local_key:   ****************    # v2.0 (BLE) key
  uuid:        a1b2c3d4e5f60789
  device_id:   tuyaXXXXXXXXXXXX
  passcode:    ********

=== ESPHome config (paste into your device YAML) ===

ble_client:
  - mac_address: AA:BB:CC:11:22:33
    id: lock0_client

tuya_lock:
  - id: lock0
    ble_client_id: lock0_client
    local_key: "****************"
    uuid: "a1b2c3d4e5f60789"
    device_id: "tuyaXXXXXXXXXXXX"
    passcode: "********"

button:
  - platform: tuya_lock
    action: unlock
    tuya_lock_id: lock0
    name: "Front Door Unlock"
  - ...
```

(Real `local_key`/`passcode` are shown in place of the `****`; they're masked here.) Progress
and warnings go to stderr; the credentials/YAML go to stdout, so you can pipe it, e.g.
`… --yaml > my-lock.yaml`.

**If something's wrong**, the tool tells you specifically — e.g. bad credentials print
`authentication with the Tuya cloud FAILED …` with a checklist; a wrong region, an unlinked
app account, or a lock whose passcode/MAC couldn't be read each get their own clear message
rather than a generic failure.

The rest of this section explains what the tool does under the hood.

### ⚠️ The critical gotcha: use the **v2.0** `local_key`

Tuya's cloud returns **two different `local_key` values** for the same device:

- `/v1.0/devices/{id}` → one key (this is what tinytuya's wizard shows) — **WRONG for BLE**
- `/v2.0/cloud/thing/{id}` → a different key — **this is the one the BLE stack uses**

If pairing fails / you get garbage, you used the v1.0 key. Pull the v2.0 one via the Tuya
API Explorer (call `GET /v2.0/cloud/thing/{device_id}`) or with tinytuya's `Cloud` class:

```python
import tinytuya, json
c = json.load(open('tinytuya.json'))
cloud = tinytuya.Cloud(apiRegion=c['apiRegion'], apiKey=c['apiKey'], apiSecret=c['apiSecret'])
r = cloud.cloudrequest(f"/v2.0/cloud/thing/{DEVICE_ID}")
print(r['result']['local_key'])   # <-- use THIS as local_key
```

### The `passcode`

Read the lock's `ble_unlock_check` status value from the cloud
(`GET /v1.0/devices/{device_id}/status`, code `ble_unlock_check`), base64-decode it, and the
ASCII digits in the middle are the passcode. For example the value decodes to
`0001ffff <8 ASCII digits> 01 <4 bytes> 0000` — those 8 digits are your `passcode`.

## How it works

- Crypto: `login_key = md5(local_key[:6])`, `session_key = md5(local_key[:6] + srand)` where
  `srand` comes from the device-info reply. AES-128-CBC, per-Tuya-BLE framing, CRC16.
- Unlock = DP71 (`ble_unlock_check`) raw payload `ffff0001 + passcode + 01 + <4-byte timestamp> + 0001`.
- Lock = DP46 (`manual_lock`).
- The write/notify characteristics (`0x2B11`/`0x2B10`) live under BLE service **0x1910** on
  this lock; the component enumerates all services and matches by characteristic UUID.
- The ESP connects on button-press, runs the handshake, sends the command, then disconnects
  to free the radio.

## Home Assistant automation

Target the buttons directly, e.g. unlock on arrival:

```yaml
automation:
  - alias: "Unlock front door when I come home"
    trigger:
      - platform: state
        entity_id: person.you
        to: "home"
    action:
      - action: button.press
        target:
          entity_id: button.front_door_unlock
```

## Development

The Tuya protocol logic (crypto, framing, reassembly, DP payloads) lives in a pure,
dependency-free layer — `components/tuya_lock/tuya_protocol.{h,cpp}` — that is covered by
host unit tests (no ESP hardware or external crypto needed):

```
make -C tests test
```

See [`tests/`](tests/). CI runs them on every push.

## Security note

This controls **your own lock, locally**. Treat the credentials (especially `local_key` and
`passcode`) as secrets — keep them in ESPHome `!secret`s / a private config, never commit them.

## Credits

Built by reverse-engineering the Tuya BLE protocol from packet captures of the official app.
Protocol groundwork by the wider community:
[PlusPlus-ua/ha_tuya_ble](https://github.com/PlusPlus-ua/ha_tuya_ble),
[ShonP40/Tuya-BLE](https://github.com/ShonP40/Tuya-BLE),
[redphx](https://github.com/redphx), and
[tinytuya](https://github.com/jasonacox/tinytuya).

## License

MIT — see [LICENSE](LICENSE).
