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
**Bluetooth connection** diagnostic, plus a **Gate position** diagnostic. No Home Assistant helpers are required;
source sensors and command buttons stay internal to the firmware.

Gate state is an opening-class binary sensor: Open/Closed with fresh feedback,
**Unknown** if Bluetooth disconnects, feedback expires or startup state is not yet
confirmed, and **Unavailable** when the ESP32 cannot reach Home Assistant.
The native covers retain their entire last confirmed state, including Opening
or Closing, when feedback is lost or awaiting confirmation. They can therefore
still display movement after a lost connection until fresh feedback returns;
they do not independently become unavailable on a Bluetooth failure. Before
using a cover in an automation, require Gate state to be `on` or `off`.
PS19001 permits a five-unit closed-position tolerance only with an explicit
CLOSED status. After tracked closing reaches that range, PS19001 accepts one
explicit CLOSED reply; otherwise it requires repeated fresh confirmation.
CLOSED outside that tolerance makes
Gate state Unknown rather than falsely refreshing Open. Gate position is the raw
controller percentage, not an independently measured leaf angle; it becomes
Unknown on Bluetooth loss or after 25 seconds without a valid status reply.
Shared UART reply mismatches refresh READ STATUS immediately after the completed
GATT read, with two refreshes maximum per transaction (shared with notification
recovery). Status reads request the reply immediately after confirmed GATT write
completion, without an added first-read wait. Debug logs under `gate_timing` record the write, wait,
read and total durations plus whether the reply matched the expected acknowledgement;
a matching reply alone does not prove freshness. A developer-only constructor
option enables paired-read auditing: the early result is withheld, a second read
starts no earlier than 200ms after write completion, and only that later result
enters feedback. This diagnostic changes transaction duration and is disabled
by default; a difference may reflect legitimate movement rather than staleness.
The audit-only third constructor argument selects an early-read wait from
0–200ms. It has no effect when auditing is disabled, so diagnostic
trials do not change normal zero-delay operation.
Exhausted transactions retry after
1, 2, then 5 seconds without tearing down a healthy
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
5. Install and add the ESPHome device to Home Assistant. Bluetooth monitoring
   starts automatically on every boot and reconnects after a link failure. The
   driver waits for GATT registration and runs its timeout watchdog independently
   of ESPHome's idle BLE-client loop. Leave
   the gate closed for the initial status synchronization.
6. The covers, Gate state and Bluetooth connection appear automatically through
   the native ESPHome integration. No MQTT broker or HA helpers are required.

The Bluetooth hop replaces the gate's cloud path, but the ESP32 still needs a
reliable Wi-Fi connection to Home Assistant. Start with the phone app closed for
baseline testing, then test coexistence separately. The gate can accept a limited number of Bluetooth clients; close the phone app
when diagnosing contention.

## Polling and movement feedback

Experimental listening-first mode waits 10 seconds between health reads while
freshly confirmed Closed. A notification or a local movement command wakes status
reads immediately. While open or unsynchronized, each completed reply allows the
next transaction without an added scheduling delay. Confirmed closure returns to
listening immediately, without a post-movement fast window. A missed notification
can delay movement detection until the next health read; notification coverage for
all opening sources is still under validation. Feedback still expires after 25s.
While a transition candidate awaits confirmation, the previous 150 ms gap is
retained: reducing that gap can bring the next reply inside the unchanged
500 ms confirmation window and unnecessarily require another sample.
These are scheduling delays, not guaranteed update rates: GATT/UART response
latency and bounded mismatch recovery add time. Only one transaction is in flight.
A notification prompts a fresh status read; it never directly establishes state.
Due background settings reads take priority at the Closed-idle boundary so
faster status polling cannot starve automatic configuration refreshes.
If a notification arrives while a status transaction is waiting on unrelated
reply content, the bridge can submit a fresh READ STATUS after the outstanding
GATT operation completes. A new event also leaves idle mismatch backoff promptly. This refresh shares the transaction budget and never repeats a movement command.
Compatible endpoint notifications preserve a pending fresh confirmation rather
than restarting its timer; stable Closed still requires fresh revalidation.
A matching Opening/PedOpening event can also retain a pending OPENING sample
received within 1.5 seconds. It never publishes a mode by itself: another fresh,
progressing read is required, and conflicting hints reset confirmation.
If status has already confirmed physical Open without an access mode, the same
1.5-second opening context may accept a delayed mode notification. The gate must
have started from a fresh synchronized Closed baseline; another solicited OPENING
reply must progress beyond the latest position observed before the notification.
The event neither changes Gate state nor renews its freshness. Expired context,
reversal, conflicting mode hints, other movement phases and disconnect reject it.
A fresh confirmed Gate state is retained while notifications or new candidate
samples are checked, instead of flashing Unknown during each transition. These
checks never extend the 25-second freshness timeout. Configuration writes stay
blocked while a notification or candidate awaits confirmation, even if the last
confirmed displayed state is Closed. Disconnect and stale-status handling are
unchanged.

