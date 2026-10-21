# C source size and module boundaries

RustyRig's owned C sources should be organized by responsibility, not collected into multi-thousand-line compilation units. A line-count guideline is a review trigger, **not** a mechanical splitting algorithm.

- Aim for **300–800 lines** per owned C source.
- At **1,000 lines**, require a deliberate review of separate responsibilities and documented rationale if keeping it whole.
- At **1,500 lines**, prioritize decomposition before adding substantial features.
- Treat generated files and external/vendor sources separately. Never edit generated output just to meet a count.
- Preserve public API boundaries, event ownership, signal lifecycle, lock ordering, and build profiles. Move cohesive private state with its operations rather than producing a miscellaneous "helpers" file.
- New compilation units must be included in default, no-GTK, platform and package builds as appropriate. Add behavioral tests before and after each extraction.
- Use the repository's formatter, including braces around conditional/loop bodies. Do not shorten code merely to meet a line target.

## Initial audit (2026-10-20, GitHub main)

| File | Approx. lines | Candidate responsibility split |
| --- | ---: | --- |
| `rrclient/gtk/gtk.admin.c` | 3,078 | Separate user/grant management, request handling, sessions, and controls administration widgets; keep a small admin-tab entry point |
| `rrclient/gtk/gtk.resources.c` | 2,107 | Separate inventory tree/model, type filtering, file/share operations, and selection/actions |
| `rrclient/media.c` | 1,735 | Separate channel catalog/state, subscription and routing policy, transport notices, and command/status UI |
| `libfwdspmgr/fwdsp-mgr.c` | 1,698 | Separate child process lifecycle, codec instance ownership and linger, I/O framing and recording |
| `rrserver/bridge.manager.c` | 1,486 | Separate bridge process management, IPC state and media paths |
| `rrserver/serial.c` | 1,164 | Review separate endpoint lifecycle and protocol/export responsibilities |
| `rrserver/objects.c` | 1,027 | Review object snapshot/inventory emission vs control/property handling |
| `rrserver/relay.c` | 1,006 | Review relay session routing vs forwarding and media integration |

The numbers are estimates from fetched source contents, not formatter-normalized line counts. Other components and new files still need a complete tracked-file sweep.

## Recommended sequence

1. Split `gtk.admin.c` first; the largest file and most clearly separated dialogs/tabs.
2. Split `gtk.resources.c` after Explorer tree/inventory behavior is verified to avoid losing ongoing changes.
3. Split `rrclient/media.c` with tests for subscription, active VFO, transport fallback, and reconnection.
4. Extract fwdsp and server bridge process-lifecycle units, preserving teardown order.
5. Sweep all tracked `.c` and `.h` files across component repositories; review files above the thresholds.

Do **not** treat this list as completed refactoring. Each split should be committed independently and compiled/tested before starting the next.
