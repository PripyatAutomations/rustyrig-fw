# RustyScript reference — RustyRig integration

*Reviewed against RustyRig and librustyaxe source on November 22, 2026.*
This document describes the implemented interface, including known limits; it
does not imply every code path has been build- or runtime-tested.

RustyScript lives in the **PripyatAutomations/librustyaxe** repository as
a separately installed `librustyscript` shared library. `rrclient` and
`rrserver` load it when `pkg-config librustyscript` is available at build
time. Without the development package, they build without script support.

Install the current librustyaxe source tree with `make install`, or build
the `librustyscript1` and `librustyscript-dev` Debian packages first.

For script-defined dialogs, floating panels, controls and portable event bindings,
see [RustyScript UI model](RUSTYSCRIPT-UI.md). The API describes logical
controls so future TUI and third-party native frontends can implement it.

## Source and build separation

RustyScript has separate application host implementations:

- `rrclient/script.c` → `build/rrclient/script.o`: client lifecycle, UI panel bindings, and client-specific script natives.
- `rrserver/script.c` → `build/rrserver/script.o`: server lifecycle and server-owned script bindings.
- `rrscript/host.c` → `build/{rrclient,rrserver}/rrscript/host.o`: shared, frontend-independent script loading, event subscriptions, and VM lifecycle.

The client and server **do not compile the same file as their respective
host implementation**. They each register their own capabilities into the
shared script loader, which contains no GTK/frontend dependencies.
The RustyScript interpreter itself remains in `librustyaxe/rustyscript/`.

## Script directories and autoload

`security.scripting.path` configures a **script root**, not the autoload
directory itself.

- Client: `$XDG_CONFIG_HOME/rustyrig/scripts` (usually
  `~/.config/rustyrig/scripts`)
- Server: `/etc/rustyrig/scripts` (local administrator-managed scripts)

The client package ships `ascii-art.rrs` (`/artwork`) and `hf-conditions.rrs`
(`/hf`) in `/usr/share/rustyrig/scripts/client/autoload/`. These load automatically
without a per-user copy. A matching user script takes precedence over an
administrator or packaged script; upgrades never overwrite user scripts.

Place scripts in an `autoload/` subdirectory to load them when the process
starts. Only immediate `*.rrs` files are loaded, in lexical filename order;
the parent directory and any other subdirectories are **not** executed.

```text
scripts/
    helper.rrs
    autoload/
        10-hello.rrs
        20-watcher.rrs
```

`helper.rrs` is retained but is not loaded. At startup, both hosts
create the configured scripts root and its `autoload/` subdirectory if
missing. Newly created directories use mode `0700` for rrclient or `0750`
for rrserver (subject to the process umask); existing permissions are left
unchanged. The process must have filesystem permission to create them.
Failures are reported in the `scripting` log and do not abort startup.
After adding a script to `autoload/`, restart `rrclient` or `rrserver`,
or use the appropriate script load command.

When autoload scripts fail, RustyRig logs each filename and failure detail at
`LOG_CRIT` under the `scripting` subsystem. The GTK client also displays a
single **RustyScript startup errors** dialog after autoload, with a scrollable,
selectable error report and a shortcut to the Scripts Manager. Failures do not
abort client startup or prevent other scripts from loading. Runtime invocation
failures and manual script load/reload errors are also logged at `LOG_CRIT`.
Without a graphical frontend, the errors remain available in the client log.

After startup, normal command, hotkey and external-event handlers wait
for their trigger. Two **per-script lifecycle events** are exceptions:

```c
event script.init {
    log.info("My script loaded")
    ui.panel("my-controls", "My Controls")
    ui.add("my-controls", "slider", "level", "Level", "0:100:5:50")
}

event script.fini {
    log.info("My script is unloading")
    // Optional explicit cleanup; all script-owned UI and timers are
    // released automatically immediately afterward.
}
```

`script.init` runs exactly once when that script is successfully activated,
whether via autoload, manual load or reload. `script.fini` runs exactly once
before its VM is torn down on unload, successful reload or host shutdown.
On reload the outgoing script's `fini` runs **before** the new script's
`init`. If a replacement fails to parse or register, the old script remains
loaded and neither lifecycle transition occurs. A script that never
successfully activates receives neither lifecycle callback.

These are **not published on the global event bus**; another script cannot
subscribe to them to observe someone else's lifecycle. Lifecycle handlers
execute synchronously and cannot be used as background jobs. Script-owned
widgets and timers are automatically reclaimed after `fini`, even if the
script does not explicitly call `ui.destroy` or `timer.cancel`.



The client provides these local commands, even when disconnected:

```text
/script list
/script load helper.rrs
/script unload helper.rrs
/script reload helper.rrs
/script reload autoload/10-hello.rrs
/script debug
```

