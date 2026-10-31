# Native Home Assistant entities

Version 0.8.0 publishes the entities directly from ESPHome YAML. No HA Template
helpers or MQTT broker are needed. Underlying normalized state sensors and
movement buttons are internal and are not advertised to Home Assistant.

- **Car gate** and **Pedestrian gate** are mutually exclusive access modes,
  not leaf-position sensors. Pedestrian open means car closed, and vice versa.
- **Gate state** uses the Opening binary-sensor class: `on` = Open, `off` =
  Closed. Missing/unconfirmed/stale Bluetooth feedback gives **Unknown**;
  losing the ESP32 connection gives **Unavailable**. No reed switch is used.
- **Bluetooth connection** uses the Connectivity class and shows the actual
  GATT link, Connected/Disconnected. Connected does not imply authenticated,
  synchronized or fresh feedback. Consult Gate state for that.
- Uptime and Wi-Fi signal strength default disabled. Enable them only when
  troubleshooting. Bluetooth connection remains enabled. ESPHome has no
  separate YAML hidden-by-default flag; disabled diagnostics are omitted from
  the normal device view.
- Protocol status, command results, counters and the manual query button are
  internal from 0.8.2 and do not create Home Assistant entities. Their useful
  state changes are recorded at DEBUG under `gate_protocol` in ESPHome logs;
  inspect those logs when investigating failures. No authentication PIN or
  challenge is logged by these diagnostic handlers.
- **Status** (bridge connection) defaults disabled because other entities already
  show Unavailable on an ESP32 disconnect. It can be enabled for diagnostics.

Covers are non-optimistic. A command ACK does not report movement. A recent
acknowledged local opening command can identify the access mode once fresh
status confirms movement; ambiguous transmitter movement still waits for a
distinguishing controller state. When the access
mode is unknown, they retain the last confirmed position and clear the displayed
movement operation. On boot they have no trustworthy remembered gate position;
**ignore covers until Gate state is valid**. Unknown access mode during movement
can also leave cover positions unchanged even when Gate state is Open. Gate
state alone does not prove that the car/pedestrian access mode is known.

For automation conditions, accept only Gate state's raw `on` or `off`; reject
both `unknown` and `unavailable`. For example, combine two native state
conditions in OR, or use an explicit state condition for the intended Open or
Closed condition. Both covers Closed is not proof the gate is physically shut.
Opening commands are blocked if their access mode is unknown, and the optional
software-lock guard still applies. Close and Stop remain callable independently.

## Upgrading from 0.7.x helpers

Back up the three helper configurations and check all their consumers first.
Install 0.8.0, confirm native states and loss/recovery behavior, then remove only
the old car/pedestrian/Gate state Template helpers. Rename the new entities to
previous IDs if needed and preserve voice-assistant exclusions. Do not remove
existing cloud, Shelly, reed-switch or security consumers as part of this step.
The API may add a numeric suffix while old helper IDs are occupied.

Gate state changed failure semantics from helper Unavailable to native Unknown.
Update any automation that checks only for Unavailable to reject Unknown too.
Keep experimental entities out of voice assistants until physical control and
recovery testing is complete. Explicit close and Stop still need hardware tests.
