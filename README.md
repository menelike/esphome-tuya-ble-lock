# esphome-tuya-ble-lock

[![tests](https://github.com/menelike/esphome-tuya-ble-lock/actions/workflows/tests.yml/badge.svg)](https://github.com/menelike/esphome-tuya-ble-lock/actions/workflows/tests.yml)

**Local, cloud-free ESPHome control of the Tuya ZX-5330/-5377 BLE smart lock — the ESP32 runs the full unlock handshake on-device (no cloud, no phone, no gateway).**

An [ESPHome](https://esphome.io) external component for the **Tuya ZX-5330/-5377 Bluetooth (BLE) smart lock** (`jtmspro` category). It controls the lock **fully locally**: the entire encrypted Tuya handshake, pairing, and unlock run **on the ESP32 itself**, exposed to Home Assistant as `unlock` / `lock` / `status` buttons.

No cloud. No phone. No Tuya gateway.

## Scope

The goal of this project is — and always will be — **only lock and unlock** (plus a status
read that surfaces **battery %** and a **last-status** summary). In particular **local unlock**,
which was previously **not possible** for these Tuya BLE locks: the cloud API deliberately
doesn't expose remote unlock, Home Assistant Bluetooth proxies can't hold the connection, and existing
integrations only did lock/status. This component's whole reason for existing is to make
**local, cloud-free unlocking** work.

It intentionally does **not** aim to expose the lock's other features (fingerprints, PIN
management, access logs, etc.) — just reliable lock/unlock plus battery.

## Why on-device (and not a Bluetooth proxy)

A Home Assistant Bluetooth *proxy* connects to this lock but **can't hold the connection** — the timing-strict lock drops it (`reason 0x13`) and sends zero notifications, because the ESP32 shares one radio between WiFi and BLE and the proxy's constant WiFi traffic starves the BLE link.

This component sidesteps that entirely: it does the whole exchange as a **brief on-demand BLE burst on the ESP itself**, so WiFi/BLE coexistence isn't a problem. Verified working on a dedicated ESP32-C3 and on a re-flashed Shelly Plus 1 sitting next to the door.

## Supported hardware

- **The lock:** Tuya **ZX-5330 / ZX-5377** (`jtmspro`). Other Tuya BLE locks use the same crypto but may differ in DP mapping / characteristics — not supported/tested here.

  **Specifically tested with** the lock sold in Germany by Pearl as
  [GRA-15377 / **model TSZ-90**](https://www.pearl.de/a-GRA15377-3110.shtml) (purchased July 2025).
  These generic Tuya locks are rebadged under many names; if yours matches the `jtmspro` category
  and the DP codes in the [status table](#why-only-battery-observed-data-points), it should work.
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

With **multiple locks**, Home Assistant still sees a single ESPHome **device** (the one ESP32), and every
button from every lock appears as its own entity under it — e.g. `button.front_door_unlock`,
`button.back_door_unlock`, etc. They're distinguished by the `name:` you give each button, so
prefix them per lock (e.g. "Front Door …", "Back Door …").

What happens when you press **Unlock**: the ESP connects to the lock over BLE, runs the
encrypted handshake, sends the unlock, and disconnects — all on-device, typically in a couple
of seconds. There is no `lock` *entity* (with a lock/unlock toggle) — this component is
deliberately button-based (fire an action), matching its "just lock/unlock" scope. If you want
a lock entity, you can build a [template lock](https://www.home-assistant.io/integrations/lock.template/)
in Home Assistant that calls these buttons.

### Optional entities: battery + last status

Pressing **Status** reads the lock's data points. You can surface two of them as entities:

- **Battery** (`sensor`, `battery_level:`) — the lock's remaining charge, e.g. `70 %`. Updates
  on each status read and whenever the lock spontaneously reports it.
- **Last status** (`text_sensor`, `last_status:`) — a summary published on **every** status
  read, e.g. `battery 70% · @342s`. The `@Ns` suffix is the ESP's uptime in seconds; it's there
  so the value **differs on every press** — Home Assistant only records a logbook / activity row
  on a state *change*, and the battery % holds steady for days. So this gives you a reliable
  "the press worked" row each time (Home Assistant stamps the real time on the row itself). Only battery is
  surfaced — the lock/door-state DPs report unreliably on this device, so they're intentionally
  left out of the summary to avoid showing misleading values.

```yaml
sensor:
  - platform: tuya_lock
    tuya_lock_id: my_lock
    battery_level:
      name: "Front Door Battery"

text_sensor:
  - platform: tuya_lock
    tuya_lock_id: my_lock
    last_status:
      name: "Front Door Last Status"
```

> If your device config already has a `sensor:` or `text_sensor:` block, **merge** these
> entries into the existing block — YAML forbids declaring the same top-level key twice.

#### Why only battery? (observed data points)

A status read returns several Tuya **data points (DPs)**. On the tested ZX-5330 these were the
only ones that ever came back, and the full raw set is always logged (e.g.
`status frame (DPid=value): 40=0 47=1 8=70`) so you can inspect your own device:

| DP | Tuya name              | Observed        | Exposed?                                    |
|----|------------------------|-----------------|---------------------------------------------|
| 8  | `residual_electricity` | `70` (battery %)| ✅ **yes** — the one value reported reliably |
| 47 | `lock_motor_state`     | `1`             | ❌ no — did not track real lock/unlock state |
| 40 | `unlock_door` / door   | `0`             | ❌ no — did not track real door open/closed  |

DP47 and DP40 **looked like** lock/door state, but in testing they were effectively **dead** —
they didn't change when the door was actually locked/unlocked or opened/closed, so exposing them
as entities would show misleading values. They're intentionally left out. If your device reports
them meaningfully, you'll see them change in the `status frame` log line and can open an issue.

### Reliable first connect (scan window)

The lock is a **sleepy, battery-powered peripheral**: it advertises infrequently to save power.
The ESP32 can only connect when its BLE scanner happens to catch one of those advertisements —
and the `esp32_ble_tracker` default listens only **30 ms out of every 320 ms** (~9% of the time).
So the **first** on-demand connect can take several seconds, or time out, simply because the
scanner kept missing the advertisement. (The component already retries once automatically as a
backstop, but the real fix is to scan more.)

Widen the scan window so the ESP is listening almost continuously:

```yaml
esp32_ble_tracker:
  scan_parameters:
    interval: 320ms
    window: 300ms      # ~94% listening instead of ~9% — catches the lock right away
    active: true
```

This makes discovery near-instant. On a mains-powered ESP (like a Shelly) the extra radio time
is negligible; if you ever see WiFi coexistence issues, dial `window` back toward ~150 ms.

### Warm-up on boot (`status_on_boot`)

By default the component does **one status read a few seconds after boot**.

**Why this matters — it absorbs the first-connect penalty.** Every action connects *cold* (the
component never holds an open connection), and the **very first** connect after boot is the worst
case: the BLE stack is freshly up and the lock — a sleepy peripheral — may be mid–quiet-gap, so
that first attempt is the one most likely to miss the advertisement and need its automatic retry
(you'll see `no connection yet — retrying discovery once` in the log). `status_on_boot`
deliberately **spends that costly first attempt at boot, when nobody is waiting.** By the time
you press a button at the door, the path is already warm and the lock was recently discovered, so
*your* press connects on the first try. With it off, that same penalty doesn't disappear — it
just moves to whenever you first press, i.e. you'd feel it standing at the door.

As a bonus it also **populates the battery / last-status entities immediately** instead of
leaving them `unknown` until you first press Status.

To disable it (strictly on-demand — the device only ever touches the lock on an explicit
action):

```yaml
tuya_lock:
  - id: my_lock
    # ...
    status_on_boot: false     # default: true
```

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

Each `tuya_lock` fires `on_success` / `on_error` automations you can hook in Home Assistant.
Both receive a string `x` describing the result:
- unlock/lock success → `"unlock sent"` / `"lock sent"`
- status success → `"status: 8=70 47=1 40=0"` (the raw DP dump; battery is DP 8)
- error → a short reason (e.g. `"lock not found (no advertisement)"`)

```yaml
tuya_lock:
  - id: front
    ble_client_id: front_client
    local_key: "..."
    uuid: "..."
    device_id: "..."
    passcode: "..."
    on_success:
      - logger.log:
          format: "front door action ok: %s"
          args: [x.c_str()]
    on_error:
      - logger.log:
          format: "front door action FAILED: %s"
          args: [x.c_str()]
```

## Configuration fields

Under `tuya_lock:`:

| Field           | What it is                                         | Required |
|-----------------|----------------------------------------------------|:--------:|
| `ble_client_id` | Links to the `ble_client:` for this lock           | yes |
| `local_key`     | BLE session crypto key (**v2.0** — see below)      | yes |
| `uuid`          | Device pairing identity                            | yes |
| `device_id`     | Tuya device id                                     | yes |
| `passcode`      | The lock's numeric passcode                        | yes |
| `status_on_boot`| Read status shortly after boot to warm the BLE path + fill entities (default: `true`) | no |
| `on_success` / `on_error` | Automations run when an action finishes  | no |

The lock's **BLE MAC address** is not a `tuya_lock` field — it goes on the linked `ble_client:`
block (`mac_address:`), which `ble_client_id` points to.

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

## Security

This device can **open your front door**, so treat it as a security-sensitive device, not a
regular sensor.

**Credentials are secrets.** The `local_key` and `passcode` are all that's needed to unlock —
keep them in ESPHome `!secret`s / a private config and never commit them. The `passcode` is the
lock's real door code.

**Don't expose a fallback AP or config portal.** The examples deliberately omit
`captive_portal:` and the WiFi fallback `ap:`. Here's why that matters for a lock:

- With a fallback `ap:` + `captive_portal:`, an ESP that **loses WiFi** (router reboot, or an
  attacker simply **cutting power to your router**) will bring up its **own open/soft-AP
  Wi-Fi network** and serve a config portal. Anyone in range can then connect to it and
  potentially reconfigure the device or point it at their own network — a physical-proximity
  attack that needs no credentials.
- Similarly, avoid the ESPHome **`web_server:`** component on a door controller — it's an
  extra network-exposed control surface.
- Leaving these out means a WiFi outage simply makes the lock **buttons unavailable** until
  WiFi returns — the *safe* failure mode (the door just can't be operated remotely; it isn't
  opened, and nothing new is exposed). To change WiFi, re-flash over USB.

**Keep it on a trusted/IoT VLAN.** The unlock path is Home Assistant → (encrypted API) → ESP → BLE → lock.
Protect the Home Assistant side as you would any door-unlock capability, and consider putting the ESP on a
segregated IoT network.

## Credits

Built by reverse-engineering the Tuya BLE protocol from packet captures of the official app.
Protocol groundwork by the wider community:
[PlusPlus-ua/ha_tuya_ble](https://github.com/PlusPlus-ua/ha_tuya_ble),
[ShonP40/Tuya-BLE](https://github.com/ShonP40/Tuya-BLE),
[redphx](https://github.com/redphx), and
[tinytuya](https://github.com/jasonacox/tinytuya).

## License

MIT — see [LICENSE](LICENSE).
