# RustyScript host integration (initial runtime)

RustyScript lives in the **PripyatAutomations/librustyaxe** repository as
a separately installed `librustyscript` shared library. `rrclient` and
`rrserver` load it when `pkg-config librustyscript` is available at build
time. Without the development package, they build without script support.

Install the current librustyaxe source tree with `make install`, or build
the `librustyscript0` and `librustyscript-dev` Debian packages first.

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

`helper.rrs` is retained but is not loaded. The host does not create any
directory automatically. Create the directory yourself, then restart
`rrclient` or `rrserver` to load newly added scripts.

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

Scripts are trusted code running in the host process.

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
typing, persistent variables, SQLite script databases, `async`/`await`,
`try`/`except`/`success`/`finally`, built-in `exec()` or `subproc`,
the routing/pipeline/control APIs, workspace restoration, runtime script
reload, and server invocation of slash-command bindings. These are part of
the planned language and require additional parser/runtime/host work. Do not
assume unsupported code was validated at load time; errors may be deferred
until invocation.

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
| `window.move(id, location, x, y)` | Move an existing panel to a dock or the main overlay |
| `window.zoom(id, percent)` | Set local panel zoom, 10–200 percent |
| `window.opacity(id, percent)` | Set panel opacity, 30–100 percent |

`panel.find`, `panel.move`, `panel.zoom`, and `panel.opacity` have
identical behavior.

Locations are **N** (north/top), **E** (east/right), **W** (west/left),
**S** (south/bottom), **M** (main floating workspace); `*` means any
location **for discovery only**. All coordinates are nonnegative integers
in pixels relative to the destination dock's slot, or relative to the main
overlay for `M`. Coordinates must be supplied explicitly, currently
between 0 and 8192.

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
