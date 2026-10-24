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

## Experimental FT-991A rear audio/PTT handoff

The native backend recognizes CAT ID 0670 and, during initialization, saves
EX070/074/106 (DATA/FM/SSB input) and EX071/076/108 (DATA/FM/SSB PTT).
It selects value 1 (REAR) for inputs and value 0 (DAKY) for PTT, checks each
readback, and restores captured values during normal backend destruction.
The menu numbers and parameter values follow the official FT-991A CAT reference.
The FT-991A uses three-digit EX menu IDs; FT-891 uses four digits.
This feature requires hardware validation: confirm read/write/restore logs,
including when a setting was already at its desired value. Abrupt termination
cannot restore the in-memory values. Never assume FT-891's EX IDs on FT-991A.

## Per-mode RF power (FT-891 and FT-991A)

Requested RF power is durable in the server SQLite database, not written back
into the read-only rig configuration. The `rig_power_preferences` table is
keyed by persistent rig UUID, frequency group and logical mode class (SSB,
AM, FM, and DATA/CW). No default is inferred: if an operator has not requested
a setpoint, pre-PTT preparation leaves the radio's power untouched.

- **FT-891**: HF (<50 MHz) and 50 MHz are distinct. SSB and AM use their
  dedicated EX menus; FM and DATA/CW select the shared general-power menu.
  Separate software preferences for FM and DATA/CW are restored before TX.
  AM is capped at 40 W.
- **FT-991A**: Uses `PCnnn;` and `PC;` CAT commands for the active
  operating power; the selected mode's preferred setpoint is applied and
  read back before asserting PTT. Database band groups distinguish HF, 50 MHz,
  VHF and UHF. FT-991A DATA-FM uses `MD0A;` rather than ordinary FM `MD04;`.
- On a configured power set, the backend requires CAT readback and then saves
  the confirmed requested value. A pre-PTT read or write failure rejects
  key-down rather than transmitting at the wrong configured output level.
- This verifies the **CAT setpoint**, not measured forward RF power. Physical
  rig tests into a dummy load are still needed, especially FT-991A AM and
  higher-band limits. No general FT-991A equivalence for FT-891-only menu
  sliders is assumed.

CAT model and menu references: Yaesu FT-891 Advanced Manual (TX GNRL menu
16-01 through 16-06), and Yaesu FT-991A CAT Operation Reference Manual
(PC power, MD mode and EX settings).
