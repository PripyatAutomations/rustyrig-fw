# Adding Yaesu radios to the native backend

The model-specific registry lives in `rrserver/backend.yaesu.c`.

1. Add the radio CAT `ID;` numeric response to `yaesu_models[]`.
2. Add a `yaesu_menu_entry_t` array with the menu IDs and labels **verified in that model's CAT reference**.
3. Set `ft891_controls` only if its direct command formats and ranges have been verified identical; otherwise leave false. This intentionally avoids offering bogus sliders on a newly recognized model.
4. Menu enumeration is metadata-only (no CAT I/O). The EX read/write editor is not wired yet, so entries remain read-only and no menu values are polled.
5. Never infer a radio model from a serial path or configuration alias: use `ID;` confirmation. Unknown and disconnected radios display Unknown Yaesu rather than another rig's menu.
6. Generic descriptors, schema, values and permissions belong to the object property interface; the GTK UI must not contain Yaesu model-specific command encoders.

## Testing on real hardware
- Verify `ID;` responds `ID0650;` (FT-891) or `ID0670;` (FT-991A).
- In Resource Explorer, confirm model and MENU entries match the identified radio.
- Confirm the FT-991A does **not** show FT-891-only display/control descriptors.
- Verify regular frequency/mode polling works even with MENU collapsed.
- Record CAT transcripts for failing commands. Do not test unverified TX-related commands on the air.
- Build both the server and GTK client before merging this branch.

## Remaining work
Full model-specific EX catalogs and typed editors; explicit on-open reads;
on-demand property subscriptions; signed and multi-parameter CAT settings;
remote readback and failure propagation; expanded FT-991A direct CAT capabilities.
