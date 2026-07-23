<!--
AI-Provenance:
  model: claude-opus-4-8[1m]
  harness: Claude Code
  skills:
    - brainstorming
-->

# Terma MOA Blue — ESP-IDF port via a minimal external component

**Date:** 2026-07-22
**Status:** Approved design, pending implementation plan

## Problem

The project's ESPHome config (`terma-moa-esphome.yaml`) targets the **Arduino**
framework and relies on `terma_ble_helper.h` — a bare header of four Bluedroid
`#include`s — so that inline YAML lambdas can call raw Bluedroid APIs
(`esp_ble_set_encryption`, the `esp_ble_*_bond_*` family) to:

1. **Force encryption** on the BLE connection when it opens (the radiator
   requires an encrypted link before its characteristics can be read; an
   unencrypted read fails with `status=5`, insufficient authentication).
2. **Clear BLE bonds** from a diagnostic button.

We want to run this on the **ESP-IDF** framework and modernize the encryption
glue into a proper ESPHome external component, removing the helper header and
the raw lambdas.

## Goals

- Config compiles and runs under `esp32: framework: type: esp_idf`.
- The proactive "force encryption on connect" logic lives in a small
  `external_components` component, triggered **automatically** on connect — no
  `on_connect` encryption lambda in YAML.
- Delete `terma_ble_helper.h` and both raw-Bluedroid lambdas.
- Delegate everything that ESPHome now does natively to native features.
- Update install docs to match (no more "copy the `.h` into the HA config dir").

## Non-goals

- No change to sensors, climate, switches, globals, or passkey automations —
  they are already native and framework-agnostic.
- No speculative fix for the upstream esp_idf bond-instability reports; we note
  the risk and verify on-device instead.
- Not publishing the component as a reusable/registry component; it lives
  locally alongside the config.

## Research findings that shaped this design

Verified against esphome.io docs, ESP-IDF source, and ESPHome GitHub issues
(2024–2026):

- **Bluedroid is the stack** ESPHome uses for `esp32_ble` / `ble_client` under
  esp_idf, and ESPHome enables the required sdkconfig itself. So
  `esp_ble_set_encryption()` and `ESP_BLE_SEC_ENCRYPT` are valid — no manual
  sdkconfig flag needed.
- The Bluedroid headers are `extern "C"`-wrapped → no linkage gotchas when
  included from a C++ compilation unit.
- `ble_client.h` already transitively includes `esp_gap_ble_api.h` and
  `esp_bt_defs.h`, which cover every symbol the old lambdas used. The helper
  header was essentially redundant even before this port.
- **Native replacements exist** for two of the three helper jobs:
  - Clearing bonds → native **`ble_client.remove_bond`** action.
  - Passkey handling → already native (`on_passkey_request` +
    `ble_client.passkey_reply`), already used in this config.
- **No native replacement** for proactively initiating encryption. ESPHome
  auto-*accepts* security when the peripheral initiates it
  (`ESP_GAP_BLE_SEC_REQ_EVT` → `esp_ble_gap_security_rsp(..., true)`), but it
  never proactively initiates, and it does **not** retry a read after an
  insufficient-authentication error. This radiator needs the central to
  initiate — so this one piece genuinely needs custom code.
- **A plain `include:` header cannot satisfy "automatic on connect."** A header
  can only expose symbols to a lambda you still have to place in `on_connect`.
  Hooking the connect event automatically requires a component that is a
  `BLEClientNode`. This is why the component (not a trimmed header) is the right
  vehicle given the approved "automatic on connect" choice.
- **`USE_ESP_IDF` is deprecated** (ESPHome 2026.1, removal targeted 2026.6) in
  favor of `USE_ESP32`; all ESP-IDF APIs are available regardless of framework,
  so framework-gating BLE code is unnecessary. Guard component code with
  `#ifdef USE_ESP32`.

## Architecture

A single minimal external component whose only job is to proactively request
encryption the moment the BLE connection opens.

