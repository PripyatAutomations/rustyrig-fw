# RustyScript script-defined user interfaces

The RustyScript UI API describes **logical controls**, not GTK widgets.
The core and VM use owned, toolkit-independent value messages from
`inc/rrclient/script.gui.h`. Each frontend renders those controls using
its own native toolkit. GTK is the first implementation; a TUI, another
native desktop toolkit, or a third-party frontend can implement the same
model without requiring different .rrs scripts.

A frontend advertises the optional `rr_frontend_ops_t.script_gui_available`
capability. Frontends without it reject UI creation with an explicit
capability error. They can still run non-UI RustyScript handlers.

The historical `gui.*` names are supported as aliases for `ui.*` where
applicable, but `ui.*` is recommended so scripts read naturally under GTK,
TUI, or another native frontend. Event aliases `gui.click`, `gui.change`,
and `gui.response` remain available; prefer `ui.click`, `ui.change`,
and `ui.response` for new scripts.

## Script API

```c
script("/meter", {
    ui.panel("meter", "Signal meter")
    ui.add("meter", "column", "content", "")
    ui.add("content", "label", "status", "Monitor")
    ui.add("content", "slider", "gain", "Gain", "35")
    ui.add("content", "button", "apply", "Apply")
})

event ui.change {
    if (event.id == "gain") {
        log.info("Gain changed")
    }
}

event ui.click {
    if (event.id == "apply") {
        log.info("Apply clicked")
    }
}
```

### Message and confirmation dialogs

The convenience call `ui.message(id, title, message[, buttons])` creates
a standard modal message dialog, without requiring scripts to pack buttons
or labels. `ui.dialog(id, title, message[, buttons])` is an equivalent
form; the existing two-argument `ui.dialog(id, title)` still creates an
empty customizable dialog.

The optional `buttons` argument is `"ok"` (default) or `"yesno"`
(`"yes/no"` is accepted too). The call returns the dialog ID immediately;
it **does not block** the RustyScript VM or return the user's answer.

```c
script("/ask", {
    ui.dialog("confirm", "Transmit?", "Enable transmitter?", "yesno")
})

event ui.response {
    if (event.id == "confirm") {
        if (event.answer == "yes") {
            log.info("User agreed")
        } else {
            log.info("User declined or dismissed the dialog")
        }
    }
}
```

The `ui.response` event includes `id`, `kind`, `answer` (`"ok"`,
`"yes"`, `"no"`, `"cancel"`, or `"close"`), and boolean
`accepted` (true for OK/Yes). It also retains the original numeric
`response` field for compatibility with existing custom dialogs.
Closing a dialog without choosing a button yields `"cancel"`, not
`"no"`. Reusing an ID replaces the previous message dialog; use
`ui.destroy(id)` to discard it explicitly.

A TUI or third-party frontend should implement the same logical message
request and response names using its own native dialog presentation.

`ui.panel(id, title)` creates a dockable floating panel;
`ui.utility(id, title)` creates an overlay-only floating utility panel
that stays above the workspace; `ui.dialog(id, title)` creates a dialog.
`ui.add(parent, type, id, label[, initial_value])` appends a logical
widget to a container. Supported GTK3 types:

- Containers: `row`, `column`, `frame` (captioned group), `separator`.
- Display: `label`, `readonly` (scrollable multiline text),
  `progress` (0–100 percentage), `irc-preview` (colorized .mac artwork).
- Input: `button`, `entry`, `password`, `multiline`, `check`
  (checkbox), `switch`, `slider`, `spin`, `knob`, `combo`, `color`,
  `file` (file chooser), `folder` (directory chooser).
- File/room selectors: `filelist` uses RustyRig's reusable local file browser;
  its initial value is the root directory, optionally followed by `|suffix`
  (e.g. `"/home/user/macros|.mac"`), and the label is the list title.
  Without a suffix, all regular files are listed.
  `roomlist` enumerates open conversation tabs, including private queries,
  and returns the `profile|room` identity rather than a display caption.

Combo options are comma-separated. Slider, spin and knob default to the historical
0–100 range; optionally pass `"min:max:step:value"` to give them explicit
bounds (for example `"1:10:1:1"` for lines/sec). `ui.set(id, value)`
sets numeric, boolean and text controls. For `filelist`, setting a value
refreshes the list; for `irc-preview`, set a local file path (the content is rendered with IRC indexed and RGB colors);
for `roomlist`, set `"refresh"` to rescan open tabs.