A successfully submitted HA opening command supplies an 8-second access-mode hint. It
cannot report Opening/Open by itself: two subsequent status samples must confirm
actual movement. The hint is discarded on disconnection or expiry, and explicit
endpoint status overrides it. The PS19001 profile also opts into an event-mode
hint: exact C004 `Opening` means car and `PedOpening` means pedestrian. It is
accepted only in an already synchronized session with a Closed
baseline still within the existing 25-second physical freshness limit, then corroborated by two progressing OPENING status reads. Replayed
events followed by Closed, conflicting hints, reversed readings and expired hints
cannot classify an opening. The PED/FULL prefix in status replies is never used
to classify access mode. Untimestamped events still have residual ambiguity if an
old event coincides with a different real movement, so validate your installation
before using these modes for announcements. Without a usable hint, the access
mode remains unresolved until a distinguishing endpoint. Non-closed confirmation
uses 500 ms. For the opted-in PS19001 profile, one explicit in-tolerance CLOSED
reply can finish an established closing cycle: at least two decreasing CLOSING
readings separated by 500 ms must have reached the closed range within the last
5 seconds, with synchronized valid physical feedback. Notifications alone, Stop,
reversal, stale tracking or reconnect cannot take this shortcut. Otherwise Closed
confirmation retains two samples at least 1 second apart. Untimestamped shared
UART replies still carry residual freshness uncertainty; these guards do not
provide independent physical sensing.

One user command can wait behind a background status read, without overlapping
Bluetooth writes. A second non-Stop command is rejected while that slot is full;
Stop retains priority and the software lock is rechecked before a queued open.

## Controller definitions

### Configuration entities (settings-readonly branch)

All 19 PS19001 parameters are editable native ESPHome **Configuration** selects,
including motor type, current thresholds, speeds, leaf delays, auto-close,
safety mode, pedestrian mode, lights, photocells, buzzer, latch, LED orientation,
single/dual gate, and closing reverse duration. Values stay unknown until a
validated controller read; they are never optimistically published or restored
from flash. System learning, factory reset, PIN changes and firmware updates
remain separate operations and are not exposed here.

Changing an option requires authenticated Bluetooth and fresh confirmed Closed
feedback. The bridge reads the full configuration, replaces just the selected
field, submits one guarded `WRITE FUNCTION`, and verifies the complete readback.
Unknown records and every unrelated value are preserved byte-for-byte. Unknown
metadata or incomplete/invalid snapshots block writing. Selecting the current
value makes no board write. Stop cancels an unsent change; reconnect never replays
writes. A readback mismatch displays actual values and does not overwrite them.

Motor type has dependent current encodings: Overcurrent/Limit Switch use 2–5 A;
Hall Sensor uses 0.5–4 A in 0.1 A steps. Current selects list the union of these
choices, but only values valid for the controller's freshly read mode can be
saved. Switching motor type converts both current fields while preserving their
physical amperage. If a threshold cannot be represented in the target mode, the
change is rejected; choose compatible thresholds first. No default or nearest
threshold is substituted. Hall mappings are derived from the Android model and
covered by host tests, but have not been physically validated on Hall hardware.

Settings refresh automatically after connection/status synchronization, after a
save, and every five minutes while feedback confirms the gate is closed. An
exhausted configuration-read attempt is scheduled again after 30 seconds, with
the same confirmed-Closed/idle guard. No monitoring switch or manual refresh button is
published to Home Assistant. **Settings update result** explains validation,
rejected edits and uncertain outcomes. Settings retain their last validated
values during loss of feedback; they are not a continuous settings stream.

Automatic background refreshes yield to controller notifications, queued movement
commands, or movement-status content encountered in a shared reply. An outstanding
GATT write/read finishes first; no further configuration reread is started. Status
polling resumes and configuration is reconsidered after 30 seconds, once Closed
is confirmed. Manual reads, active setting saves and uncertain-save verification
are not discarded by this background scheduling rule.

The original 18 read-only sensor entities are replaced by
`select.*` entities with the same names; Auto-closing keeps its existing select.
Update any consumers of the old `sensor.*` settings entities during migration.
Covers, Gate state, Bluetooth connection and unrelated entity IDs are unchanged.

### Shared UART reply recovery

