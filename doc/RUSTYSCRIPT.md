# RustyScript host integration (initial runtime)

RustyScript lives in the **PripyatAutomations/librustyaxe** repository as
a separately installed \`librustyscript\` shared library. \`rrclient\` and
\`rrserver\` load it when \`pkg-config librustyscript\` is available at build
time. Without the development package, they build without script support.

Install the current librustyaxe source tree with \`make install\`, or build
the \`librustyscript0\` and \`librustyscript-dev\` Debian packages first.

## Script locations

The config key \`security.scripting.path\` selects the directory containing
\`*.rrs\` files. Script filenames are enumerated in alphabetical order.

- Client default: \`$XDG_CONFIG_HOME/rustyrig/scripts\` (or equivalent user
  configuration directory)
- Server default: \`/etc/rustyrig/scripts\` (admin-installed files)

The directories are not created automatically. The server does not expose a
remote script-upload or script-management endpoint; installation is a local
administrator action. Scripts are trusted code running in the host process.

## Initial registration syntax

\`\`\`c
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
\`\`\`

\`_\` represents Shift, \`&\` Alt, and \`^\` Control. Braces are required.
The client dispatches script commands only when the name does not shadow an
existing built-in command; existing built-ins take precedence.
GTK dispatches custom script hotkeys after its priority shortcut handling.
The server has no hotkey dispatcher; server scripts can register event handlers.

Event data is passed as a typed \`event\` dictionary. IRC PRIVMSG handlers get
\`event.text\`, \`event.nick\`, and \`event.room\`/ \`event.target\` aliases
for the corresponding \`talk.*\` fields. The native \`talk.msg\` event remains
available on both sides.

Loading a script only registers handlers; it does not run them.
The initial interpreter supports scalar/dictionary values, local declarations,
arithmetic, \`if\`/\`else\` with mandatory braces, equality comparisons and
registered native calls. \`log.info()\` and \`log.warn()\` are initially exposed.

**Not implemented yet:** user function declarations, arrays, strict return
typing, persistent variables, SQLite script databases, \`async\`/\`await\`,
\`try\`/\`except\`/\`success\`/\`finally\`, built-in \`exec()\` or \`subproc\`,
the routing/pipeline/control APIs, workspace restoration, runtime script
reload, and server invocation of slash-command bindings. These are part of
the planned language and require additional parser/runtime/host work. Do not
assume unsupported code was validated at load time; errors may be deferred
until invocation.

## External program execution policy

Both client and server default to:

\`\`\`ini
security.scripting.allow-unsafe-exec=false
\`\`\`

Once a subprocess API is implemented, this setting must be checked for
**every script-initiated executable spawn**, including any future \`exec\`,
\`process.spawn\` or \`subproc\` wrapper. There are currently no such script
functions to invoke or bypass the policy. Ordinary RustyRig-managed workers
for audio, modems, or CAT are not script-initiated arbitrary binary execution.

## Testing

Within the librustyaxe repository:

\`\`\`sh
make -C rustyscript test
make test
\`\`\`

The standalone tests cover syntax, typed value copying, registrations,
handler dispatch, and preservation of previous registrations after a failed
load. They do not yet exercise real GTK, networking, or server authorization.
