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

## Repository layout decisions

Preserve the independently maintained `librustyaxe`, `librrprotocol`, and `rustyrig-fw` repositories and their existing library linking boundaries. Do **not** consolidate subprojects simply to shorten include paths. No recursive move or mass rename is planned before release.

Keep `rrclient/gtk/` exclusively GTK-specific; keep transport, media ownership, network state and resource models in plain `rrclient/` where the TUI can reuse them. Prefer cohesive dotted filenames (`gtk.admin.users.c`, `gtk.admin.controls.c`, `gtk.resources.model.c`, `media.catalog.c`) over new nested folders that would require wholesale include/build-system churn.

Candidate extractions:
- `gtk.admin.c` → separate supervision (already extracted), controls UI, access requests, user/privilege administration, sessions and a small tab assembly unit. Introduce a private admin header for shared types and explicit state; do not export static widget globals indiscriminately.
- `gtk.resources.c` → inventory model, filter UI, tree presentation and actions, with shared state owned by one module.
- `rrclient/media.c` → channel catalog, subscription policy, status/command output. Native network protocol belongs in `librrprotocol` only when both server and client need that exact implementation.
- `libfwdspmgr/fwdsp-mgr.c` → frame I/O, child lifecycle, recording, codec lifetime. Keep subprocess ownership in libfwdspmgr rather than duplicating server logic.
- `rrserver/bridge.manager.c` → worker spawning/supervision, IPC protocol handling and media path management. Keep bridge protocol definitions in librrprotocol.
- `rrserver/serial.c`, `rrserver/objects.c` and `rrserver/relay.c` → separate lifecycle from inventory, protocol/message routing and session/media forwarding respectively.

Splits are behavioral refactors: do not simultaneously rename protocol fields, change visible GTK behavior, or revise serialization. For each extraction, update the owning component's `rules.mk`, compile the relevant build configurations and run focused tests. Maintain the user's C formatting style (braced multiline conditionals and loops).

### Refactor progress

- [x] Move independent GTK supervision prompts from `gtk.admin.c` into `gtk.supervision.c`; add it to `rrclient/rules.mk`.
- [ ] Compile and run GTK shutdown/supervision regressions for that extraction.
- [x] Extract GTK admin hardware controls into `gtk.admin.controls.c` and `gtk.admin.controls.h`; the admin entry point delegates page assembly, refresh and replies.
- [x] Extract access requests, filtering, grant review, response handling, and tab creation into `gtk.admin.access.c` with a narrow header.
- [ ] Extract admin users, permissions and sessions with similarly narrow interfaces.
- [ ] Extract Resource Explorer inventory model from GTK presentation.
- [ ] Split client media catalog/subscriptions/notices.
- [ ] Split DSP manager worker lifecycle and codec instances.
- [ ] Split bridge manager and review server serial/objects/relay.

## Wire v2 efficiency considerations (deferred from structural refactor)

- Do not compress Opus and other encoded media payloads a second time; additional compression is unlikely to offset CPU and latency overhead.
- Inventory snapshots, history and bulk object metadata may benefit from standard WebSocket `permessage-deflate` *if negotiated*; measure frame byte sizes, CPU and latency first.
- Prefer eliminating repeated verbose fields and coalescing inventory rows before introducing stateful compression dictionaries. A persistent custom dictionary complicates reconnection, resynchronization, embedded implementations and version compatibility.
- If compression is introduced, advertise it as a named v2 capability, bound expansion size, and make it opt-in per transport; never assume a compressor on a same-major peer.
- Keep server-to-server link traffic and binary frame features separately negotiated from client text-frame compression.

