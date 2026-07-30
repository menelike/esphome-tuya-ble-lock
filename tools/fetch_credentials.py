#!/usr/bin/env python3
"""
fetch_credentials.py — pull the credentials needed by esphome-tuya-ble-lock from the
Tuya cloud, and print a ready-to-paste ESPHome config block.

It fetches, for each Tuya BLE lock on your account:
  - local_key   (the v2.0 one — the WHOLE point; the v1.0 key does NOT work for BLE)
  - uuid
  - device_id
  - mac_address
  - passcode    (decoded from the ble_unlock_check status value, best-effort)

Requirements:
  pip install tinytuya

You need a (free) Tuya IoT Platform project with your app account linked:
  1. https://iot.tuya.com  ->  Cloud  ->  create a project (note Access ID + Access Secret)
  2. In the project: Devices -> Link App Account -> scan the QR with your Tuya/Smart Life app
  3. Pick your data-center region code:  eu | us | cn | in

Usage:
  python fetch_credentials.py --api-key <ACCESS_ID> --api-secret <ACCESS_SECRET> --region eu
  # or run with no args and it prompts (and can read a tinytuya.json if present)

This talks to the Tuya cloud ONCE to read the keys. After setup the lock runs fully local.
"""

import argparse
import base64
import getpass
import json
import sys