EyeOpen/cloud clients can overwrite the shared UART reply. The bridge serializes
its own operations and waits at least 250 ms between settings transactions.
It searches multiline responses for a complete matching reply, rejects conflicting
acknowledgements, and validates every echoed byte of a full write acknowledgement.
Settings reads wait 600 ms for their first reply and allow five reply reads,
500 ms apart, before resending. If a configuration reply is overwritten, it
resends **READ FUNCTION only** with staggered backoff: at most three sends.
The first recovered background retry is logged internally; prolonged recovery
and exhausted attempts remain visible in Settings update result. Write/readback
uncertainty is always reported explicitly. It retains a
healthy Bluetooth connection throughout these text mismatches. Initial and
reconnect status reads also retain a healthy link and back off on unrelated
replies; a queued movement command still cannot bypass baseline validation. Invalid or
truncated configuration frames also enter this bounded recovery.

If a write acknowledgement is lost, the bridge checks the actual configuration
by reading it; it never repeats the write. A complete matching readback establishes
success even without that acknowledgement. Exhausted read recovery reports an
uncertain save, blocks further edits until a fresh configuration read, and resumes
normal status polling. An automatic read is scheduled after 30 seconds to resolve
the uncertainty when fresh Closed feedback is available. Transport failures still disconnect. An uncertain outcome
survives disconnect messages until a fresh read resolves it. These measures reduce
reply contention; this protocol has no compare-and-swap or transaction identifier,
so concurrent edits from another app cannot be made atomic. Avoid editing settings
from two clients at once.

Host tests cover all field encodings and failure paths. Only the auto-close
10 → 20 → 10 seconds save has been tested on the physical controller so far;
other parameter writes still require installation-specific validation.

The PS19001/P3.6 controller returns `ACK READ FUNCTION,` followed by labeled
fields. The matcher also accepts the colon separator. The model schema is in
`settings/ps19001.json`; inspect a saved ACK with
`python tools/read_settings.py /path/to/snapshot.txt`. It validates all 19 fields,
numeric formats and option ranges. Field definitions generate both native YAML
entities and the firmware decoder. Extend controller support with a validated
model/schema and transport tests, rather than broadening the command allowlist.

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

Learning, firmware updates and network provisioning are not implemented. They
require separate validated workflows and are not exposed through arbitrary UART
entry. Motor and safety settings should match the installed hardware.

Licensed under MIT. This project is not affiliated with Powertech or EyeOpen.

Hardware testing showed that PS19001 P3.6 may report PED OPENING during full
car travel after a previous pedestrian command. FULL/PED prefixes therefore
are not trusted as independent access-mode evidence. External opening mode
remains unknown until a distinguishing controller state arrives. A local
command supplies only a bounded hint; fresh status still proves movement.
Concatenated status replies are usable only when identical or when
one exact movement phase/prefix advances monotonically; the bundle counts as
one sample, never two confirmations. Mixed phases/modes, reversed progression
and contradictory endpoints remain rejected.

A queued user command can proceed as soon as an in-flight background status
read completes, including an unrelated reply; background rereads no longer
hold it up. Startup/reconnect baseline validation, lock checks, serialized GATT
operations and no movement replay remain unchanged.

Movement acknowledgement reads have no added first-read or reread delay. They
are serialized, with a two-second retry window and a maximum of 12 completed
reads (whichever ends first); an outstanding read still has its transport
timeout. A missing movement acknowledgement then causes the
bridge retains the healthy link and resumes status reads. The outcome remains
explicitly uncertain; no movement is replayed. A failed GATT operation still
uses transport-failure handling. This avoids an eight-second telemetry stall
and reconnect quarantine solely because another client overwrote the ACK.

### Retained link diagnostics

Gate position appears with the normal sensors. Bluetooth signal strength is a
diagnostic sensor reporting the connected controller's RSSI in dBm every 20
seconds. It becomes Unknown when the Bluetooth link closes; Wi-Fi signal
strength is separate and remains disabled by default.

The bridge keeps the last four transport failures/disconnect reasons in RAM and
repeats them in DEBUG logs once per minute, with event age, protocol phase and
operation category. They are not Home Assistant entities and contain no PIN or
command payload. This bounded buffer survives Bluetooth reconnects, but not an
ESP32 restart. A log capture opened after a dropout can therefore recover its
reason without continuously streaming logs. It is diagnostic evidence, not an
automatic inference of the root cause.

### Home Assistant publication batching

Native API state updates use a 20ms batching delay instead of the 100ms default.
This reduces the configured publication wait without changing Bluetooth polling
or feedback confirmation. It uses more network packets; the configured reduction
is not a measured end-to-end latency guarantee.
