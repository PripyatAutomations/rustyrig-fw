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

There is **no** interactive `/script load`, `/script unload`, or
`/script reload` command yet. After startup, handlers run when their
registered slash command, hotkey or event fires. They do not execute at
load time. In particular, an event handler for `irc.privmsg` runs only
when a matching event arrives.

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
subscriptions; their slash command bindings cannot yet be invoked by
remote users. The server does not expose a script upload or management
endpoint.

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
