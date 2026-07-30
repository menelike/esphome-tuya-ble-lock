# tools

## fetch_credentials.py

Pulls the credentials needed by this component from the Tuya cloud and prints a ready-to-paste
ESPHome config. This is the easiest way to get the (correct, **v2.0**) `local_key`, `uuid`,
`device_id`, `mac`, and decoded `passcode` for each Tuya BLE lock on your account.

### Setup (one-time)

1. `pip install tinytuya`
2. Create a free Tuya IoT project at <https://iot.tuya.com> (Cloud → create project). Note the
   **Access ID** and **Access Secret**. These are API credentials — **not** your app login.
3. Link your Tuya / Smart Life app account: project → Devices → Link App Account → scan the QR.

### Usage

```
python fetch_credentials.py --api-key <ACCESS_ID> --api-secret <ACCESS_SECRET> --region eu --yaml
```

- `--region` — your data-center: `eu` | `us` | `cn` | `in`
- `--yaml` — also print a paste-ready ESPHome config block
- `--device-id <id>` — only fetch one device (default: auto-detect all `jtmspro` locks)
- no flags → interactive prompts (also reads a `tinytuya.json` if present)

Progress/warnings → stderr; credentials/YAML → stdout (so `--yaml > my-lock.yaml` works).

### Behaviour on errors

The tool gives a specific, actionable message for each failure mode:

| Situation | Message |
|-----------|---------|
| Missing Access ID/Secret | `missing required credential(s): …` |
| Invalid `--region` | `invalid --region '…'. Must be one of: eu, us, cn, in` |
| Wrong credentials | `authentication with the Tuya cloud FAILED …` + checklist |
| App account not linked | `authenticated, but the account has ZERO devices …` |
| No BLE locks found | lists all devices + suggests `--device-id` |
| A lock's MAC/passcode unreadable | a per-device note; the rest still succeeds |

Nothing is written anywhere — it only reads. **Keep the printed `local_key` and `passcode`
secret** (use ESPHome `!secret` / a private config).
