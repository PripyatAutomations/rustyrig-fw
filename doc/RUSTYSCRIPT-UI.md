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

## Script API

```c
script("/meter", {
    gui.panel("meter", "Signal meter")
    gui.add("meter", "column", "content", "")
    gui.add("content", "label", "status", "Monitor")
    gui.add("content", "slider", "gain", "Gain", "35")
    gui.add("content", "button", "apply", "Apply")
})

event gui.change {
    if (event.id == "gain") {
        log.info("Gain changed")
    }
}

event gui.click {
    if (event.id == "apply") {
        log.info("Apply clicked")
    }
}
```

`gui.panel(id, title)` creates a nonmodal floating panel; `gui.dialog(id,
title)` creates a dialog. `gui.add(parent, type, id, label[, initial_value])`
appends a widget to a logical container. Types: `row`, `column`,
`label`, `button`, `entry`, `check`, `slider`, `spin`, `combo`, and
`separator`. For now, slider and spin controls have a 0–100 range; combo
options are comma-separated. `gui.show(id)`, `gui.hide(id)`, and
`gui.destroy(id)` change visibility or ownership.

`control.val(id)` returns the last known logical value: boolean for
checkboxes, numeric for slider/spin, string for other value controls.
`control.set(id, value)` changes the value. Both use script-local IDs; a
script cannot access another script's controls. The shorthand
`control.val()` on a first-class control object can be introduced once
the language implements user-facing handle values.

Creating widgets and setting values is asynchronous: the core sends an
owned message to the active frontend rather than directly manipulating
native widgets. `control.val(id)` is an immediate cache read, not a
blocking cross-thread toolkit query. GUI widgets are cleaned up when the
owning script unloads or reloads.

## Events (same contract for all frontends)

A script binds `event gui.click`, `event gui.change`, or
`event gui.response`, filtering by `event.id`. Event payloads include
widget `id` and `kind`. The host isolates these events to the
owning script.

`gui.click` includes:
- `button`: 1=left, 2=middle, 3=right; **0** for keyboard or semantic
  activation, important for text interfaces and accessibility
- `count`: click cadence 1/2/3
- `interval_ms`: interval since last relevant click
- `time_ms`: backend-local event timestamp
- `shift`, `ctrl`, `alt`, `super`: modifier key state

`gui.change` includes the updated `value`.
`gui.response` includes a numeric dialog `response`.
Mouse-only fields have portable neutral defaults when invoked by keyboard.

## Frontend implementers

Do **not** expose native widget pointers or toolkit object names through
the scripting ABI. The renderer subscribes to
`RR_SCRIPT_GUI_COMMAND`, using its own event domain, maintains the
owner-scoped container tree and creates native controls. It emits
`RR_SCRIPT_GUI_CHANGE` for value-cache updates and the logical
`gui.click`, `gui.change`, `gui.response` events for scripts.
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