A simple name searches the script root first, then `autoload/`.
Use `autoload/name.rrs` to select an autoload script explicitly.
`list` shows script filenames with their loaded/available status.
`reload` parses the replacement before unloading the old version;
a syntactically invalid replacement leaves its previous handlers active.
`unload` unregisters the script's commands, hotkeys and event subscriptions.
Files are never deleted by these operations.

`/script` is exclusively for **local rrclient scripts**. It never sends
script commands to a connected server.

### GTK Scripts manager

The main GTK toolbar has a **Scripts** button. It opens a floating local
file browser rooted in `security.scripting.path` (or
`~/.config/rustyrig/scripts` by default). The list shows relative script
filenames and **Loaded** / **Available** status. The reusable browser widget
(`rrclient/gtk/gtk.local-browser.[ch]`) provides a caller-defined action
toolbar, selection and status columns for future local file managers.

- **Load** registers the selected script's handlers; **Unload** unregisters
  them. **Reload** is enabled only when the selected script is loaded, and
  parses the replacement before removing the old handlers.
- **Edit** opens the existing GTK local file editor. Saving does not reload
  a running script automatically; choose **Reload** to apply edits.
- **Move / Rename** accepts a path relative to the script root. Use
  `autoload/name.rrs` to put it under autoload. Loaded scripts are unloaded
  before the move and loaded at the new path; an unsuccessful file move
  restores the old loaded script.
- **Delete** confirms first and unloads before removing a file. The manager
  does not traverse symlinks and refuses overwriting existing destinations.
- **Refresh** rescans the directory and updates loaded status.

Only immediate `.rrs` filenames in the root or `autoload/` are accepted
by the runtime; the browser may display other nested `.rrs` files, but
script operations are disabled for unsupported paths. Scripts in
`autoload/` will load again on the next client startup even if manually
unloaded during this session. Without optional RustyScript support the
manager remains available for file editing, but runtime actions are disabled.

Server script management is separate, under the authenticated administrative
command channel. Administrators can use `/admin script-list`,
`/admin script-load <name.rrs>`, `/admin script-unload <name.rrs>`, and
`/admin script-reload <name.rrs>` when connected to that server.
Server operations require `admin|owner` authorization, operate only on files
already present in the server's configured scripts directory, and provide no
remote upload functionality.

For example, in the client:

```sh
mkdir -p ~/.config/rustyrig/scripts/autoload
cat > ~/.config/rustyrig/scripts/autoload/hello.rrs <<'EOF'
script("/hello", {
    log.info("Hello from RustyScript")
});
EOF
```

Restart `rrclient` and type `/hello` into any appropriate chat/status
input. Check the client logs under the `scripting` subsystem.

For the server, install the same script syntax in
`/etc/rustyrig/scripts/autoload/`, owned/managed by an administrator,
then restart `rrserver`. Server scripts currently support event
subscriptions; their registered slash command bindings cannot yet be
invoked by remote users. The server supports admin-only management of
existing files through the authenticated admin protocol, but has no
script upload endpoint.

Scripts are trusted code running in the host process. They can access
files readable/writable by the host OS account and should be installed only
from trusted sources.

## Filesystem primitives (client and server)

User-owned and administrator-installed RustyScript handlers can use
`file.read(path)`, `file.write(path, text)`, `file.append(path, text)`,
`file.list(directory)`, `file.exists(path)`, `file.copy(from, to)`,
`file.rename(from, to)`, `file.remove(path)`, `file.mkdir(path)`, and
`file.rmdir(path)`. All accept normal local filesystem paths, not remote
cluster filenames. `file.mkdir` creates missing parents. `file.rmdir`
requires an empty directory; `file.remove` removes files. `file.list`
returns a newline-separated string of immediate directory entries.

`file.read` returns a UTF-8 string for a regular file up to 1 MiB;
`file.list` limits returned filenames to 64 KiB and `file.copy` handles
regular files up to 16 MiB. File writes and destructive operations use the
same permissions as the host process; these are *not* sandboxed APIs. They
do not grant network privileges or remotely change server access rights.


## Scriptable routing objects (experimental)

Native I/Q oscillators are the first exposed kind in the generic
`routing.block.*` API. These methods enqueue typed requests in the client
core domain; a `true` return means submitted, **not** a confirmed render
or hardware operation. Pin-level output connections are supported for oscillators;
graph snapshots and lifecycle notifications for other block types remain
future additions.

```c
routing.block.create("oscillator", 1, 136.5, -18, "sine")
routing.control.set("control.ctcss", 123.0)
routing.connect("control.ctcss", "osc.1.frequency")
routing.connect("osc.1.I", "tx.mix")
routing.connect("osc.1.Q", "proc.lv2.1.in.R")
routing.block.set("osc.1", "frequency", 123)
routing.disconnect("osc.1.I", "tx.mix")
routing.block.remove("osc.1")
```

