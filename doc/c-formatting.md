# C formatting

Use Uncrustify with the repository's `.uncrustify.cfg` for consistent C formatting.
The style follows librustyaxe: three spaces, no indentation tabs, opening braces
on the same line (including functions), `} else {`, and a 160-column line limit.
Always brace if/else, for, while and do bodies, including single statements.
The existing full-brace settings add missing braces; one-line controls expand.

Preview a file from the repository root without changing the source:

```sh
uncrustify -c .uncrustify.cfg -l C -f librustyaxe/config.c -o /tmp/config.formatted.c
diff -u librustyaxe/config.c /tmp/config.formatted.c
```

After reviewing, format selected owned files with:

```sh
uncrustify -c .uncrustify.cfg -l C --replace --no-backup librustyaxe/config.c
```

Never include `ext/` or the separate `www/` submodule in a native C formatting
pass. `tools/indent.sh` formats tracked C/header files recursively, including frontend
and test directories and the owned librustyaxe, librrprotocol and callsign-lookup
submodules. Incomplete reference fragments in `snippets/` are excluded because
they are not parseable C translation units. Run `bash tools/indent.sh` from any
directory. Statements are split after semicolons, except for the separators in
`for (...)` headers.
Keep a tree-wide formatting change separate from behavior changes and build/test
all affected profiles after adding braces, especially conditional compilation.

GNU indent can approximate the layout using the root `.indent.pro`:

```sh
indent -st librustyaxe/config.c > /tmp/config.indent.c
```

Run from the repository root so it discovers the profile. GNU indent is optional
and does not enforce missing control-flow braces; prefer Uncrustify for that.
The GNU indent profile was validated with GNU indent 2.2.13.

Both formatters use 160 as their wrapping limit. Long indivisible tokens, string
literals, macros or preserved comments may still exceed it and need manual review;
never truncate them. Check formatted files for remaining overlong lines:

```sh
awk 'length($0) > 160 { print FILENAME ":" FNR ":" length($0) }' librustyaxe/config.c
```
