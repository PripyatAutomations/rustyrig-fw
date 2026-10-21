# Runtime path policy (pre-release)

RustyRig is supported **only when installed**, either with distribution packages or the `make install` installation layout. Running rrserver, rrclient, rrbridge or fwdsp directly from a source checkout is not a supported runtime mode. Build and unit-test workflows may still use build-tree paths.

## Resolution

- Runtime configuration paths that start with `/` are absolute and used as configured.
- `~`, `$HOME`, and `${HOME}` are expanded against an **absolute** HOME; relative application paths (including `./name`) are resolved beneath HOME rather than the process working directory, via `librustyaxe/expand_path()`.
- If HOME is missing or not absolute, home-relative path resolution fails. It must not fall back to `.`, `/`, or another accidental writable location.
- Paths for executables, modules, bundled resources, web assets and database schemas are **installed paths**, not home-relative locations. Runtime code must not search `./bin`, `./www`, or the source checkout as a fallback.

## Conventional installed paths

- Executables: `/usr/bin` (or PATH lookup for installed executables).
- Server/bridge/client modules: `/usr/lib/rustyrig/modules/<family>`.
- Configuration: `/etc/rustyrig` (installation never implicitly overwrites local configuration).
- Shared server state: `/var/lib/rustyrig`, with database, SQL resources, modems, recordings and served web files beneath it.
- Runtime files: `/var/run/rustyrig` (or distribution-managed equivalent).
- User-specific writable paths: explicitly configured absolute paths or home-expanded paths.

**Important:** Service daemons and their package-managed runtime state should use the installed `/var/lib/rustyrig` paths; `$HOME` is not guaranteed to be a human login directory when running as a system account. Never silently redirect package state into `/root` or a developer's checkout.

## Audit checklist

Before adding a runtime filesystem path: choose its purpose (installed resource / shared service state / user state), pass configurable path values through `cfg_get_path()` or `expand_path()`, validate expansion, then fail with an actionable error if inaccessible. Relative paths must not be passed directly to `open()`, `fopen()`, `mkdir()`, `execve()`, etc. Unit-test fixtures and compiler/build paths are exceptions.

Future cleanup: audit legacy run-from-checkout instructions, sample configs, helper scripts, and any unexpanded relative paths in less commonly used embedded or Windows paths. Do not assume this initial pass fixes every legacy call site.
