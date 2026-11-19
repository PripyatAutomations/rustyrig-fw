# rustyrig-fw test suite

This directory contains the test runner, shared shell helpers, and the
self-test for the main repository. Component tests live beside the component
they exercise.

## Layout

Component suites are kept with their source tree:

```
fwdsp/tests/             # fwdsp tests
librrprotocol/tests/     # protocol tests
rrclient/tests/          # native client and WebUI parity tests
rrserver/tests/          # server tests
librustyaxe/tests/       # authoritative external-submodule tests
www/tests/               # authoritative external-submodule WebUI tests
tests/
├── run-tests.sh         # runs the component suites
├── common.sh            # reusable shell helpers
└── selftest/            # tests for the shared helpers
```

Existing in-tree suites (e.g. `librustyaxe/tests/`, `fwdsp/tests/`) are
invoked through their own Makefiles so they remain the source of truth.
Generated RustyAxe test objects and executables are placed below
`build/tests/librustyaxe` and are removed after `check`, keeping component
source directories clean.

## Usage

```sh
make test                       # build configured programs, then run maintained suites
TEST_SUITES="fwdsp rrclient" make test # select suites for make test
./tests/run-tests.sh            # run every suite, including librustyaxe
./tests/run-tests.sh rrclient   # run one suite
```

The native mesh/WebSocket integration tests read the authoritative
`wire-schema.json` from the sibling `librrprotocol` source checkout. When
running against installed packages instead, set `RR_WIRE_SCHEMA` to the
corresponding schema file (or install it at
`/usr/share/librrprotocol/wire-schema.json`). Missing schemas produce a
clear error rather than silently weakening wire-message validation.

## IRC and DCC tests

The IRC implementation lives in the sibling `librustyaxe/librustyirc/`
source directory. It builds as a **separate** `librustyirc.so.0` shared
library; it is not linked into `librustyaxe.so`.

IRC tests build a temporary RustyRig transport adapter, then link against
the installed `librustyirc` and its declared dependencies. The adapter
first links a small ABI probe covering its required protocol entry points.
If that probe fails, rebuild/install the matching `librustyirc` library and
development package from the sibling `librustyaxe` checkout (and refresh
the dynamic linker cache if required). Do **not** replace missing protocol
functions with no-op test stubs: parsing, registration, DCC offers and
authentication must execute the actual library code.

Use `bash rrclient/tests/test_irc_protocol.sh` and
`bash rrclient/tests/test_dcc_commands.sh` for focused reruns. A successful
RustyRig application build does not demonstrate that an old installed
`librustyirc.so.0` exports the current API.

## Writing a new suite

Create `<component>/tests/` and either:

1. A Makefile with a `check` target (C tests), or
2. Shell scripts named `test_*.sh` that source `tests/common.sh` when they
   need the assertion helpers (`assert_eq`, `assert_contains`, `assert_ok`).

Register the component name in `tests/run-tests.sh` when adding a new suite.
The default `make test` excludes `librustyaxe` because it is an external
submodule with its own test state; pass `TEST_SUITES="... librustyaxe"` to
include it when its tests are ready. The direct runner with no arguments still
invokes all registered suites.