```
components/terma_moa/
  __init__.py     # config: depends on ble_client, registers a BLE node
  terma_moa.h     # class TermaMoa : Component, ble_client::BLEClientNode
  terma_moa.cpp   # gattc event handling → esp_ble_set_encryption(...)
```

### Component behavior

- Registers as a `ble_client::BLEClientNode` bound to `terma_moa_ble_client`,
  so it receives that client's GATTC events.
- On the connection-open GATTC event (`ESP_GATTC_OPEN_EVT`) with status OK, it
  calls `esp_ble_set_encryption(param->open.remote_bda, ESP_BLE_SEC_ENCRYPT)`.
  The peer address comes straight from the event payload — no `sscanf`, no
  reaching into the parent's internals.
- Logs the request at `INFO` so the pairing flow stays observable (parity with
  the current log line "Encryption requested for bonded device").
- Code is wrapped in `#ifdef USE_ESP32`. Bluedroid headers
  (`<esp_gap_ble_api.h>`, and whatever the event-handler signature needs) are
  included explicitly in the component for robustness rather than relying on
  transitive includes.

> Implementation note: the exact `BLEClientNode` override signatures
> (`gattc_event_handler`, and the `loop()` name-collision with `Component`)
> must be matched against the installed ESPHome version's `ble_client.h` during
> implementation. The connect hook (`ESP_GATTC_OPEN_EVT` vs. node reaching the
> established state) is chosen for whichever fires reliably on-device; the spec
> fixes the intent, implementation confirms the exact hook.

### Config-generation (`__init__.py`) shape

- `DEPENDENCIES = ["ble_client"]`
- Schema extends `ble_client.BLE_CLIENT_SCHEMA` and `cv.COMPONENT_SCHEMA`.
- `to_code`: `new_Pvariable` → `register_component` → `ble_client.register_ble_node`.

## YAML changes

| Area | Before | After |
|---|---|---|
| `esp32.framework.type` | `arduino` | `esp_idf` |
| `esphome.includes` | `- terma_ble_helper.h` | removed |
| `external_components` | — | `- source: {type: local, path: components}` |
| new node | — | `terma_moa:` with `ble_client_id: terma_moa_ble_client` |
| `ble_client.on_connect` | encryption lambda (`sscanf` + `esp_ble_set_encryption`) + `delay: 2s` + set `is_connected` | `delay: 2s` + set `is_connected` (encryption now automatic) |
| "Clear BLE Bonds" button | ~12-line enumerate/remove lambda | native `ble_client.remove_bond` action (per-device) |

Everything else in the YAML is unchanged.

### Deleted

- `terma_ble_helper.h`
- The `on_connect` encryption lambda
- The clear-bonds enumerate/remove lambda

### Clear-bonds semantics (decided)

Use native `ble_client.remove_bond`, which clears the bond for **this radiator
only**. The old lambda cleared *all* bonds on the ESP32; for a single-device
setup these are equivalent, and per-device is cleaner.

## Risk

There are open ESPHome reports of bond instability / crashes with **encrypted**
BLE connections specifically under esp_idf (e.g. esphome/esphome#16071). This
port is sound on paper, but the real proof is a full pair + reconnect cycle on
hardware. Success is not claimed from a successful compile alone.

## Verification plan

1. **Compile:** `esphome compile terma-moa-esphome.yaml` succeeds under the
   esp_idf framework (component builds, no undefined Bluedroid symbols).
2. **First-pair on hardware:** flash, put radiator in pairing mode, and confirm
   the logs show the encryption request firing automatically on connect, the
   passkey exchange, and successful characteristic reads (no `status=5`).
3. **Reconnect:** power-cycle the ESP32 and confirm it reconnects to the bonded
   radiator and reads characteristics without re-pairing.
4. **Clear bonds:** press the diagnostic button, confirm the bond is removed and
   a fresh pair works afterward.

## Documentation updates

Update `README.md`:
- Software requirements / install: replace "copy `terma_ble_helper.h` to the HA
  config dir" with "the `components/` folder travels alongside the YAML."
- Note the esp_idf framework.
- Keep the pairing / troubleshooting sections (still accurate).
