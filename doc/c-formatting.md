# C formatting

Use GNU indent with `.indent.pro`, followed by Uncrustify with `.uncrustify.cfg`.
The second pass applies the final repository style and adds missing braces.
The style follows librustyaxe: three spaces, no indentation tabs, opening braces
on the same line (including functions), `} else {`, and a 160-column line limit.
Always brace if/else, for, while and do bodies, including single statements.
The existing full-brace settings add missing braces; one-line controls expand.

Preview a file from the repository root without changing the source:

```sh
mapfile -t indent_options < .indent.pro
indent -npro "${indent_options[@]}" librustyaxe/config.c -o /tmp/config.indent.c
uncrustify -c .uncrustify.cfg -l C -f /tmp/config.indent.c -o /tmp/config.formatted.c
diff -u librustyaxe/config.c /tmp/config.formatted.c
```

After reviewing, copy the final output back to the selected source:

```sh
cat /tmp/config.formatted.c > librustyaxe/config.c
```

Never include `ext/` or the separate `www/` submodule in a native C formatting
pass. `tools/indent.sh` formats tracked C/header files recursively, including frontend
and test directories and the owned librustyaxe, librrprotocol and callsign-lookup
submodules. Generated wire-registry and EEPROM headers are excluded; regenerate
them using their generators. Incomplete reference fragments in `snippets/` are excluded because
they are not parseable C translation units. Run `bash tools/indent.sh` from any
directory, or use `make indent`. Both formatters must be installed before any
source is changed. Each file is formatted in temporary storage and copied back
only when both passes succeed; a failure stops the run, preserving that file.
Earlier successfully formatted files remain changed. Unchanged files retain
their timestamps, and no backup files are left in the source tree.
Statements are split after semicolons, except for the separators in
`for (...)` headers.
Keep a tree-wide formatting change separate from behavior changes and build/test
all affected profiles after adding braces, especially conditional compilation.

The script passes `.indent.pro` options explicitly with `-npro`, ignoring personal
profiles. The GNU indent profile was validated with GNU indent 2.2.13.

Both formatters use 160 as their wrapping limit. Long indivisible tokens, string
literals, macros or preserved comments may still exceed it and need manual review;
never truncate them. Check formatted files for remaining overlong lines:

```sh
awk 'length($0) > 160 { print FILENAME ":" FNR ":" length($0) }' librustyaxe/config.c
```
