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

The compiled-in CSS is the default. Source installs and Debian packages also
ship **`/etc/rustyrig/rrclient.css` as a reference document only**: the client
does not read it. For a fresh copy from source, run:

```sh
perl tools/releng-dump-css.pl --css > rrclient.css
```

Copy and customize specific rules in a `[gtk-css]` section of your own
`rrclient.cfg`, then run `/css-reload`. That section overrides the compiled
CSS. The example config intentionally contains no active CSS or font overrides.
The script without `--css` emits the same rules in paste-ready INI format,
including escaped `#` selectors:

```sh
perl tools/releng-dump-css.pl > gtk-css.cfg
```

Avoid generic `label` or `button` overrides unless the same typography is
intended throughout the UI. If fonts are bundled later, verify their open
redistribution license and include its required notices.
