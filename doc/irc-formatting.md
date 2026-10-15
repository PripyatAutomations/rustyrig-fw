# IRC Text Formatting

RustyRig chat messages use IRC formatting control bytes. In GTK, shortcuts
insert visible `B`, `C`, `H`, `I`, `O`, `R`, `U`, `S` and `M` markers styled with
reverse colors; each behaves like one normal character. Markers inserted by
shortcuts or restored from history become IRC control bytes when sent. The `H`
marker represents a restored hex-color control. Typed or pasted letters remain
ordinary text. Backspace, Delete, selection and input history work normally on
markers.

## Formatting Controls

| Byte | Meaning | GTK shortcut |
| --- | --- | --- |
| `0x02` (`\002`) | Toggle bold | Ctrl+B |
| `0x03` (`\003`) | Set foreground and optional background color | Ctrl+C |
| `0x04` (`\004`) | Set RGB foreground and optional background | Preserved on paste/history |
| `0x0F` (`\017`) | Reset formatting | Ctrl+O |
| `0x11` (`\021`) | Toggle monospace | Ctrl+Shift+M |
| `0x16` (`\026`) | Toggle reverse foreground/background | Ctrl+R |
| `0x1D` (`\035`) | Toggle italic | Ctrl+I |
| `0x1E` (`\036`) | Toggle strikethrough | Ctrl+Shift+S |
| `0x1F` (`\037`) | Toggle underline | Ctrl+Shift+U |

Styles toggle independently. Reset ends all active styles and clears colors.
GTK renders all controls in this table. TUI renders bold, italic, underline,
reverse, strikethrough and reset. The TUI consumes monospace controls without a
visual change because terminal text is already monospace.

## Indexed Colors

The color control takes one or two decimal digits in the range 0-99. Use
`\003fg` for a foreground and `\003fg,bg` for foreground plus background. A
comma belongs to the color code only when followed by a background index. A
color control without a foreground index resets both colors. A foreground-only
code preserves the previous background. Index 99 restores the default for the
selected foreground or background independently. Indices 0-15 are listed below;
16-98 use the fixed extended IRC palette.

| Index | Color | RGB |
| ---: | --- | --- |
| 0 | bright-white | `#ffffff` |
| 1 | black | `#000000` |
| 2 | blue | `#00007f` |
| 3 | green | `#009300` |
| 4 | bright-red | `#ff0000` |
| 5 | brown | `#7f0000` |
| 6 | magenta | `#9c009c` |
| 7 | orange | `#fc7f00` |
| 8 | bright-yellow | `#ffff00` |
| 9 | bright-green | `#00fc00` |
| 10 | cyan | `#009393` |
| 11 | bright-cyan | `#00ffff` |
| 12 | bright-blue | `#0000fc` |
| 13 | bright-magenta | `#ff00ff` |
| 14 | bright-black | `#7f7f7f` |
| 15 | white | `#d2d2d2` |

For example, `\00304red text\017` selects foreground 4, and
`\0033,1green on black\017` selects foreground 3 and background 1. After
Ctrl+C, GTK displays the available foreground colors as numeric samples, but
the palette is optional: type the foreground digits directly, then an optional
comma and background digits. Typing a comma shows numeric foreground/background
pairs for that foreground. After choosing a swatch, the input caret remains
ready for typing. The popup closes when a character other than a comma or digit
is typed; Escape also closes it.

## Config Values

Theme and TUI status-line values accept readable IRC escapes. `\C` starts a
color control, followed by an optional foreground index and optional
`,background` index. Style escapes are `\B` bold, `\I` italic, `\U` underline,
`\S` strikethrough, `\M` monospace, `\R` reverse and `\O` reset. For example:

```ini
tui.status-line=\C12${window}\O A:${vfo_a_freq_khz:---} kHz
theme.status=\OStatus: \C09${online}\O
```

The config parser expands these escapes only in `theme.*`, `ui.theme.*`,
`tui.status-line` and `tui.room-status-line` values. Other config values and
paths are unchanged.

## Hex Colors

Both native frontends support `0x04` followed by exactly six hexadecimal digits:
`\004RRGGBB` or `\004RRGGBB,RRGGBB`. For example,
`\004FF8800,001122text\017` displays orange text on a dark background.
Hex and indexed colors share the same foreground/background state. Bare color
controls reset both colors without turning off bold or other styles. Incomplete
parameters remain visible, including a comma without a valid background.

GTK renders the exact palette/RGB values and preserves hex controls in input
and history. The Ctrl+C picker offers all 100 indices in a 10-column grid;
choices insert two digits so following numeric text is preserved. TUI uses
256-color terminal equivalents for the extended palette and true-color ANSI
sequences for hex colors. Hex display requires a true-color terminal.

See the [IRC formatting specification](https://modern.ircdocs.horse/formatting)
for the extended palette and control syntax. IRC transport remains native-only.
