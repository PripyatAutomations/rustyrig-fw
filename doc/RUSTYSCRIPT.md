# RustyScript host integration (initial runtime)

RustyScript lives in the **PripyatAutomations/librustyaxe** repository as
a separately installed `librustyscript` shared library. `rrclient` and
`rrserver` load it when `pkg-config librustyscript` is available at build
time. Without the development package, they build without script support.

Install the current librustyaxe source tree with `make install`, or build
the `librustyscript0` and `librustyscript-dev` Debian packages first.

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

After startup, handlers run when their registered slash command, hotkey,
or event fires. They do not execute at load time. In particular, an event
handler for `irc.privmsg` runs only when a matching event arrives.

The client provides these local commands, even when disconnected:

```text
/script list
/script load helper.rrs
/script unload helper.rrs
/script reload helper.rrs
/script reload autoload/10-hello.rrs
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

**Not implemented yet:** user function declarations, arrays, strict return
typing, automatically persisted script variables (distinct from explicit client settings), `async`/`await`,
`try`/`except`/`success`/`finally`, built-in `exec()` or `subproc`,
the routing/pipeline/control APIs, workspace restoration, and server invocation of slash-command bindings. These are part of
the planned language and require additional parser/runtime/host work. Do not
assume unsupported code was validated at load time; errors may be deferred
until invocation.

## IRC artwork archiver example

The example `doc/examples/scripts/ascii-art.rrs` demonstrates ordinary user
automation, with all capture, burst-grouping, filenames, and playback logic
implemented **in RustyScript**, not hardwired into the client.

Copy it to the scripts directory and load it:

```text
/script load ascii-art.rrs
/artwork
```

The collector watches `irc.privmsg`. An indexed mIRC color (`0x03` plus
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
`chat.send` and `chat.cancel`. Playback speed (1–10 lines/sec) is
stored separately per profile/target using `setting.get/set`. Buttons
demonstrate `ui.edit`, file rename/move/delete, `file.mkdir`, and
confirmation dialogs. The client still enforces its bounded chat queue.

These primitives are reusable for other station automations:

- `timer.after(id, ms)`, `timer.every(id, ms)`, `timer.cancel(id)`
  deliver `event ui.timer` with `event.id`; timers stop on script unload
- `state.get(key)` and `state.set(key, value)` retain script values
  across event invocations; setting a value to `null` removes it
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
handler dispatch, and preservation of previous registrations after a failed
load. They do not yet exercise real GTK, networking, or server authorization.