Frequency, level (dBFS), and waveform each have a typed green numeric
input. A connected numeric control overrides that property; the saved
oscillator setting remains the fallback. Disconnecting a control restores
the fallback without restarting oscillator phase. Use
`routing.control.set("control.NAME", number)` for live, nonpersistent
updates; Routing → Add → Numeric Control creates a reusable saved source.
An oscillator is **not connected to a speaker, microphone, TX mixer or radio**
just because it was created. Its `I` and `Q` output pins are connectable
to `rx.mix`, `tx.mix`, `hq.mix`, `proc.lv2.N.in.L`/`.R`, or
`modemN.rx`. Both the GTK Routing view and RustyScript send the same
core-domain connection requests. The named mixer must already be available;
LV2 processors and modems must exist before being connected. TX mixer
connections remain subject to existing interlocks and do **not** key PTT. For server-side RF DDS devices, a future hardware
provider uses compatible controls without claiming to generate client audio.

## Initial registration syntax

```c
script("/hello", {
    log.info("A user invoked /hello")
});

hotkey _&^X {
    log.info("Shift+Alt+Ctrl+X")
}

event irc.privmsg {
    log.info("IRC message received")
}

event talk.msg {
    log.info("RustyRig chat message received")
}
```

`_` represents Shift, `&` Alt, and `^` Control. Braces are required.
The client dispatches script commands only when the name does not shadow an
existing built-in command; existing built-ins take precedence.
GTK dispatches custom script hotkeys after its priority shortcut handling.
The server has no hotkey dispatcher; server scripts can register event handlers.

Event data is passed as a typed `event` dictionary. IRC PRIVMSG handlers get
`event.text`, `event.nick`, and `event.room`/ `event.target` aliases
for the corresponding `talk.*` fields. The native `talk.msg` event remains
available on both sides.

Loading a script only registers handlers; it does not run them.
The initial interpreter supports scalar/dictionary values, local declarations,
arithmetic, `if`/`else` with mandatory braces, equality comparisons and
registered native calls. `log.info()` and `log.warn()` are initially exposed.

**Not implemented yet:** strict typing for all variable assignments/returns,
automatically persisted script variables (distinct from explicit client settings), `async`/`await`,
`try`/`except`/`success`/`finally`, built-in `exec()` or `subproc`,
the full routing/pipeline/control APIs, workspace restoration, and server invocation
of slash-command bindings. Some UI primitives are already implemented.
Function declarations and arrays are supported; source registration checks
braces but many handler/function body errors are still deferred until invocation.

## Core API reference (November 22, 2026)

All native functions below execute in the current script process. Client and
server share the core APIs; the extra client bindings require rrclient and
some require GTK. Values passed to APIs must have the documented types.

| Core function | Returns / behavior |
|---|---|
| `log.info(text)`, `log.warn(text)` | Write a string to the scripting log and diagnostic console |
| `string.regex(text, pattern)` | Boolean GLib regular-expression search (not automatically anchored) |
| `string.chr(number)` | One-character string for ASCII codes 1–127, including IRC control codes 3 and 4 |
| `string.slug(text)` | Filename-safe ASCII result, up to 48 characters; other bytes become underscores |
| `string.hash(text)` | First 32 hexadecimal characters of the SHA-256 checksum |
| `string.length(text)` | **Byte** length of a UTF-8 string, not Unicode code-point count |
| `string.slice(text, start[, length])` | Byte-offset substring; negative positions/lengths produce an empty string |
| `string.line(text, index)` | Zero-based line, or `null` when out of range |
| `string.lines(text)` | Number of nonempty-length line segments (trailing newline adds no extra segment) |
| `string.str(value)` | Convert string/integer/float/boolean to text; unsupported types become empty text |
| `time.now()` | Unix epoch **milliseconds** |
| `time.stamp()` | Local time string `YYYYMMDD-HHMMSS` |
| `env.data_dir()` | GLib user data directory (`$XDG_DATA_HOME` or system fallback) |
| `state.get(key)` | Copy value from current namespace, or `null` |
| `state.set(key, value)` | Set value (including dictionaries); `null` removes a key |
| `state.use(name)` | Select named shared namespace; `""` selects private script state |
| `state.namespace()` | Current namespace, or empty string for private |
| `state.namespaces()` | Newline-delimited names of shared namespaces created in this host |
| `state.keys()` | Newline-delimited keys in the current namespace |
| `timer.after(id, milliseconds)` | One-shot timer; reusing ID replaces the previous timer |
| `timer.every(id, milliseconds)` | Repeating timer; reusing ID replaces the previous timer |
| `timer.cancel(id)` | Cancel a timer owned by this script |

Timers accept numeric delays between **100 and 86,400,000 milliseconds**;
timer IDs are 1–63 bytes and are **script-local**, even with shared state.
Timer callbacks arrive as `event ui.timer` with `event.id` and only
fire for the script that registered them. They are cancelled on unload.
The shared `global` state lasts only until the client/server script
host exits; it is not persistent storage.

