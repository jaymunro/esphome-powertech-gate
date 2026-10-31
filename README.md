# Powertech Gate for ESPHome

Local Bluetooth control and feedback for compatible Powertech / EyeOpen WBT01
controllers, using an ESP32 as a bridge to Home Assistant. No cloud account is
required. This is an experimental interoperability project, currently tested
with an M5Stack ATOM Lite and a PS19001 controller on module firmware P3.6.

## Current capabilities

- Car and pedestrian open/close commands, Stop, and authenticated status reads.
- Gate state separate from mutually exclusive car/pedestrian access modes.
- Startup quarantine, status confirmation, stale-state expiry and status-only
  reconnect. Buffered Bluetooth events cannot directly establish gate state.
- Optional Home Assistant software-lock guard. Authentication uses the existing
  gate PIN; it does not change the PIN or learn a transmitter.
- Controller definitions separate from protocol code: see `models/` and
  [CONTRIBUTING.md](CONTRIBUTING.md).

Pedestrian opening and automatic closure were physically confirmed. Both-leaf
transmitter feedback was physically confirmed. Explicit close and Stop commands
still need hardware validation. Do not migrate security or announcement consumers
until your own installation passes the documented checks. UART reply contention
with EyeOpen/cloud clients is under investigation.

From version 0.8.0, ESPHome publishes two native covers, **Gate state** and a
**Bluetooth connection** diagnostic. No Home Assistant helpers are required;
source sensors and command buttons stay internal to the firmware.

Gate state is an opening-class binary sensor: Open/Closed with fresh feedback,
**Unknown** if Bluetooth disconnects, feedback expires or the state is not yet
confirmed, and **Unavailable** when the ESP32 cannot reach Home Assistant.
The native covers retain their entire last confirmed state, including Opening
or Closing, when feedback is lost or awaiting confirmation. They can therefore
still display movement after a lost connection until fresh feedback returns;
they do not independently become unavailable on a Bluetooth failure. Before
using a cover in an automation, require Gate state to be `on` or `off`.
PS19001 permits a one-unit closed-position tolerance only with an explicit
CLOSED status and repeated fresh confirmation. Shared UART reply mismatches
retry status reads after 1, 2, then 5 seconds without tearing down a healthy
Bluetooth link. Actual transport failures still disconnect; no motion command
is automatically retried. A connected Bluetooth link alone does not guarantee
fresh feedback. See
[Home Assistant entity semantics](home-assistant/README.md).

## Installation

Requires ESPHome 2026.10.1 or later. The initial hardware package targets the
ATOM Lite (ESP32-PICO-D4). Other ESP32 boards have not been hardware tested.

1. Find the controller's Bluetooth address and advertised name. Do not substitute
   its Wi-Fi address: these can differ. The tested family advertises `WBT01-xxxx`.
2. Create an ESPHome device configuration like this:

```yaml
substitutions:
  device_name: powertech-gate
  friendly_name: Powertech Gate
  gate_lock_entity: lock.gate_lock
  require_gate_unlock: "true"
packages:
  controller:
    url: https://github.com/jaymunro/esphome-powertech-gate
    ref: main
    files:
      - packages/powertech-gate.yaml
      - packages/models/ps19001.yaml
```

For a validated deployment, pin `ref` to a reviewed commit instead of following
main. Only select a model that has been validated for your controller.

3. In ESPHome Secrets, set `wifi_ssid`, `wifi_password`, `gate_api_key` (a freshly
   generated ESPHome encryption key), `gate_ota_password`, `gate_ble_pin` (quoted),
   `gate_ble_mac`, and `gate_ble_name`. Never use the synthetic example values
   for deployment. `examples/secrets.example.yaml` is for compilation only.
4. The opening guard defaults on. Supply an existing HA lock entity that reports
   `unlocked`, or deliberately set `require_gate_unlock: "false"` if you do not
   have a software lock. Unknown/locked blocks opening while the guard is on.
5. Install and add the ESPHome device to Home Assistant. Enable **Bluetooth
   monitoring**. It defaults off on the first install. Leave the gate closed
   for the initial status synchronization.
