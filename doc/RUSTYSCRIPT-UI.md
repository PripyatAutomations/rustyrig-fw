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

`ui.panel(id, title)` creates a nonmodal floating panel; `ui.dialog(id,
title)` creates a dialog. `ui.add(parent, type, id, label[, initial_value])`
appends a widget to a logical container. Types: `row`, `column`,
`label`, `button`, `entry`, `check`, `slider`, `spin`, `combo`, and
`separator`. For now, slider and spin controls have a 0–100 range; combo
options are comma-separated. `ui.show(id)`, `ui.hide(id)`, and
`ui.destroy(id)` change visibility or ownership.

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

A script binds `event ui.click`, `event ui.change`, or
`event ui.response`, filtering by `event.id`. Event payloads include
widget `id` and `kind`. The host isolates these events to the
owning script.

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