| Client-only function | Returns / behavior |
|---|---|
| `setting.get(key)` | String from client settings database, or `null` |
| `setting.set(key, value)` | Persist string, numeric, or boolean value; values are read back as strings |
| `chat.send("#room@network", text[, tag[, interval_ms]])` or `chat.send(rooms[], text[, tag[, interval_ms]])` | Queue into one or more joined rooms through any connected IRC/RustyRig profile; returns whether all destinations enqueued. Explicit interval 0 selects caller-managed pacing; otherwise normal flood control applies |
| `chat.cancel(tag)` | Remove this script's unsent queued messages with the supplied tag |
| `routing.block.create("oscillator", slot, hz, dbfs, waveform)` | Create/update one native I/Q oscillator (slots 1–8; waveform `sine`, `square`, `triangle`, `saw`); unconnected by default |
| `routing.block.set("osc.N", property, value)` | Configure an existing oscillator's `frequency`, `level` or `waveform` without changing its phase |
| `routing.block.remove("osc.N")` | Remove a native oscillator |
| `routing.connect("osc.N.I|Q", destination)` | Connect one I/Q source to a compatible mixer bus, LV2 effect input, or modem RX |
| `routing.disconnect("osc.N.I|Q", destination)` | Disconnect a specific sink (or use `"all"` to disconnect every consumer of that pin) |
| `routing.control.set("control.NAME", number)` | Update a numeric signal source without writing to persistent settings (suitable for modulation) |
| `routing.connect("control.NAME", "osc.N.frequency|level|waveform")` | Patch a live numeric signal to one oscillator control-input pin |
| `routing.disconnect("control.NAME", "osc.N.frequency|level|waveform")` | Remove the numeric override; restore its configured fallback |

| `window.find(pattern[, location])` / `panel.find(...)` | Look up existing panel (GTK only) |
| `window.move(id, location[, x, y])`, `window.zoom(id, percent)`, `window.opacity(id, percent)` | Change panel placement/display (GTK only); `panel.*` aliases available |
| `ui.*`, `gui.*`, `control.val/set` | Build and control logical UI widgets; see [RustyScript UI model](RUSTYSCRIPT-UI.md) |

Destination strings use **`#room@network`** (or `&room@network`) where `network` is the configured client connection/profile name, whether IRC or RustyRig WS. The legacy `profile|room` form still works. You may pass an array literal such as `["#hamradio-test@efnet", "#test@rig-profile"]` or a `char rooms[][]` variable. The optional tag defaults to `"message"`; the array limit is 32 rooms and sends are queued independently to each already-joined room. A failure may mean earlier destinations were queued; check the boolean result. The default chat interval is **1,000 ms** and explicitly supplied intervals
must be **100–10,000 ms**. A tag is 1–40 bytes and belongs to the script's
generation, preventing another script from cancelling its queued lines.
The target must already be an open chat or query. `setting.*` keys are
stored using the client `script.` prefix and survive restarts; do not use
them for temporary events or large buffers.

### Filesystem API

