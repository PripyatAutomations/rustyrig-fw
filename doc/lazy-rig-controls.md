# Lazy rig controls and summonable room widgets

## Runtime behavior
- Publish every supported rig control's descriptor in Resource Explorer without
  reading its value. Descriptor discovery must never perform CAT I/O.
- Read the individual control once when its editor opens. Mark it loading until
  readback completes; never display an invented default as the actual value.
- While an editor or room widget remains visible, subscribe to that property.
  Coalesce subscribers and refresh only subscribed properties; stop on close.
- A write uses the rig's serialized hardware worker and publishes the confirmed
  readback. Failed readbacks must not be presented as successful settings.
- Poll frequency, mode and essential radio state independently of optional
  hardware controls. Meter refresh belongs to a visible meter/widget subscription.
- For bulk edits and paired menu fields use read-modify-write, preserving any
  adjacent hardware settings. Never query a whole menu tree during idle poll.
- Preserve native units, discrete enumerations and signed offsets in the
  descriptor model. Percent is acceptable only for genuinely continuous gain
  controls; toggles are boolean, not percent sliders.
- Namespace groups receiver, filtering, transmit, CW, display, memory,
  VFO/clarifier and diagnostics. A room widget uses these exact object property
  identities; no separate CAT or room-widget command protocol.
- Authorization is server-side on each property write. Read-only and dangerous
  actions (TX, power, tuning, tuner) require explicit capabilities.
- Limit update rate for UI sliders, send the final settled value and support
  cache invalidation for front-panel changes.

## Implementation progress
The FT-891 backend advertises its current property catalog at initialization.
Normal VFO polling no longer queries the catalog. Successful setter operations
still perform CAT readback and publish the resulting state.

**Not yet connected**: an explicit per-property on-open read command, property
subscriptions/unsubscriptions, UI loading state, meter subscription, signed
offsets, multi-field CAT commands, and native enum/boolean renderers. Until
these exist, opening a control without a prior write will not populate its
hardware value. Keep this branch a draft until end-to-end reads are complete.
