# TUI top status line

`tui.status-line` controls the first terminal row, where the window topic or
“status window” appears. It does not change the bottom status bar.

The default is:

```
tui.status-line=${topic} | VFO ${active_vfo}: ${active_freq_khz:---} kHz ${active_mode:---}
```

For example, show both VFOs and highlight the active one:

```
tui.status-line={bright-cyan}VFO ${active_vfo}{reset} A:${vfo_a_freq_khz:---} B:${vfo_b_freq_khz:---} kHz ${active_mode:---} ${active_ptt:RX}
```

Values are read from current client state whenever the TUI redraws. Updates
to inactive VFOs also trigger a redraw. After editing the configuration, use
`/reload`. An empty template leaves the top row blank; `${topic}` alone keeps
the original window topic. Long lines are clipped to the terminal width.

| Variable | Value |
|---|---|
| `active_vfo` | Active VFO letter |
| `active_freq`, `active_freq_hz` | Active frequency in integer Hz |
| `active_freq_khz` | Frequency in kHz with three decimal places |
| `active_freq_mhz` | Frequency in MHz with six decimal places |
| `active_mode` | Mode name |
| `active_width` | Filter width in Hz |
| `active_power` | Radio's reported power value |
| `active_ptt` | `TX` or `RX` |
| `vfo_a_freq`, `vfo_b_mode`, etc. | Same fields for a specific VFO; letters A–Z, either case |
| `window`, `win.title` | Current window title |
| `topic` | Current window topic/status text |
| `win.scroll` | Current window scroll offset |
| `server`, `user` | Server name and login user |
| `connection` | `ONLINE`, `CONNECTING` or `OFFLINE` |
| `rxcodec`, `txcodec` | Current local codec ID, or `NONE` |

Use `${name:fallback}` to display a fallback when a value is unavailable.
Unknown variables without a fallback expand to an empty string. Colors use
the existing `{red}`, `{bright-cyan}`, `{reset}`, etc. syntax and respect
`tui.use-color`. Percent signs are literal. This template is TUI-specific;
GTK and the browser continue using their own displays of the same radio state.