`file.read/write/append/exists/list/rename/remove/mkdir/rmdir/copy` are
available to both hosts. See the [filesystem section](#filesystem-primitives-client-and-server)
for path handling, size limits, and access warnings. These calls act with
the process's OS privileges, including when evaluated in the debugger.

### Value assignment, managed references, and const

RustyScript uses familiar C-style declarations, including `char rooms[][] = ["general", "radio"]`. The older `char[][] rooms` spelling remains accepted for compatibility. Text is represented by strings; a single character can be written as a one-character string.

`=` makes a value copy, including recursively copied arrays and dictionaries. `:=` binds a managed reference to an **existing variable**; assigning through that reference updates its original storage. The binding can be redirected with another `:=`. Neither operator exposes an address, and reference storage remains alive while any binding owns it.

```c
int a = 10;
int copy = a;
int alias := a;
copy = 20;      // a is still 10
alias = 30;     // a becomes 30

int other = 100;
alias := other;  // Rebind, not a value copy
alias = 200;     // other becomes 200

const int limit = 5;
const int view := a;
a = 40;         // Allowed, view observes 40
view = 50;      // Error: const binding
```

`const` is read-only **through the binding**, not a global freeze of the underlying value. A mutable reference cannot bind to a const variable; a const reference may bind to a mutable variable. Const references cannot be rebound. Const declarations require an initializer. Reference binding to expressions or temporary values is rejected.

**Current implementation:** managed, reference-counted variable storage and
user-defined typed functions with variable parameters passed by reference.
Expressions/literals become temporary parameter values. Const arguments cannot
be passed to mutable parameters. Host-native functions retain their existing
value-copy API. Nested member mutation and recursively enforced const are not
yet supported, nor is a cyclic tracing GC. See below for available statistics.

## Execution and language limits

The current interpreter supports `int`, `float`, `bool`, `char`,
strings, `char rooms[][]` array variables, array literals (`["a", "b"]`), and `dict` values; local declarations/assignments,
arithmetic, comparisons, property access (`event.room`), synchronous
native calls, and `if/else` blocks. Curly braces are mandatory for
conditionals; semicolons are optional separators. Line comments may use
`//` or `#`; block comments use `/* ... */`. Scripts register
`script("/command", { ... })`, `hotkey ... { ... }`, or
`event name { ... }` handlers. Top-level registrations do not run
their bodies until dispatched.

The debugger's snippet evaluator has a persistent **console-local**
variable scope. Use a single expression for its displayed return value,
or statements/declarations to modify console-local variables. It is
**not** a full JavaScript REPL: no JavaScript objects, DOM, breakpoints,
stepping, async execution, or direct reads of another VM's local stack
variables. Use the read-only `debug.*` helpers to inspect another script's
published `state.*` values.

### Functions and runtime memory

Define functions at the top level of a `.rrs` file, alongside command/event
registrations. Parameters are managed references to the caller's variables
by default, with no `ref` annotation needed. Arguments that are expressions
or literals become temporary values. `const` parameters prevent writes and
cannot be bypassed by passing a const variable to a mutable parameter. Calls
are limited to 64 nested frames.

```c
void increment(int value) {
    value++;
}

int square(const int value) {
    return value * value;
}

script("/counter", {
    int n = 4;
    increment(n);  // n is 5
    log.info(string.str(square(n)));
});
```

A ready-to-load [functions and references example](examples/scripts/functions.rrs)
demonstrates copy assignment, reference binding, const parameters, and
memory accounting. Load it with `/script load functions.rrs` and invoke
`/references`.

Top-level functions are available to all handlers in the same script VM.
Assignments with `=` copy values; `:=` binds an alias; writing `=` through
a reference updates its shared target. Function return values are copied,
not returned as references. Native host functions currently accept value
copies, so they do not mutate script arguments.

`runtime.memory()` returns a dictionary with `used`, `peak` (managed
bytes), `objects`, `allocations`, `releases`, `references`, and a
`gc` dictionary containing `collections`, `requests`, and `reclaimed`.
`runtime.gc.collect()` requests collection and returns zero with the
current acyclic reference-counted storage model. Currently `gc.collections`
is zero, `gc.requests` counts explicit requests, and `gc.reclaimed`
counts cells released when their last reference disappears. Values include
copied strings, arrays, and dictionaries; byte accounting does not include
host-native allocations. A tracing cycle collector is not implemented
because managed references cannot currently form reference cycles.

```c
dict memory = runtime.memory();
log.info("Managed bytes: " + string.str(memory.used));
log.info("Managed peak: " + string.str(memory.peak));
```

### Debugger inspection API

| Console function | Result |
|---|---|
| `debug.scripts()` | Newline-separated list of loaded script names |
| `debug.namespaces()` | Newline-separated shared namespaces |
| `debug.keys("script.rrs")` | Keys from that loaded script's current state namespace |
| `debug.state("script.rrs", "key")` | Its current state value |
| `debug.keys("@global")` | Keys directly in the shared `global` namespace |
| `debug.state("@station.audio", "gain")` | Value from a specifically named shared namespace |

Inspecting a script does **not** run its handlers. The console itself uses
an independent VM and can join namespaces with `state.use(name)`. Thus
`state.set` from the console intentionally changes shared state, while
`debug.state` is read-only. String lists are newline-separated, rather
than arrays (array support is not implemented yet).

## Callsign decode events

When a client-local modem decoder (FT8, Olivia, PSK, etc.) decodes a message
containing the operator's callsign or a configured watched callsign, RustyRig
publishes **`event watch.callsign`** to RustyScript. This source-neutral name also allows other future callsign monitors to publish the same event. For modem decodes, matching happens in the client core regardless of whether the GTK Terminal tab is visible.
The existing watched-calls database filters by mode and RF band, including
the `Any` wildcard. Callsigns are matched case-insensitively as complete
tokens, not substrings. The My callsign field in the Terminal toolbar is now
stored as `modem.my_callsign` in client settings and reused after restart.

```c
event watch.callsign {
    if (event.kind == "own") {
        log.info("My callsign heard: " + event.callsign + " " + event.text)
    } else {
        log.info("Watchlist hit: " + event.callsign + " " + event.text)
    }
}
```

Each matching watchlist entry produces a notification with `kind="watchlist"`;
matching the user's callsign produces `kind="own"`. A single decode may
produce both if the callsign is also on the watchlist.

| Event field | Description |
|---|---|
| `event.callsign` | Matched callsign, retaining the configured case |
| `event.kind` | `"own"` or `"watchlist"` |
| `event.text` | Decoded text, without FT8 JSON envelope |
| `event.raw` | Original worker decode record (including JSON when supplied) |
| `event.time_ms` | Client timestamp in Unix epoch milliseconds |
| `event.own` | Boolean, true for the operator callsign match |
| `event.origin` | Producing subsystem, currently `"modem"` |
| `event.modem` | Local modem identifier, e.g. `modem1` |
| `event.audio_source` | Exact receiver/audio stream feeding the decoder |
| `event.vfo_uuid` | Source VFO resource UUID, if known; empty otherwise |
| `event.rig` | Rig room name such as `dsmiia00-rig0`, if established by the RX stream |
| `event.model` | Rig model such as `FT-891`, resolved from its catalog owner where available |
| `event.server` | Connection/profile providing the source stream, if known |
| `event.engine` | Decoder engine (`ft8`, `olivia`, `psk`) |
| `event.mode` | Normalized mode used for watchlist matching (e.g. `FT8`) |
| `event.band` | RF amateur band name, or empty if unavailable |
| `event.frequency_hz` | RF dial frequency (integer Hz; 0 if unavailable) |
| `event.source` | Source audio stream identifier |
| `event.slot` | Local decoder slot, starting at 1 |
| `event.watch_id` | Watched-call database row ID; 0 for own-callsign matches |
| `event.audio_hz`, `event.dt`, `event.sync_score`, `event.snr` | Optional decoder-provided metrics, included when present; numeric worker values remain numeric, string worker values remain strings |

The [callsign-alert example](examples/scripts/callsign-alert.rrs) begins with
an easy configuration block: a `const char rooms[][]` array of `#room@network`
destinations (default `#hamradio-test@efnet`), a popup toggle, a room
announcement toggle, and a 30-minute per-callsign *absence* window (reset by every matching decode). It assembles a
single message like `Spotted K1ABC on 20m FT8 at 14.074 MHz, using
dsmiia00-rig0 (FT-891)`, logs it, optionally shows a dialog, and queues
it into all configured, joined IRC/RustyRig rooms. The last-seen timestamps live in shared in-memory state and reset on client restart. A callsign detected continuously will not repeat announcements until it disappears for the configured duration and reappears. A per-watch-entry
notification checkbox in the C watchlist editor is not implemented yet.

Only decoded text messages generate this notification; worker status JSON,
configuration messages, and unrelated lines are ignored. A rig identifier is not inferred when the audio source cannot reliably be mapped; scripts can use `event.vfo_uuid` and `event.source` for further lookup. The signal is **notification-only** and never keys a transmitter or modifies modem
routing. This event currently covers client-local decoder workers; it
does not imply that remote server-side serial modem output is subscribed
or analyzed on the client.

## IRC artwork archiver example

The example `doc/examples/scripts/ascii-art.rrs` demonstrates ordinary user
automation, with all capture, burst-grouping, filenames, and playback logic
implemented **in RustyScript**, not hardwired into the client.

Copy it to the scripts directory and load it:

```text
/script load ascii-art.rrs
/artwork
```

The collector watches transport-neutral `talk.msg` for live public messages from IRC and RRPROTO. An indexed mIRC color (`0x03` plus
digits), an extended RGB color (`0x04` plus six hexadecimal digits), or
Unicode block graphics can start a capture. Subsequent lines from the same
nick/channel join the current burst. A resettable `timer.after` saves one
completed artwork after **3.5 seconds of silence**, provided at least two
lines were collected. Each burst has an 80-line cap. The original formatting
bytes are retained as `chan-nick-yyyymmdd-hhmmss.mac` under
`$XDG_DATA_HOME/rustyrig/artwork/`. A small filename suffix is used when
necessary to avoid overwriting an earlier capture.

The example also declares a floating manager via `ui.utility`. It uses the
generic `filelist` widget, a read-only `irc-preview` backed by RustyRig's
existing IRC color renderer, and a `roomlist` of open conversations.
Send and Stop are implemented through `timer.every`, `string.line`,
`chat.send` and `chat.cancel`. Playback has separate **sustained** (X lines per Y seconds) and
**burst** (X lines per Y seconds for at most Z lines) controls, remembered
per profile/target using `setting.get/set`. Burst maximum 0 disables the
faster initial phase. The send timer schedules the artwork line by line;
the client also enforces its shared chat token bucket and bounded queue
on script-tagged sends, so an aggressive macro configuration cannot bypass
the client's `chat.send.burst` and `chat.send.interval-ms` limits.
IRC networks may impose stricter limits. Buttons demonstrate `ui.edit`,
file rename/move/delete, `file.mkdir`, and confirmation dialogs.

These primitives are reusable for other station automations:

- `timer.after(id, ms)`, `timer.every(id, ms)`, `timer.cancel(id)`
  deliver `event ui.timer` with `event.id`; timers stop on script unload
- `state.get(key)` and `state.set(key, value)` access **shared**
  state by default. `state.use(namespace)` selects a named shared table;
  `state.use("")` switches back to private state. `state.namespace()`,
  `state.namespaces()` and `state.keys()` inspect the available scopes
  and keys; setting a value to `null` deletes it
- `string.regex`, `string.chr`, `string.slug`, `string.hash`,
  `string.slice`, `string.length`, `string.line`, `string.lines`,
  and `string.str` provide basic text operations
- `time.now()`, `time.stamp()`, and `env.data_dir()`
  provide time and the per-user data location
- Client `setting.get/set` persists simple values in `client.db`;
  `chat.send(profile|target, text, tag[, interval_ms])` queues a message
  for an existing chat, and `chat.cancel(tag)` drops unsent tagged lines

No C API named `art.capture` or `art.play` is needed. The example is
intentionally readable and modifiable by people who aren't C developers.

## Shared namespaces and naming

Each loaded script has its **own interpreter VM, handlers, timer IDs and local
variables**. Scripts share a `global` **state namespace by default**, so two
scripts can communicate by reading and writing the same `state.*` keys.
Namespaces belong to the client or server script host; they are not network
synchronized or persisted across application restarts. Use `setting.get/set`
for client settings that must survive a restart.

```text
// Script A — shared global state
state.set("station.audio.gain", 12)

// Script B — reads the same value
state.get("station.audio.gain")

// Use a named shared namespace for a particular station subsystem
state.use("station.audio")
state.set("gain", 12)

// Another script joining the same namespace sees it
state.use("station.audio")
state.get("gain")

// For scratch data that must not be shared
state.use("")
state.set("temporary.value", 42)
```

**Naming convention:** namespaces and state keys use lowercase
dot-separated components, for example `station.audio.gain` or
`artwork.capture-1234.body`. Each component begins with `a`–`z`
and may contain lowercase letters, digits, underscores and hyphens.
Empty components, spaces, uppercase letters and trailing dots are rejected.
Namespace names are limited to 64 bytes; state keys to 256 bytes.
The reserved default scope is `global`.

## Script Debugger

Use `/script debug` to open the floating **Script Console**, modeled on a
browser's JavaScript console. It records script load/runtime errors and
`log.info`/`log.warn` messages. Entries include timestamp, severity,
script filename, handler name, line/column, and diagnostic text. Repeated
identical messages are grouped by an occurrence count; the console retains
up to 1,000 entries, even while its panel is closed.

Filter by all messages, warnings/errors, or errors only. Select a row to
**Copy** the diagnostic or **Open Script** at its source location; double-click
a row to open it directly. **Clear** empties the current console history.
The ordinary client log remains available independently.

The console also includes a **multiline RustyScript input**. Paste expressions
or snippets and click **Evaluate** (or Ctrl+Enter). Console variables persist
between evaluations and do not enter another loaded script's interpreter scope.
Examples: `debug.scripts()` lists loaded scripts;
`debug.namespaces()` lists shared state scopes;
`debug.keys("@global")` lists shared keys; and
`debug.state("@global", "station.audio.gain")` prints a value.
Use `debug.keys("autoload/ascii-art.rrs")` and
`debug.state("autoload/ascii-art.rrs", "artwork.play.rate")` to inspect
the namespace used by a particular loaded script. Values, including
nested dictionaries, are formatted for inspection.

New runtime diagnostics include positions like `line 17:24: unknown variable`.
Because the interpreter lives in **librustyaxe**, rebuild/install that library
before rebuilding the client to get the new line/column reporting.

## Scriptable GTK panels and docking

The initial window/panel API is available in **rrclient with GTK**. It
operates on RustyRig's `RRFloatPanel` windows (including the same instances
used when dragging windows into the chat's four docks). It does not yet
move arbitrary operating-system toplevel windows, GTK notebook pages, or
separate native dialogs. No GTK widget pointers are exposed to scripts.

Both `window` and `panel` are aliases for this first API:

| Function | Operation |
|---|---|
| `window.find(pattern)` | Find the first title matching a case-insensitive GLib regular expression; exact panel ID also accepted |
| `window.find(pattern, location)` | Restrict discovery to one location |
| `window.move(id, location[, x, y])` | Move an existing panel to a dock or the main overlay, with optional coordinates |
| `window.zoom(id, percent)` | Set local panel zoom, 10–200 percent |
| `window.opacity(id, percent)` | Set panel opacity, 30–100 percent |

`panel.find`, `panel.move`, `panel.zoom`, and `panel.opacity` have
identical behavior.

Locations are **N** (north/top), **E** (east/right), **W** (west/left),
**S** (south/bottom), **M** (main floating workspace); `*` means any
location **for discovery only**. When supplied, coordinates are nonnegative integers in pixels relative to
the destination dock's slot, or the main overlay for `M` (range 0–8192).
If omitted, the panel reuses its previously saved position for that
destination type (`window.move(w.id, "E")`).

`find` returns either `null` or a **dictionary** containing `id`,
`title`, and `location`. Store the returned stable ID instead of
relying on a mutable visible title. Search returns the first match; a
`find_all` or typed `window[]` API will follow after arrays and handles
are implemented by the language runtime.

Example — move the first RF spectrum panel into the east dock:

```c
script("/spectrum-east", {
    dict w = window.find("RF Spectrum|Waterfall", "*")
    if (w != null) {
        window.move(w.id, "E", 20, 40)
        window.zoom(w.id, 125)
        window.opacity(w.id, 85)
    }
});
```

Example — return an existing panel to the main area:

```c
script("/spectrum-main", {
    dict w = window.find("RF Spectrum|Waterfall")
    if (w != null) {
        window.move(w.id, "M", 50, 40)
    }
});
```

Example — find a panel currently in the west dock by a substring:

```c
script("/west-east", {
    dict w = panel.find("VFO", "W")
    if (w != null) {
        panel.move(w.id, "E", 10, 10)
    }
});
```

Local RustyScript slash commands can run while the client is offline; normal built-in command precedence and restrictions still apply.

A ready-to-load demonstration is included at `doc/examples/scripts/panel-layout.rrs`.
Copy it to your client scripts root or `autoload/` and invoke
`/spectrum-east`, `/spectrum-main`, or `/vfo-west` after loading it.

**Behavior:** Movement reparents the existing GTK panel chrome and contents
through the docking manager, not by destroying and recreating it. It
preserves the panel's content, zoom, opacity and saved identity. Dock and
floating locations are saved through the normal client layout storage.
The zoom and opacity functions adjust the existing UI sliders to keep
the settings popover synchronized.

**Current limits:** Docking requires an active chat workspace whose docks
exist; inactive-room panels are not moved into the active room. Nonexistent
IDs, unavailable docks, invalid percentages and coordinates raise script
errors. Non-GTK client frontends and rrserver have no panel API. For now,
window functions must run on the GTK main thread (as they do for UI
slash-command and hotkey handlers). Calling them from an event delivered on
another thread raises an error rather than accessing GTK unsafely. A future
asynchronous UI-dispatch bridge can lift this restriction. Arbitrary OS
windows, modal dialogs, cross-room docking, multi-match selection and
moving entire groups are not yet supported.

## External program execution policy

Both client and server default to:

```ini
security.scripting.allow-unsafe-exec=false
```

Once a subprocess API is implemented, this setting must be checked for
**every script-initiated executable spawn**, including any future `exec`,
`process.spawn` or `subproc` wrapper. There are currently no such script
functions to invoke or bypass the policy. Ordinary RustyRig-managed workers
for audio, modems, or CAT are not script-initiated arbitrary binary execution.

## Testing

Within the librustyaxe repository:

```sh
make -C rustyscript test
make test
```

The standalone tests cover syntax, typed value copying, registrations,
handler dispatch, console variable persistence/value formatting, and
preservation of previous registrations after a failed load. They do not
exercise the real GTK frontend, network, or server authorization.

In the RustyRig checkout, run `make test` to include the example script
smoke test (when `librustyscript` is installed). After changing
`librustyaxe`, rebuild/install it before rebuilding the client.
**Documentation review is not a substitute for successful local builds
and functional tests.**

## SDR and HTTP automation

Client-only `sdr.connect_favorite(frequency_hz, mode)` chooses an accessible favorite
and requests a new temporary receiver with initial RX tuning. Returns the selected
URL or null. This is acceptance, not a guarantee of a successful tune. Requires
an authenticated RustyRig connection and matching updated server.

`net.http.fetch(url)` and `net.http.post(url, body[, content_type])` return
opaque request IDs immediately. POST defaults to application/json. Both client
and server scripts can call `net.http.result(id)` to collect a completed dictionary
with status (HTTP status or zero), body (UTF-8 text), and error (null on successful
transport). Pending requests return null; a result can be collected only once.
HTTP errors such as 404 retain their status and body. Poll from an existing timer
rather than busy-waiting. There are four outstanding requests per script, 1 MiB
request/response limits, 5-second connect and 15-second total timeouts. TLS
verification is mandatory; redirects are not followed automatically. Unloading
a script cancels and joins its outstanding workers. WebSockets are not yet exposed.

### SDR manager example

Install `doc/examples/scripts/sdr.rrs` under the script root, then
`/script load sdr.rrs`. The script implements `/sdr`, `/sdr open Hz MODE`,
`/sdr Hz MODE`, `/sdr favorites`, `/sdr list`, and `/sdr close URL-or-room`.
The list and close operations are scoped to the selected RustyRig server.
`chat.print(text)` writes locally to the active chat, never to the server.
