# GDB tracing recipes

Small, application-neutral command files. Source them **before** `run`, even
when the interesting functions live in libraries loaded later with `dlopen`.

```sh
gdb -q --args /usr/bin/rrclient
```

At the `(gdb)` prompt:

```gdb
source gdb/threads.gdb
run
```

Pick **one** trace to start with; each trace prints a backtrace and continues,
so multiple active traces can produce substantial logs. Each script enables
logging automatically, overwriting its own file under `/tmp` on each run:

| Script | Log file |
| --- | --- |
| `threads.gdb` | `/tmp/rr-gdb-threads.log` |
| `pixbuf-loads.gdb` | `/tmp/rr-gdb-pixbuf.log` |
| `processes.gdb` | `/tmp/rr-gdb-processes.log` |
| `glib-logs.gdb` | `/tmp/rr-gdb-glib.log` |
| `crashes.gdb` | `/tmp/rr-gdb-crashes.log` |

You can also launch
directly with `gdb -q -x gdb/pixbuf-loads.gdb --args /usr/bin/rrclient`;
then enter `run`.

| Script | What it traces |
| --- | --- |
| `threads.gdb` | Calls to `pthread_create`, including dependencies |
| `pixbuf-loads.gdb` | Image loader close and stream decoding |
| `processes.gdb` | Fork/vfork and common process-launch APIs |
| `glib-logs.gdb` | GLib `g_logv` warnings and other traditional log calls |
| `crashes.gdb` | SIGABRT/SIGSEGV/SIGBUS; stop and print all thread stacks |

For the GTK/Glycin image-loader investigation, try
`source gdb/pixbuf-loads.gdb`. The trace should show which application or
GTK caller asks for images to be decoded.

All scripts use `set breakpoint pending on`, which avoids the interactive
"Make breakpoint pending on future shared library load?" prompt. Breakpoints
can remain unresolved if a symbol is absent in that build; this is expected.

GDB paths in `source` resolve from GDB's current working directory, not
necessarily the executable's directory. Use an absolute path if necessary.
Each script is self-contained; no special initialization or extension is needed.

**Cautions**

- These are diagnostic traces, not performance benchmarks. Every backtrace
  incurs debugger overhead, especially in hot paths.
- `threads.gdb` may print multiple traces per logical creation when ASan
  interposes on `pthread_create`.
- `processes.gdb` intentionally stays attached to the parent by default.
- `crashes.gdb` deliberately does **not** auto-continue after a fatal signal.
- Under ASan, the more useful report may still be the sanitizer's own
  allocation/free stacks.

The scripts avoid RustyRig internals so they can later be moved to
`librustyaxe` unchanged.
