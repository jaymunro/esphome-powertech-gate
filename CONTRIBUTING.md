# Adding a controller definition

Start with the controller board's actual model and the module's reported UART
protocol. Similar gate hardware or a shared app name is not proof of compatibility.

1. Copy `models/ps19001.json` to a file named after your controller model.
2. Keep the exact schema fields. `family` must currently be `WBT01` and
   `protocol` must be `legacy_uart_v1`; a different protocol requires driver work.
3. Define the observed pedestrian stopped signature, full-open minimum and exact
   closed position plus a validated `closed_position_tolerance` (0 for exact
   matching). A CLOSED status is always required; tolerance alone cannot close
   a moving/stopped gate. PS19001 accepts 0 or 1 after repeated CLOSED replies. Positions are in the controller's 0–100 protocol scale,
   not measurements of separate leaves. Disable pedestrian support if unproven.
4. Record tested firmware and what was physically verified in `validation`.
5. Add sanitized fixtures/tests for idle closed, pedestrian opening/open/auto-close,
   car opening/open/auto-close, explicit close, Stop, reconnect buffered messages,
   malformed replies and missing feedback. Include cases where the opening mode
   cannot be known; never infer it from a generic PED label or a command ACK.
   Set `opening_event_mode_hint` to false unless exact C004 Opening/PedOpening
   labels have been corroborated against wired and transmitter movements on the
   documented firmware. This enables a guarded hint, never notification-only
   physical state. Test reconnect replay and contradictory event/status evidence.
6. Run generation, host tests and an ESPHome example compile. Open a PR with
   the model/firmware, a concise test matrix and remaining limitations.

The driver authenticates with a per-device advertised name and PIN supplied by
its owner; these are not model-definition fields. Do not commit credentials,
private MAC/IP addresses, raw capture logs, account identifiers or proprietary
app code. Use synthetic sources and addresses in test fixtures.

Definitions intentionally cannot introduce arbitrary UART writes, commissioning,
force/safety settings or firmware actions. A settings feature needs validation,
original-value capture, acknowledgement and readback in its own change.

Generated files under `packages/` must match `tools/generate.py --check`.
New profiles must not silently change an existing model's behavior.