def die(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def load_tinytuya():
    try:
        import tinytuya  # noqa
        return tinytuya
    except ImportError:
        die("tinytuya not installed. Run:  pip install tinytuya")


def decode_passcode(ble_unlock_check_b64):
    """The `ble_unlock_check` status value (base64) decodes to bytes like:
       00 01 ff ff <N ASCII digits> 01 <4 bytes> 00 00
    The ASCII-digit run in the middle is the passcode. Best-effort."""
    try:
        raw = base64.b64decode(ble_unlock_check_b64)
    except Exception:
        return None
    # longest run of ASCII digits
    best = ""
    cur = ""
    for b in raw:
        if 0x30 <= b <= 0x39:
            cur += chr(b)
        else:
            if len(cur) > len(best):
                best = cur
            cur = ""
    if len(cur) > len(best):
        best = cur
    return best or None


def main():
    ap = argparse.ArgumentParser(description="Fetch esphome-tuya-ble-lock credentials from Tuya cloud")
    ap.add_argument("--api-key", help="Tuya Access ID / Client ID")
    ap.add_argument("--api-secret", help="Tuya Access Secret / Client Secret")
    ap.add_argument("--region", help="Data center region: eu | us | cn | in")
    ap.add_argument("--device-id", help="Only fetch this device id (default: all locks)")
    ap.add_argument("--yaml", action="store_true", help="Also print ready-to-paste ESPHome YAML")
    args = ap.parse_args()

    tinytuya = load_tinytuya()

    # allow reading a tinytuya.json produced by `python -m tinytuya wizard`
    key, secret, region = args.api_key, args.api_secret, args.region
    if not (key and secret and region):
        try:
            cfg = json.load(open("tinytuya.json"))
            key = key or cfg.get("apiKey")
            secret = secret or cfg.get("apiSecret")
            region = region or cfg.get("apiRegion")
            if key:
                print("(using credentials from tinytuya.json)")
        except Exception:
            pass
    def prompt(label):
        if not sys.stdin.isatty():
            return ""  # non-interactive: can't prompt
        try:
            return input(label).strip()
        except (EOFError, KeyboardInterrupt):
            return ""

    if not key:
        print("These are Tuya Cloud API credentials from https://iot.tuya.com "
              "(NOT your app login).", file=sys.stderr)
        key = prompt("Tuya Access ID: ")
    if not secret:
        try:
            secret = getpass.getpass("Tuya Access Secret: ").strip() if sys.stdin.isatty() else ""
        except Exception:
            secret = prompt("Tuya Access Secret: ")
    if not region:
        region = prompt("Region [eu/us/cn/in] (default eu): ") or "eu"

    # ---- validate before hitting the API ----
    missing = [n for n, v in (("--api-key", key), ("--api-secret", secret)) if not v]
    if missing:
        die("missing required credential(s): " + ", ".join(missing) +
            ". Pass them as flags or run interactively. "
            "Get them from https://iot.tuya.com (Cloud project → Access ID/Secret).")
    region = region.lower()
    if region not in ("eu", "us", "cn", "in"):
        die(f"invalid --region '{region}'. Must be one of: eu, us, cn, in")

    print("Connecting to Tuya cloud…", file=sys.stderr)
    try:
        cloud = tinytuya.Cloud(apiRegion=region, apiKey=key, apiSecret=secret)
    except Exception as e:
        die(f"could not initialize the Tuya cloud client: {e}")

    # tinytuya stores the auth error (if any) on the client; verify auth explicitly so we can
    # tell "bad credentials/region" apart from "no devices".
    if getattr(cloud, "token", None) is None:
        err = getattr(cloud, "error", None)
        detail = ""
        if isinstance(err, dict):
            detail = str(err.get("Payload") or err.get("err") or err)
        die("authentication with the Tuya cloud FAILED. " + (f"({detail}) " if detail else "") +
            "Check that:\n"
            "  - the Access ID and Access Secret are correct (from iot.tuya.com → your project)\n"
            f"  - --region '{region}' matches your project's data center\n"
            "  - the 'IoT Core' / 'Authorization' API products are subscribed in the project")

    print("Authenticated. Listing devices…", file=sys.stderr)

    # list devices (or just the one requested)
    if args.device_id:
        device_ids = [args.device_id]
    else:
        devs = cloud.getdevices(False)
        if isinstance(devs, dict):  # error payload
            die("could not list devices: " + str(devs.get("Payload", devs)))
        if not isinstance(devs, list):
            die(f"unexpected response listing devices: {devs}")
        if not devs:
            die("authenticated, but the account has ZERO devices. Link your Tuya/Smart Life "
                "app account to this project: iot.tuya.com → your project → Devices → "
                "Link App Account → scan the QR with the app.")
        # jtmspro = Tuya BLE smart lock
        device_ids = [d["id"] for d in devs if d.get("category") == "jtmspro"]
        if not device_ids:
            print("No jtmspro (BLE lock) devices found. Devices on this account:", file=sys.stderr)
            for d in devs:
                print(f"  {d.get('id')}  {d.get('name')}  category={d.get('category')}",
                      file=sys.stderr)
            die("no Tuya BLE locks (category 'jtmspro') found on this account. "
                "If your lock is listed above with a different category, pass it explicitly "
                "with --device-id <id>.")

    print(f"Found {len(device_ids)} lock(s). Fetching credentials…\n", file=sys.stderr)

    def req(path):
        """cloudrequest with graceful handling of network / API errors."""
        try:
            return cloud.cloudrequest(path)
        except Exception as e:  # network down, timeout, etc.
            return {"success": False, "msg": f"request error: {e}"}

    results = []
    warnings = []
    for did in device_ids:
        print(f"  • {did} … ", end="", file=sys.stderr, flush=True)

        # v2.0 endpoint -> the BLE local_key (NOT the v1.0 one!)
        v2 = req(f"/v2.0/cloud/thing/{did}")
        if not v2.get("success") or not v2.get("result"):
            print("FAILED", file=sys.stderr)
            warnings.append(f"{did}: could not read device detail ({v2.get('msg', 'unknown error')})"
                            " — skipped")
            continue
        r = v2["result"]

        local_key = r.get("local_key")
        if not local_key:
            print("no local_key", file=sys.stderr)
            warnings.append(f"{did}: no local_key returned — the account may not own this device; skipped")
            continue

        # factory-infos -> mac (non-fatal if missing)
        mac = None
        fi = req(f"/v1.0/iot-03/devices/factory-infos?device_ids={did}")
        if fi.get("success") and fi.get("result"):
            m = fi["result"][0].get("mac")
            if m and len(m) == 12:
                mac = ":".join(m[i:i + 2] for i in range(0, 12, 2))
        if not mac:
            warnings.append(f"{r.get('name') or did}: MAC not available via API — "
                            "read it off the device/label and fill it in manually")

        # status -> ble_unlock_check -> passcode (non-fatal if missing)
        passcode = None
        st = req(f"/v1.0/devices/{did}/status")
        if st.get("success"):
            for s in st.get("result", []):
                if s.get("code") == "ble_unlock_check":
                    passcode = decode_passcode(s.get("value", ""))
        if not passcode:
            warnings.append(f"{r.get('name') or did}: could not auto-decode the passcode — "
                            "capture one app unlock and read it from the DP71 payload (see README)")

        print("ok", file=sys.stderr)
        results.append({
            "name": r.get("name") or did,
            "device_id": did,
            "local_key": local_key,  # v2.0 = the BLE key
            "uuid": r.get("uuid"),
            "product_id": r.get("product_id"),
            "mac": mac,
            "passcode": passcode,
        })

    # ---- report warnings, then results ----
    if warnings:
        print("\nNotes / partial results:", file=sys.stderr)
        for w in warnings:
            print(f"  ! {w}", file=sys.stderr)

    if not results:
        die("no usable credentials were fetched. See the notes above. Most common causes: "
            "wrong --region, app account not linked to the project, or the API product "
            "'IoT Core' not subscribed in your Tuya project.")

    print("\n=== Credentials ===")
    for x in results:
        print(f"\n# {x['name']}  (product_id {x['product_id']})")
        print(f"  mac_address: {x['mac'] or '<unknown — read from device label>'}")
        print(f"  local_key:   {x['local_key']}    # v2.0 (BLE) key")
        print(f"  uuid:        {x['uuid']}")
        print(f"  device_id:   {x['device_id']}")
        print(f"  passcode:    {x['passcode'] or '<could not decode — capture one app unlock>'}")

    if args.yaml:
        print("\n=== ESPHome config (paste into your device YAML) ===\n")
        print("ble_client:")
        for i, x in enumerate(results):
            print(f"  - mac_address: {x['mac'] or 'AA:BB:CC:11:22:33'}")
            print(f"    id: lock{i}_client")
        print("\ntuya_lock:")
        for i, x in enumerate(results):
            print(f"  - id: lock{i}")
            print(f"    ble_client_id: lock{i}_client")
            print(f'    local_key: "{x["local_key"]}"')
            print(f'    uuid: "{x["uuid"]}"')
            print(f'    device_id: "{x["device_id"]}"')
            print(f'    passcode: "{x["passcode"] or "REPLACE_ME"}"')
        print("\nbutton:")
        for i, x in enumerate(results):
            for action in ("unlock", "lock", "status"):
                print(f"  - platform: tuya_lock")
                print(f"    action: {action}")
                print(f"    tuya_lock_id: lock{i}")
                print(f'    name: "{x["name"]} {action.capitalize()}"')

    print("\nKeep these SECRET (local_key + passcode). Use ESPHome !secret / a private config.")


if __name__ == "__main__":
    main()
