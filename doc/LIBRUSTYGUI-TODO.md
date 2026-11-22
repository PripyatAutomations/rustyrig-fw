# librustygui — design, extraction and implementation TODO

**Status:** design proposal; not an implementation claim  
**Last revised:** 2026-11-22  
**Proposed repository:** \`PripyatAutomations/librustygui\`  
**Initial toolkit:** GTK3 / GLib / Cairo / Pango  
**Application of origin:** RustyRig (\`rrclient-gtk.so\`)  
**Companion diagram:** [LIBRUSTYGUI-ARCHITECTURE.svg](LIBRUSTYGUI-ARCHITECTURE.svg)

> This is a decision log and executable migration checklist. Treat APIs, event
> names, wire formats, and package naming below as **proposals**, not promises
> of existing functionality. Update this document as decisions are finalized.

## 1. Goals

Make the reusable parts of RustyRig's GTK interface available to other applications,
especially other radio, SDR, DSP, audio and test-equipment programs.

1. Reuse its floating panels, tabs/workspaces, docking, window positioning, adaptive
   toolbars, content scaling, zoom, touch support, dialogs, file browsers, and canvas.
2. Include radio-centric presentation widgets: frequency entry, RF/AF spectrum,
   waterfall, signal/S meters, VFO presentations, mode/bandwidth selectors and
   associated markers/controls.
3. Include reusable routing/graph/drawing facilities; never make the graph renderer
   responsible for executing actual DSP, PTT or network operations.
4. Drive widgets through the existing **librustyaxe event bus** and unique
   identities. Native C and RustyScript should use the same vocabulary.
5. Allow multiple independent windows, workspaces and application instances.
   No hidden dependency on the singleton \`rrclient\` GTK globals.
6. Move application-specific state, radio/network backends, authentication and
   configuration ownership **out** of the library.
7. Keep \`rrclient\` headless and preserve dynamic GTK module loading.
8. Migrate in small testable steps, preserving user layouts and established
   interactions; no visual redesign is required just to extract code.
9. Make it possible to use the general-purpose GUI without importing a radio
   backend or enabling scripting.
10. Provide build-time and runtime diagnostics for widget/event ownership,
    resource leaks, queued callbacks, and failed request/reply conversations.

### Non-goals

- Implementing CAT, SDR receivers, audio transport, decoding, DSP, PTT or rig
  authorization in \`librustygui\`.
- Replacing GTK3 immediately or inventing a cross-platform renderer in phase 1.
- Assuming every radio widget must be backed by RustyRig's VFO/room structures.
- Treating event transport as authorization or exposing arbitrary GTK methods
  to untrusted events.
- Rewriting all of RustyRig's GTK frontend in one commit.
- Forwarding high-rate FFT or PCM data through JSON one event per sample.

## 2. Dependency and packaging policy

\`\`\`text
      RustyRig rrclient-gtk.so        Other GTK radio/desktop applications
                   \                       /
                    \                     /
                 librustygui (GTK3 / GLib / Cairo / Pango)
                       |              \
                 librustyaxe       librustyscript (optional)
                       |
               event bus and core utilities

      RustyRig-specific integration stays in rrclient-gtk.so:
      database, rig/VFO models, IRC/chat rooms, DSP, audio, network, privileges
\`\`\`

- **Required:** GTK3, GLib/GObject, GDK, Cairo and Pango at the appropriate
  dependency level; \`librustyaxe\` for event bus and common facilities.
- **Optional:** \`librustyscript\` support. Resolve whether this is a separate
  library/package or a component supplied by \`librustyaxe\` before finalizing
  pkg-config metadata. No hard link to the interpreter for core widget users.
- **Prohibited dependencies:** \`rrclient\`, \`rrserver\`, \`librrprotocol\`,
  hamlib, rig/audio transport layers, GStreamer, IRC/rooms, or RustyRig's database.
- Existing GTK frontend remains loadable as \`rrclient-gtk.so\`; do not add GTK
  linkage to headless \`rrclient\`.
- Prefer separate pkg-config modules/features, rather than premature separate
  repositories: \`rustygui\` core, optional \`rustygui-radio\`, optional
  \`rustygui-script\`. Confirm packaging, installed headers, ABI versioning and
  SONAMEs during setup.
- Use versioned, documented public headers such as \`<rustygui/app.h>\`,
  \`<rustygui/panel.h>\`, \`<rustygui/radio/spectrum.h>\`.
- Support Devuan/Debian packaging and \`rrpk\` \`.modules\` integration. Add
  repository in dependency order, with \`make test\` (not \`make check\`).
- License **decision pending**: existing extracted files are GPL-2.0-only
  unless all rights permit relicensing. Evaluate LGPL-2.1-or-later (or another
  suitable license) only with copyright-holder approval; do not silently alter
  historical source license notices.

## 3. Proposed source tree

\`\`\`text
librustygui/
  GNUmakefile                 make / make test / deb / install / clean
  README.md
  include/rustygui/
    app.h  window.h  workspace.h  panel.h  toolbar.h
    registry.h  event.h  settings.h  zoom.h  scale.h
    dialog.h  file-browser.h  editor.h  graph.h
    radio/
      frequency.h  freqentry.h  spectrum.h  waterfall.h
      axis.h  markers.h  meter.h  vfo.h  mode.h
    script.h
  src/
    core/                     context, registry, lifetime, event routing
    window/                   window manager, workspace, panes, docking
    widgets/                  toolbar, dialogs, file browser, controls
    radio/                    frequency, spectrum, waterfall, meters, VFO
    graph/                    nodes, pins, canvas, drawing tools
    scripting/                optional RustyScript bridge
  tests/                      unit, headless model, GTK integration, stress
  examples/
    panel-demo.c
    spectrum-demo.c
    script-ui-demo.rrs
    routing-demo.c
  doc/
    API.md  EVENTS.md  MIGRATION.md  INTERNALS.md
\`\`\`

The exact filenames are flexible; component ownership and dependency direction
are more important than naming.

## 4. Existing RustyRig code inventory and extraction order

These paths exist in \`rustyrig-fw\` and are intended starting points. Recheck
the current tree before any move.

| Current source | Target | Boundary work |
| --- | --- | --- |
| \`rrclient/gtk/gtk.local-browser.[ch]\` | generic file browser | Already largely independent; retain injection of actions/status callbacks |
| \`rrclient/gtk/gtk.scale-container.[ch]\` | panel/widget zoom | Isolate widget geometry and CSS behavior from application state |
| \`rrclient/gtk/gtk.floating-panel.[ch]\` | panel + workspace docking | Remove \`rrclient/database.h\`, \`gtk.tabs.h\`, chat-room identity and singleton globals |
| \`rrclient/gtk/gtk.winmgr.[ch]\` | window/widget registry | Remove application-global window list and \`gtk.core.h\` entanglement |
| \`rrclient/gtk/gtk.zoom.c\` | global zoom + touch | Move configuration/logging/hotkey integration behind callbacks |
| \`rrclient/gtk/gtk.window.c\` | reusable workspace/toolbar primitives | Leave RustyRig's actual top-level UI construction, tabs, connections and actions in client |
| \`rrclient/gtk/gtk.freqentry.[ch]\` | \`radio/freqentry\` | Remove broad \`librustyaxe/config.h\` / app-specific inclusions; use explicit frequency model |
| \`rrclient/gtk/gtk.spectrum.[ch]\` | RF/AF spectrum/waterfall | Extract renderer, axes, pan, zoom, palettes, markers; move SDR/room selection and data acquisition out |
| \`rrclient/gtk/gtk.graph.[ch]\` | generic graph renderer | Keep view objects pointer-free; add reusable interaction/model layer separately |
| \`rrclient/gtk/gtk.script-gui.c\` | script UI adapter | Existing \`owner:id\` registry is seed for new registry; remove room/app specific widgets |
| Existing dialogs, control panel, file editor | dialogs/controls/editor | Split generic presentation from workflows |
| Rig/mode/volume/VFO GTK widgets | \`radio/\` and \`widgets/\` | Extract presentation logic only; hardware/radio command logic stays client |

**Important existing couplings:** Floating panels already contain chat-specific
dock state, \`rrclient_database_setting_set\` calls and generic panel mechanics
in one module. \`gtk.window.c\` contains both reusable adaptive-toolbar layout
and RustyRig-specific window actions. \`gtk.core.h\` imports radio/protocol
headers and defines singleton GUI globals. This is not a file-copy exercise.

## 5. Library objects, ownership and threading

### 5.1 Context

Proposal:

\`\`\`c
RGApp *app = rg_app_new("org.pripyatautomations.rustyrig");
RGWorkspace *ws = rg_workspace_new(app);
RGPanel *panel = rg_panel_new(ws, "radio.spectrum.main", "RF Spectrum");
rg_panel_set_resizable(panel, TRUE);
rg_panel_show(panel);
\`\`\`

- \`RGApp\` holds the registry, settings adapter, event router, GTK execution
  domain and diagnostics. No process-global \`RGApp *current\`.
- \`RGWindow\` owns windows; \`RGWorkspace\` owns layout regions and panels;
  \`RGPanel\` owns chrome/body/configuration; GTK widget reference rules must be
  explicit. Consider GObject subclasses where lifecycle/signals benefit.
- All GTK mutation happens on GTK's owning main context, even when a request
  originates from a worker thread. Avoid nested main loops for request/reply.
- Event callbacks must not retain bare GTK pointers across dispatch.
- Unregister callbacks, cancel timers and drain queued domain messages before
  destroying state or unloading the dynamic module. Follow
  \`doc/client-modules.md\` lifecycle rules.
- Support multiple independently instantiated views over a shared immutable
  spectrum/frame model.

### 5.2 Persistence adapter

**Do not link to** \`rrclient_database_setting_get/set\`. Define an interface for
loading/saving named, versioned UI state (callbacks or GInterface) with explicit
ownership and error behavior. A sensible first backend is a local app-owned
settings store; RustyRig supplies its existing SQLite adapter.

Persist: logical widget IDs, workspace/dock region, floating geometry,
roll/minimize/pin/visible state, zoom, opacity, saved layout version, UI theme
choices and application-supplied widget state. Restore screen-aware geometry
safely when displays/dpi/resolutions change. Avoid persisting temporary hide
masks, sensitive values or hardware state by default.

**Migration requirement:** read old RustyRig panel state once and preserve
existing layout behavior, including any narrower preferences; do not reset
everyone's floating windows just because the code moved.

## 6. Identity model

Every UI object has three distinct concepts:

| Item | Example | Lifetime | Use |
| --- | --- | --- | --- |
| Logical ID | \`radio.spectrum.main\` | Persistent across sessions | Scripting, configuration, saved layout |
| Instance ID | \`ui:7c3e2a91\` | One creation/destroy lifetime | Exact routing; prevents stale replies hitting recreated widgets |
| Request/correlation ID | \`req:1042\` | One request | Match response to originating request |

Additional \`owner\` or namespace identity (for example \`script:scanner\`)
isolates scripts/plugins. **Uniqueness scope:** require logical IDs unique
within an app namespace or workspace namespace; define the namespace
unambiguously and reject collisions, rather than silently picking a widget.
Instance IDs must be unique within the process/runtime; use sufficiently
unpredictable identifiers if crossing trust boundaries, but do not mistake them
for credentials. Request IDs are unique among pending requests and scoped to
the requesting process/registry. Identify the caller/namespace separately.

Retain the existing RustyRig script widget notion of \`owner:id\` as a
migration mapping, not the final globally concatenated identifier API.

### Registry obligations

- Register on construction, unregister on destroy; never return dangling
  references. Use weak GTK references where appropriate.
- Resolve exact instance ID or scoped logical ID; no implicit first-match.
- Duplicate registration must error with diagnostics.
- Every action is typed, documented and discoverable (including read/write
  property support, accepted payload schema and emitted event schemas).
- Each widget exposes **capabilities**, not arbitrary method invocation.
- Provide enumeration and lookup with explicit visibility/permission filtering;
  exposing all widgets to an untrusted script is not safe.
- Destroying a widget fails pending requests with \`target_gone\` and cancels
  subscriptions owned by that widget.
- Recreated widgets get **new** instance IDs even when logical ID is identical.

## 7. Event transport: use librustyaxe, not a competing bus

The existing \`librustyaxe/event-bus.h\` already supplies:

- \`rr_event_message_t\` with \`name\`, \`sender\`, owned/copyable \`payload\`,
  \`length\`, \`flags\`;
- \`event_emit_message\` and \`event_on_message_domain_token\`;
- named dispatch domains including \`gtk\`;
- detachable listener tokens, \`event_off_token\`,
  \`event_message_token_drain\`;
- GLib-independent dispatch adapters.

Use these facilities. Define an **envelope inside the payload** and routing
facade in \`librustygui\`; do not casually extend \`rr_event_message_t\` ABI.
Proposed event names:

- \`ui.request\` — command/query addressed to one widget or service;
- \`ui.response\` — correlated success or failure;
- \`ui.event\` — unsolicited user interaction/state change.

Use JSON for human-readable control messages initially, with an explicit
schema/version. CBOR or another typed representation may be added later if
needed. **All examples below are illustrative protocol proposals.**

### 7.1 Request

\`\`\`json
{
  "v": 1,
  "id": "req:1042",
  "from": "script:scanner",
  "to": {"logical": "radio.spectrum.main", "namespace": "client"},
  "action": "frequency.get",
  "payload": {},
  "deadline_ms": 1000
}
\`\`\`

### 7.2 Response (targeted to requester, not a global broadcast)

\`\`\`json
{
  "v": 1,
  "reply_to": "req:1042",
  "from": {"logical": "radio.spectrum.main", "instance": "ui:7c3e2a91"},
  "to": "script:scanner",
  "status": "ok",
  "payload": {"frequency_hz": 14074000}
}
\`\`\`

For errors, use \`status:"error"\` plus an error object with a stable
\`code\`, short \`message\` and optional details. Proposed codes:
\`invalid_argument\`, \`unsupported_action\`, \`not_found\`,
\`ambiguous_target\`, \`forbidden\`, \`target_gone\`, \`timeout\`,
\`cancelled\`, \`busy\`, \`internal_error\`.

### 7.3 User event

\`\`\`json
{
  "v": 1,
  "from": {"logical": "radio.spectrum.main", "instance": "ui:7c3e2a91"},
  "type": "spectrum.tuned",
  "cause": "user",
  "payload": {"frequency_hz": 14074000}
}
\`\`\`

Use consistent units: frequency as signed/unsigned **integer Hz** where
possible; documented floating values for fractional-Hz applications, never
implicit kHz/MHz. State which coordinate system graph events use, and keep
generation/sequence timestamps for high-rate view updates.

### 7.4 Request/reply lifecycle

1. API creates unique request ID and registers a pending callback/future in
   the **originating context** with expected target, generation, deadline and
   cancellation handle.
2. Validate payload size, action name, schema and the caller's capabilities.
3. Resolve an exact registry entry; capture its instance ID for the request.
4. Dispatch to GTK domain, using existing bus owned-message delivery.
5. Check target generation/instance again before invoking the action.
6. Widget evaluates action and emits \`ui.response\` containing
   \`reply_to\`, real instance ID and same caller identity.
7. Response router matches the pending table entry, sender identity and
   correlation ID; invoke callback once on caller's registered context.
8. On timeout, shutdown, cancellation or target destruction, complete once
   with a structured error; ignore and count late/duplicate responses.
9. Unsubscribe and drain handlers safely when destroying app/module.

**Key design detail:** A unique reply address need *not* be a dynamically
created event name. Use a dedicated registered requester identity and a
pending-request map keyed by correlation ID; \`ui.response\` carries \`to\`
and \`reply_to\`. This is simpler than ephemeral listeners for every request,
though per-request one-shot callbacks remain an ergonomic API. Protect the
pending map against spoofed \`reply_to\` messages and cross-context collisions.

**Threading:** never execute GTK code inline from arbitrary emitter threads;
marshal requests into the registered GTK event domain. Prefer async callbacks
or future-like completion over blocking/synchronous waits (especially from GTK).
Do not assume \`event_emit_message\` is a network transport.

**Broadcasts:** only explicit publish/subscribe \`ui.event\` (and a separately
specified broadcast action if justified). A missing \`to\` field is invalid for
\`ui.request\`; it never means "all widgets".

### 7.5 Proposed native C API

\`\`\`c
RGRequest *rg_ui_request(RGApp *app, const char *target,
                         const char *action, const RGValue *args,
                         RGReplyFunc done, void *user);

gboolean rg_ui_send(RGApp *app, const char *target,
                    const char *action, const RGValue *args);

RGWidgetHandle *rg_ui_lookup(RGApp *app, const char *logical_id);
void rg_request_cancel(RGRequest *request);
\`\`\`

\`RGValue\` and \`RGWidgetHandle\` are **placeholder types**: decide whether
to adopt the relevant existing \`librustyaxe\` value/dict types or define a
small stable versioned variant interface. Avoid stringifying every property
just to pass it back into C. Also define cancellation, timeouts, callback
ownership, user-data destroy callbacks and error lifetime in public API docs.

Simple synchronous getters/setters may be offered **only on the GTK owner
thread** and should remain separate from event-based remote/script entry.

### 7.6 Diagnostics and observability

Expose pending request count, event queue depth/latency, request timeouts,
late and duplicate responses, per-widget action counts and dispatch latency,
registered logical/instance IDs, destroyed-widget/weak-ref counts, and action
validation errors. Integrate with existing \`librustyaxe\` event profiling.
Provide an opt-in dump for debugging; avoid flooding ordinary application logs.

## 8. Widget capability/property model

Every widget should advertise a documented type and a small set of properties,
actions and signals. Examples:

| Type | Properties | Actions | Emitted events |
| --- | --- | --- | --- |
| \`ui.panel\` | title, bounds, zoom, opacity, pinned, dock | show, hide, present, fit | moved, resized, docked, closed |
| \`ui.toolbar\` | actions, compact mode | activate, set-enabled | activated |
| \`ui.freqentry\` | frequency_hz, min/max, step, enabled | focus-digit, reset | changed, committed |
| \`ui.spectrum\` | center/range, gain, palette, selected marker | tune, pan, zoom, follow | tuned, zoomed, panned, selected |
| \`ui.waterfall\` | palette, intensity, span, time history | clear, freeze, seek | selected, panned |
| \`ui.meter\` | value, scale, units, thresholds | reset-peak | peak, threshold |
| \`ui.graph\` | nodes, edges, selection, zoom | select, auto-layout, annotate | connected, disconnected, moved |
| \`ui.filebrowser\` | directory, filter, selection | refresh, select | selection-changed, action |

Prefer GObject signals/properties for intra-GTK use, with an explicit
bridge to the typed event vocabulary. Prevent feedback loops:
programmatic \`frequency.set\` should carry \`cause:"program"\` and not
pretend to be a user tune action. Support an action origin/transaction ID
when UI changes may round-trip through model/controllers.

## 9. Radio, SDR, DSP and audio widget requirements

### 9.1 Shared data model vs renderer vs backend

Separate three layers:

- **Model:** frequency axes, FFT frames, spectral bins, waterfall rows,
  tuning markers, bandwidth, ranges, selection and rendering state;
- **View/controller:** GTK/Cairo drawing, pointer/touch interaction, viewport,
  keyboard shortcuts, palettes, pan, zoom and labels;
- **Source/application:** SDR/rig/device acquisition, FFT calculations,
  audio routing, network frames, permissions and actual tune commands.

A widget may show synthetic/demo input, a Kiwi stream, a local RTL-SDR
stream, an AF spectrum or an FFT returned by another program, without
importing their transport libraries.

### 9.2 Spectrum and waterfall

- Unify frequency-axis logic between RF and AF visualizations; do not force
  the same units or operating range.
- Explicit \`min_hz\`, \`max_hz\`, \`center_hz\`, visible/span bandwidth and
  source-supported resolution; no magic defaults (notably no forced FT8
  14.074 MHz start frequency).
- Pan, mouse-wheel zoom, zoom control, touch pinch/drag, keyboard controls,
  click tune, double-click zoom, markers, receiver passbands, hover readout,
  palette/gain controls and configurable opacity.
- Preserve RustyRig's established semantics: **single click tune, double
  click zoom, drag pan**; clear distinct gestures and maintain touchscreen
  hit targets.
- Follow active source/VFO optional, never mandatory; support multiple VFO
  markers. View resizes without changing data frequency span unexpectedly.
- Provide rendering preferences and source capability bounds; clamp zoom
  to valid/source-supported ranges; never scroll beyond data edge.
- Support line spectrum, waterfall, combined display, and AF stereo
  channels/ear selection where the view model provides them.
- Use versioned, immutable, bounded frame structures for rapid updates;
  binary message/buffer transport or explicit frame submission for FFT data,
  not giant JSON control messages. Define copies vs reference-counted
  buffers and producer/consumer backpressure (drop stale frames, never
  block audio/network threads).
- Consider render throttling/coalescing; no per-frame UI event-bus
  subscriptions that grow without bound.
- Include reproducible palettes, reference frequency ticks, performance
  tests and screenshot/golden tests.

Illustrative native API:

\`\`\`c
GtkWidget *rg_spectrum_new(RGSpectrumKind kind);
void rg_spectrum_set_range(GtkWidget *view, int64_t lo_hz, int64_t hi_hz);
void rg_spectrum_set_center(GtkWidget *view, int64_t center_hz);
void rg_spectrum_push_fft(GtkWidget *view, const float *bins, size_t count);
\`\`\`

These are **sketches only**. Final API needs FFT metadata: timestamp, sample
rate, center/bin spacing, amplitude units, channel and ownership.

### 9.3 Frequency entry / VFO / modes

- Make \`GtkFreqEntry\` a standalone reusable widget, not dependent on chat
  or \`rrclient\` mode/VFO globals.
- Preserve digit-wise focus, editing-state queries, keyboard/scroll/touch
  interaction, formatting, decimal precision and explicit unavailable state.
- Frequency types should support sufficiently large integer Hz values,
  fractional-Hz where needed, signed offsets and localized presentation
  without losing precision.
- Distinguish in-progress edit from committed tune; an external update must
  not overwrite a partially edited value unexpectedly.
- VFO presentation should consume generic state snapshots; allow variable
  VFO counts/names, A/B as optional labels, and state refreshed immediately
  on switching the selection.
- Mode selector and bandwidth/step controls should be reusable UI components
  driven by source-provided capabilities, not hamlib enums compiled into GUI.

### 9.4 Routing graph and drawing

- Build on \`gtk.graph.[ch]\`'s already pointer-free view objects.
- Distinguish pin types: AUDIO L/R, PTT, COS, CONTROL, serial/data/other;
  preserve current visual conventions (input hollow/output solid;
  AUDIO cyan/magenta, PTT red, CONTROL green, COS yellow).
- Generic graph owns selection, zoom, pan, drag, snapping, multi-select,
  move-as-group, wires, highlights, annotations, undo and layout tools.
- Application validates real connections and decides if a PTT route is
  permissible. **Never allow generic GUI connection actions to bypass TX
  permissions or interlocks.**
- Persist graph geometry and annotations separately from connection model.
- Support touch-friendly pins and tool controls, context menus, explicit
  auto-layout, hover descriptions, dynamic graph updates without losing
  selection and source/sink conversion.
- Reuse with audio, digital modems, effects, signal pipelines, other
  nonradio flow editors.

## 10. RustyScript integration

- Reuse the language's event and host-binding machinery, not a GTK-specific
  scripting runtime fork.
- A script owns an isolated namespace and explicitly created widget IDs.
  Its widgets are removed or detached cleanly at script unload.
- Script API mirrors native properties/actions and receives events with
  logical ID, instance ID and origin.
- Queries use correlated async results; do **not** block GTK waiting for
  script evaluation. Design \`await\`/callback semantics to match the actual
  RustyScript language/runtime, not a hypothetical JavaScript promise API.
- No raw \`GtkWidget *\` or arbitrary C pointers exposed to scripts.
- Validate widget type, action and field schemas. Limit widget counts,
  creation rates, event sizes, timers and outstanding requests per script.
- Permissions/capabilities are supplied by the application host. A
  \`script:foo\` name is identity metadata, **not** authentication.
- Protect file chooser/editor and other privileged operations with explicit
  scope/authorization rather than merely knowing a widget ID.
- Preserve/reconcile existing \`rrclient/script.gui.h\` and
  \`gtk.script-gui.c\` APIs during migration.

Illustrative intent, **not final RustyScript syntax**:

\`\`\`text
ui.panel.create("scanner", {"title":"Scanner"});
ui.widget.set("radio.spectrum.main", "center_hz", 14074000);
ui.widget.request("radio.spectrum.main", "frequency.get", {});
// Asynchronous response is matched by correlation ID.
\`\`\`

Prefer a single coherent built-in API over many special-purpose script
commands. Scripts can subscribe to widget events through the same event bus
as native application code.

## 11. Window, workspace, panel, toolbar and touch behavior

- Explicit objects instead of global \`main_window\`, \`gui_windows\` and
  \`rr_main_toolbar\`; no panel ownership by a particular chat tab.
- Generic docks (N/E/S/W/floating and optional center/tabbed) with
  per-workspace destinations. RustyRig's room-specific attachment becomes
  an application policy binding onto generic workspaces.
- Preserve compact/zoom-only panels, headerless draggable controls,
  double-click title roll-up/down, minimized tabs clickable across their
  entire target, pin, resize limits and save/restore exact position.
- Geometry, layout and zoom must be separate concerns: zoom should scale
  the whole visual arrangement coherently; content-sized panels should
  keep natural size and not expand merely because parent is wide.
- Global zoom reset to 100% (Ctrl-0 in RustyRig adapter), plus local panel
  zoom; no double scaling when a custom Cairo widget handles its own scale.
- Adaptive toolbars: icons first, labels shown when space allows, overflow
  handling, user-defined actions, action enablement, shortcut hints, touch
  padding and accessibility names.
- Maintain keyboard-only interactions, focus preservation and
  accessibility; do not rely solely on hover or right-click.
- Common reusable file browser with injected actions/status callbacks,
  configurable filters and selection preservation. Explicit safety policy
  for symlinks/path traversal and sandbox roots.
- Configurable theme/icon source; do not force RustyRig resources on other
  applications.

## 12. Testing plan (required for each extraction)

All component repositories should implement \`make test\`, and \`rrpk/test-all.sh\`
should run them in dependency order while preserving combined stdout/stderr.

**Test categories:**

1. **Core registry:** duplicate IDs, lookup namespaces, instance generation,
   unregister/recreate, weak pointer cleanup, multiple apps/workspaces.
2. **Messaging:** simultaneous request IDs, out-of-order replies, wrong
   sender, duplicate/late responses, timeout/cancel/teardown, malformed
   JSON/oversize requests, invalid actions, capability rejection.
3. **Concurrency:** worker-thread requests marshalled onto GTK domain,
   no GTK operations from foreign threads, no deadlock/reentrant sync waits.
4. **Module lifecycle:** unload/reload, event token drain, pending callbacks,
   no dangling script/UI hooks after \`dlclose\`.
5. **Persistence:** save/restore, old RustyRig state migration, screen
   resizing, missing displays, zoom, pinned/minimized/docked state.
6. **Geometry:** responsive toolbars, content-sized/compact panels,
   min/max, snapping, mouse and touch behavior, no unexpected scrollbars.
7. **RF/AF:** axis math, clipping, zoom, pan bounds, frequency precision,
   source capability limits, marker hit tests, frame ownership, high-rate
   performance/drop behavior, no forced frequency default.
8. **Frequency entry:** edit/commit distinction, external updates during edit,
   digit focus, range/precision validation.
9. **Graph:** pin type compatibility, drag/select/multiselect, wire display,
   auto-layout, persisted coordinates; application validation remains
   authoritative.
10. **Scripting:** widget ownership, unload cleanup, response matching,
    no arbitrary GTK pointers, permissions and resource quotas.
11. **Packaging:** fresh install with/without optional scripting, correct
    header and pkg-config paths, no RustyRig dependency from library.
12. **Compatibility:** GTK module + headless client builds, existing RustyRig
    test suites, screenshots/interaction regression cases.

Run UI integration tests under a display server or Xvfb where practical,
and pure models/event tests without GTK display. Avoid fragile tests that
depend on exact theme rendering; use selected screenshot goldens for pixel
sensitive behavior. Provide a small demo program for each major widget.

### Acceptance bar for each migration

- \`make test\` passes in \`librustygui\` and the consuming applications;
- no new dependency from headless RustyRig to GTK;
- application can still reproduce existing control behavior/layout;
- new library module uses no \`rrclient/*\` includes/symbols;
- clean module teardown, with no pending callbacks referencing unloaded code;
- public headers and example usage are documented;
- log evidence/screenshot captured for visible UI behavior when relevant.

## 13. Staged implementation checklist

### Phase 0 — decisions and contracts (before major code movement)

- [ ] Agree repository name \`librustygui\`, licensing and contributions.
- [ ] Set initial GTK3-only policy and supported platforms.
- [ ] Decide header prefix, \`rg_\` symbol prefix, visibility, SONAME and
      pkg-config naming.
- [ ] Resolve \`librustyscript\` build/library packaging and optional bridge.
- [ ] Freeze v1 registry identity scope and lifecycle semantics.
- [ ] Define versioned \`ui.request\`/\`ui.response\`/\`ui.event\` envelopes;
      message limits, error codes and origin permissions.
- [ ] Decide typed value representation, callback ownership and async API.
- [ ] Choose settings backend abstraction and migration schema.
- [ ] Record current interaction behavior and layout screenshots as baselines.

### Phase 1 — bootstrap library with low-coupling components

- [ ] Create \`PripyatAutomations/librustygui\` with GNUmakefile, pkg-config,
      test runner, install and Debian packaging.
- [ ] Add \`librustygui\` to \`rrpk/.modules\` in dependency order; update
      build/install scripts as needed.
- [ ] Move generic file browser with clean includes and tests.
- [ ] Move \`gtk.scale-container\` with geometry, zoom and CSS tests.
- [ ] Create demo application proving the library runs without RustyRig.
- [ ] Add compatibility includes/wrappers in RustyRig; preserve behavior.

### Phase 2 — context, registry and event request/reply layer

- [ ] Introduce \`RGApp\`, workspace and explicit ownership.
- [ ] Implement namespace-scoped logical IDs, per-lifetime instance IDs,
      duplicate protection and discoverability.
- [ ] Bind onto \`librustyaxe\` named GTK dispatch domain.
- [ ] Implement pending-request table, correlation matching, deadlines,
      cancellation, target-gone completion and shutdown drain.
- [ ] Validate action/property schemas and caller capability policy.
- [ ] Add diagnostics and adversarial/parallel event tests.
- [ ] Port RustyRig's existing script \`owner:id\` lookup as an adapter.

### Phase 3 — panels, windows, workspaces and toolbars

- [ ] Split generic floating panel state/chrome from room-specific dock logic.
- [ ] Replace \`rrclient_database_*\` calls with persistence adapter.
- [ ] Replace per-process singleton panel/window registries.
- [ ] Extract docking, tabbing, geometry persistence, floating/compact modes.
- [ ] Extract adaptive toolbar actions, overflow, keyboard/touch behavior.
- [ ] Extract global zoom and touch handlers, removing app-specific callbacks.
- [ ] Integrate RustyRig UI through temporary wrappers and compare behavior.
- [ ] Remove obsolete local implementations after tests pass.

### Phase 4 — radio and signal widgets

- [ ] Extract frequency-entry widget/model.
- [ ] Separate spectrum/waterfall frame model, renderer and backend glue.
- [ ] Consolidate RF/AF axes, pan/zoom, palettes, overlays, markers,
      source capability clamping and touch behavior.
- [ ] Extract meters, mode/bandwidth selector and VFO presentation.
- [ ] Introduce typed binary frame ingestion and backpressure policy.
- [ ] Prove standalone simulated SDR/FFT demo with no rig backend.
- [ ] Wire RustyRig source snapshots/events into reusable views.

### Phase 5 — graph, canvas, scripting and broader controls

- [ ] Move pointer-free graph drawing primitives.
- [ ] Add reusable interactive graph model, pins, layout/selection,
      grouping, annotations, zoom and undo.
- [ ] Move generic dialogs, file editor and floating control utilities.
- [ ] Add optional RustyScript widget creation and event bridge.
- [ ] Test permission boundaries, script unload and request cancellation.
- [ ] Provide library examples for panels, spectrum, graph and scripts.

### Phase 6 — integration, stabilization and release

- [ ] Remove transitional \`rrclient/gtk\` copies and wrappers once unused.
- [ ] Re-run full \`rrpk/test-all.sh\` and all relevant Debian build profiles.
- [ ] Confirm \`rrclient-gtk.so\` load/unload and TUI-only client remain sound.
- [ ] Audit installed headers for forbidden dependencies.
- [ ] Audit ABI, symbol visibility, memory/thread ownership and licensing.
- [ ] Document migration from existing RustyRig APIs and layout storage.
- [ ] Tag first release only after independent demo apps and RustyRig pass.

## 14. Unresolved design questions for next review

1. **License:** do all relevant contributors agree to any relicensing from
   GPL-2.0-only? If not, GPL constraints follow extracted implementations.
2. **GTK3 future:** keep GTK3 API native now, or insert an internal widget
   renderer interface to ease possible GTK4 migration later? Do not overbuild.
3. **Typed payloads:** existing \`librustyaxe\` dict/value representation vs
   JSON/CBOR wrapper and fixed-size binary frame protocol.
4. **Who creates IDs:** auto-generated instance IDs mandatory; should logical
   IDs be supplied only by app/script or can auto-named transient widgets
   be addressed?
5. **Namespace scope:** app-global or per workspace, and how are nested
   script/plugin namespaces normalized?
6. **Result routing:** one pending table per \`RGApp\` with \`to/reply_to\`
   matching, or a separate requester object per script/plugin? Prefer latter
   as an ownership facade over the former.
7. **Discovery:** whether and how scripts may enumerate widgets; what is
   hidden behind permissions.
8. **Frequency representation:** integer Hz plus explicit fractional
   precision, vs a rational frequency model for high precision.
9. **FFT buffers:** owned copies, refcounted immutable frames, bounded
   queues, and optional zero-copy handoff.
10. **Settings backend:** callbacks, a standalone settings interface, or
    existing librustyaxe config/database services.
11. **Radio component packaging:** build all modules into one library, or
    publish optional \`rustygui-radio\` ABI after API settles?
12. **Script syntax:** align examples with actual evolving RustyScript
    parser, especially references, const parameters and async replies.

## 15. Immediate next steps

**Before extracting big modules**, agree the lifecycle and event/identity
contract (sections 5–7), license, and dependency graph. Then bootstrap a
standalone repo and move the relatively isolated \`gtk.local-browser\` and
\`gtk.scale-container\` pieces. Use them to validate build, packaging, tests,
headers and independent application linkage. Only then tackle the large
floating-panel/workspace and spectrum migrations.

Keep this file as the checklist of record; update checked items with commit
links and append meaningful API changes to a short decision log below.

## 16. Decision log

| Date | Decision | State |
| --- | --- | --- |
| 2026-11-22 | Extract RustyRig GTK infrastructure to \`librustygui\` | proposed/approved for design |
| 2026-11-22 | Include radio-oriented widgets, not merely generic toolkit controls | proposed/approved for design |
| 2026-11-22 | Reuse \`librustyaxe\` event bus and \`gtk\` dispatch domain | design direction |
| 2026-11-22 | Separate logical widget ID, per-lifetime instance ID and request correlation ID | design direction |
| 2026-11-22 | Keep library independent of RustyRig hardware/network/application logic | design direction |
| 2026-11-22 | Use incremental extraction with \`make test\` and \`rrpk/test-all.sh\` | design direction |