**Live property bindings.** `ui.bind(id, property)` binds a script-created
numeric knob, slider or spin control to a whitelisted client property.
Currently the supported property is `"ui.patchbay.wire-sag"` (0–240).
Moving the knob persists the new value and immediately redraws the
Routing and logic patchbay wires; opening the panel reads the current
value, including edits made in Routing. Unapproved property names fail
with an explicit runtime error. Example: `doc/examples/scripts/wire-sag.rrs`.
The script can create this panel in `event script.init` and expose a
`script("/wires", { ui.show("wires") })` command to reopen it.

**Identifying widget events.** Each `ui.change`, `ui.click`,
`ui.response` and `ui.close` contains `event.id` (the script-local
widget or dialog identifier) and `event.kind`. A script should check
`event.id` before responding, e.g. `if (event.id == "confirm")`.
The `owner` token scopes delivery to the script VM: another script's
widget events are not delivered to your handlers even if IDs collide.
`ui.response` also carries `event.answer` and `event.accepted`.

`ui.show(id)`, `ui.hide(id)`, and `ui.destroy(id)` change visibility or
ownership. Closing a script-created floating panel hides it and emits
`ui.close`; the script may reopen the same panel with `ui.show(id)` from a
slash command or other handler. `ui.destroy(id)` and unload/reload still
remove the widgets completely. Script-created widgets are removed automatically on unload/reload.
The client example `doc/examples/scripts/ascii-art.rrs` creates a working
script-owned .mac browser, previewer and paced sender. `ui.edit(path)` opens
any local regular file in RustyRig’s reusable editor.

`ui.val(id)` (also `control.val(id)`) returns the last known logical value: boolean for
checkboxes, numeric for slider/spin, string for other value controls.
`ui.set(id, value)` (also `control.set(id, value)`) changes the value. Both use script-local IDs; a
script cannot access another script's controls. The shorthand
`control.val()` on a first-class control object can be introduced once
the language implements user-facing handle values.

Creating widgets and setting values is asynchronous: the core sends an
owned message to the active frontend rather than directly manipulating
native widgets. `ui.val(id)` is an immediate cache read, not a
blocking cross-thread toolkit query. GUI widgets are cleaned up when the
owning script unloads or reloads.

## Events (same contract for all frontends)

A script binds `event ui.click`, `event ui.change`,
`event ui.response`, or `event ui.close`, filtering by `event.id`.
The `ui.close` event is emitted when the user closes a script-created
floating panel, allowing scripts to cancel timers, playback and other work.
Event payloads include widget `id` and `kind`; the host isolates these
events to the owning script.

`ui.click` includes:
- `button`: 1=left, 2=middle, 3=right; **0** for keyboard or semantic
  activation, important for text interfaces and accessibility
- `count`: click cadence 1/2/3
- `interval_ms`: interval since last relevant click
- `time_ms`: backend-local event timestamp
- `shift`, `ctrl`, `alt`, `super`: modifier key state

`ui.change` includes the updated `value`.
`ui.response` includes a numeric dialog `response`.
Mouse-only fields have portable neutral defaults when invoked by keyboard.

## Frontend implementers

Do **not** expose native widget pointers or toolkit object names through
the scripting ABI. Messages contain `RR_SCRIPT_GUI_PROTOCOL_VERSION`; reject unknown versions.
The renderer subscribes to
`RR_SCRIPT_GUI_COMMAND`, using its own event domain, maintains the
owner-scoped container tree and creates native controls. It emits
`RR_SCRIPT_GUI_CHANGE` for value-cache updates and the logical
`ui.click`, `ui.change`, `ui.response` events for scripts.
The core delivers handlers on its scripting domain.

The renderer handles layout, resizing, focus, accessibility, input
devices, and native look independently. A TUI can display dialogs as
terminal overlays and rows/columns as character-cell layouts. A new
native GUI could implement the same command/event interface and
opt into `script_gui_available`; existing scripts remain portable.

Keep widget IDs stable and owner-scoped, avoid synchronous cross-thread
calls, do not expose raw frontend pointers through messages, and dispose
all widgets on the `RR_GUI_RELEASE` operation. Unrecognized widget
types or unsupported capabilities should fail safely.

This initial implementation supports common controls and events; a
versioned capability negotiation mechanism should be added before
expanding to advanced widgets and independent external modules.
