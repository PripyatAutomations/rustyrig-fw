# Configuration source layout (v1.0)

RustyRig configuration handling is split between the generic parser and
subsystem handlers:

* `librustyaxe/config.c` owns INI parsing, includes, callbacks, reloads, and
  configuration dictionary lifetime. It must not know radio/server/client
  section details.
* Subsystem-specific **load and save handlers belong in `cfg.*.c`** in the
  component or shared library that owns the section. Handler registration
  should live beside the handler, not in unrelated runtime code.
* Keep runtime behavior, authorization, I/O, media handling and device logic in
  the component's normal implementation files.
* Public API declarations can remain in the owning subsystem's existing
  header; relocating an implementation file must not change the public API.

Current examples:

| Library/component | Config handlers |
| --- | --- |
| librustyaxe | `cfg.modules.c`, `cfg.serial.c` |
| librrprotocol | `cfg.fwdsp.c`, `cfg.servers.c`, `cfg.bridge.c` |
| rrserver | `cfg.acl.c`, `cfg.files.c`, `cfg.rig.c`, `cfg.peer.c`, `cfg.radio.c`, `cfg.rtp.c`, and device-specific `cfg.*.c` |
| rrclient | `cfg.network.c`, `gtk/cfg.gtkcss.c` |

## Callback contracts

- Load/section callbacks: `true` = handled successfully, `false` = error.
- Registration calls: `true` = registered, `false` = failure.
- Until the save callback API is migrated as a coordinated ABI change,
  `cfg_save_cb_t` implementations follow the historical convention:
  `false` = success, `true` = error. The *dispatcher*
  `cfg_run_save_callbacks()` returns `true` on overall success.
- `dict_add*` returns 0 on success; check the result. Preserve native
  dictionary types when merging configuration, and guard `snprintf()`
  output against truncation.

When adding a section, write tests for parsing, includes, reloads, save,
malformed entries, and failure paths. Keep handler state/lifetime explicit:
per-file state should not accidentally leak from one config reload to the next.

For public-library usage, see `librustyaxe/API.md` and `librrprotocol/API.md`.
