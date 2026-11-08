# GTK typography

RustyRig uses system-installed font families rather than bundling font binaries.
The default stacks are selected in `rrclient/defconfig.c`:

| Platform | Interface | Monospaced data |
| --- | --- | --- |
| Linux | Cantarell → Noto Sans → DejaVu Sans → Sans | DejaVu Sans Mono → Liberation Mono → Monospace |
| Windows | Segoe UI → Arial → Sans | Consolas → DejaVu Sans Mono → Monospace |
| macOS | Helvetica Neue → Arial → Sans | Menlo → DejaVu Sans Mono → Monospace |

These are font *names*, not files distributed with the application. Pango
resolves missing fonts using the installed system's fallback machinery.

Typography roles:

- Buttons, editable inputs and selectors: interface sans, 10.5pt.
- Tables, resource explorers, rosters: interface sans, 10pt.
- Notebook tab labels: interface sans, semibold.
- Chat transcript: mono, 11pt.
- Host/client logs: mono, 10pt.
- Frequency controls: mono, 12pt; compact VFO readout: bold mono, 15pt.
- Generic labels and most floating panel titles inherit the desktop GTK theme.

The `[gtk-css]` section in `rrclient.cfg` overrides these defaults,
including on-the-fly via `/css-reload`. Example:

```ini
[gtk-css]
\#chat-view, \#chat-view text { font-family: "Liberation Mono", "Monospace"; font-size: 12pt; }
treeview { font-size: 11pt; }
```

GTK's user CSS is applied above default rules. Avoid generic `label` and
`button` overrides unless you intentionally want the same typography on
every widget. The font families above can be used without redistributing
proprietary font files. If fonts are bundled later, prefer a verified
SIL Open Font License 1.1 family with its license text and attribution.