6. The covers, Gate state and Bluetooth connection appear automatically through
   the native ESPHome integration. No MQTT broker or HA helpers are required.

The Bluetooth hop replaces the gate's cloud path, but the ESP32 still needs a
reliable Wi-Fi connection to Home Assistant. Start with the phone app closed for
baseline testing, then test coexistence separately. Monitoring can be paused
before connecting another Bluetooth client.

## Polling and movement feedback

Closed idle polling waits 1 second after each reply. While open, unsynchronized,
or within 30 seconds of a controller event/command, it waits 500 ms instead.
These are scheduling delays, not guaranteed update rates: GATT/UART response
latency and bounded mismatch recovery add time. Only one transaction is in flight.
A notification prompts a fresh status read; it never directly establishes state.

An acknowledged HA opening command supplies an 8-second access-mode hint. It
cannot report Opening/Open by itself: two subsequent status samples must confirm
actual movement. The hint is discarded on disconnection or expiry, and explicit
endpoint status overrides it. Without a recent local command, ambiguous generic
transmitter opening feedback can still leave the access mode unknown until the
controller reports a distinguishing state. Closed confirmation still requires
samples at least 1 second apart; non-closed confirmation uses 500 ms.

One user command can wait behind a background status read, without overlapping
Bluetooth writes. A second non-Stop command is rejected while that slot is full;
Stop retains priority and the software lock is rechecked before a queued open.

## Controller definitions

### Experimental settings snapshot (settings-readonly branch)

The PS19001 profile has an optional **Read controller settings** configuration
button, disabled by default. It sends one serialized legacy `READ FUNCTION`
request using the existing authenticated connection. It works while the HA
software gate lock is locked because it cannot move the gate.

The complete response stays internal and is logged in numbered `gate_settings`
chunks. Unknown fields are preserved. Capturing a reply does not validate its
parameter meanings or indicate that any settings were saved. No parameter write,
learning, reset or alternate-protocol commands are allowlisted. This experimental
branch does not yet expose editable settings, and the request is never scheduled
as part of automatic polling or reconnect recovery.

A definition is a small JSON file under `models/`, with its supported transport,
status signatures, position semantics, pedestrian capability and validation
record. `tools/generate.py` validates definitions and generates model packages.
The shared protocol driver remains in `src/`; contributors do not copy it.

Only WBT01 legacy UART v1 is implemented. A definition cannot magically add a
new UART protocol or authorize arbitrary configuration commands. Unsupported
protocols are rejected by the generator. Each additional model needs sanitized
fixtures plus physical validation. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Development

```sh
python3 tools/generate.py
python3 tools/generate.py --check
python3 -m unittest discover -s tests -p 'test_*.py'
c++ -std=c++17 -Wall -Wextra tests/test_gate_feedback.cpp -o /tmp/gate-feedback-test
/tmp/gate-feedback-test
c++ -std=c++17 -Wall -Wextra tests/test_gate_client.cpp -o /tmp/gate-client-test
/tmp/gate-client-test
```

Host tests mock Bluetooth and hashing. They verify state/transaction handling,
not cryptography or physical delivery. Compile the example with a local
`examples/secrets.yaml` containing synthetic values to validate ESPHome codegen.
Generated YAML is checked in for remote-package installation; edit `src/`,
`templates/` or `models/`, then regenerate it.

## Before replacing existing controls

Validate pedestrian and car transmitter cycles, commanded opening/closing,
Stop, automatic closure, Wi-Fi/Bluetooth loss, power-cycle recovery, app
coexistence and stale-event handling. Check that only the intended access mode
opens and that Gate state becomes Unknown when feedback is lost. Keep
existing locking and other safety/security configuration in place during tests.

Controller settings (auto-close timer, motor configuration, learning, firmware
updates and network provisioning) are not implemented. They require separate
validated read/write workflows and are not exposed through arbitrary UART entry.

Licensed under MIT. This project is not affiliated with Powertech or EyeOpen.
