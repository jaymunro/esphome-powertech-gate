# Home Assistant presentation entities

After adding the ESPHome device, create three helpers through **Settings →
Devices & services → Helpers → Create helper → Template**. `helpers.json`
contains the fields for two Cover helpers and one Binary sensor helper. Choose
**Gate** as the covers' device class and **Opening** for Gate state. The latter
shows Open/Closed while using on/off internally.

The example uses `powertech_gate` as the ESPHome entity prefix and
`lock.gate_lock` as the optional opening guard. Adjust these to the actual
entity IDs in your installation. Set the same lock policy in the firmware;
if using no software lock, remove the helper's lock condition deliberately.
Attach the helpers to the ESPHome device in the helper's Device field.

Under Additional options set each availability template exactly as shown.
The template entity only presents a firmware state; it does not infer timing,
combine reed switches or maintain independent state. Its actions press one
allowlisted ESPHome button. There is no position-slider action.

Keep source sensors and command buttons enabled because the helpers reference
them. Hide them from the device page after setup if desired. Useful visible
diagnostics are polling health, last control result, controller status and
Wi-Fi signal strength. Keep experimental gate entities out of voice assistants
until your installation has completed physical and recovery testing.

Car and pedestrian covers are exclusive access modes. Pedestrian open means
car closed; car open means pedestrian closed. Only Gate state represents
whether the controller reports the gate physically closed. Both covers being
closed is not a substitute for a fresh, available Gate state entity.
