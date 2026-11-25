# RustyRig UI layouts

RustyRig loads each named UI layout independently, using the first usable
script in this order:

1. User: `$XDG_CONFIG_HOME/rustyrig/scripts/client/ui/`
   (defaults to `~/.config/rustyrig/scripts/client/ui/`).
2. Administrator: `/etc/rustyrig/scripts/client/ui/`.
3. Distribution: `/usr/share/rustyrig/scripts/client/ui/`.

The distribution directory is package-owned and may be replaced on upgrade.
Copy only the files you want to customize into the user or administrator
directory, retaining the same filename. Do not edit the packaged copies.
The directories are not merged within a file: an override replaces that
entire layout. Missing, unreadable, malformed or incompatible overrides are
logged and the next location is tried. The client log subsystem
`ui.layout` identifies the actual loaded path.

## Available layouts

- `server-picker.rrs`: Alt+X or /server; profile list and actions.
- `server-editor.rrs`: new/edit profile form.
- `userlist.rrs`: upper right room roster tile.
- `vfo.rrs`: lower right radio controls tile.

The primary Main pane and the two-to-one roster/VFO split are currently
native workspace geometry. Layout scripts describe the controls inside each
tile; arbitrary split trees are not yet script-configurable.

Example (personal override):

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/rustyrig/scripts/client/ui"
cp /usr/share/rustyrig/scripts/client/ui/vfo.rrs \
   "${XDG_CONFIG_HOME:-$HOME/.config}/rustyrig/scripts/client/ui/"
```

Edit the copy. Preserve native binding IDs, widget kinds and action names.
Password entries must remain masked. Restart rrclient to reload the workspace;
picker/editor layouts are reread when opened. Remove the override to restore
the next lower-priority copy.

These are restricted layout scripts, not ordinary user/autoload RustyScripts.
They run in a separate layout VM with UI creation and capability functions;
normal script-manager/autoload rules are separate. GTK does not consume these
layouts yet.

## Themes

`default.theme` is a shared semantic RGB palette in GLib keyfile format.
Copy it to the same user or administrator override directory as a UI layout.
Set `[theme]` properties to `#RRGGBB` values. Missing properties use built-in
colors; invalid recognized values reject that file and try the next location.
Restart the TUI to apply changes. The `ui.theme` log identifies the loaded
file. Terminal rendering selects truecolor, 256-color or 16-color output;
explicit IRC formatting remains separate. GTK3 does not use this palette yet.

## IRC tools and workspace sizing

The Tools tile replaces VFO on IRC connections. Override `tools.rrs` to add
buttons with `action="/command arguments"`; commands use the normal client
parser and permissions. Load any addon defining custom commands separately.
Radio and IRC divider width and tile weights are saved independently in
client.db under `ui.tui.radio.*` and `ui.tui.irc.*`. The active titlebar
background is `panel.header.active` in the theme.
