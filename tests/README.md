# rustyrig-fw test suite

This directory contains the test runner, shared shell helpers, and the
self-test for the main repository. Component tests live beside the component
they exercise.

## Layout

Component suites are kept with their source tree:

```
fwdsp/tests/             # fwdsp tests
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

## Usage

```sh
./tests/run-tests.sh            # run everything
./tests/run-tests.sh rrclient     # run one suite
```

## Writing a new suite

Create `<component>/tests/` and either:

1. A Makefile with a `check` target (C tests), or
2. Shell scripts named `test_*.sh` that source `tests/common.sh` when they
   need the assertion helpers (`assert_eq`, `assert_contains`, `assert_ok`).

Register the component name in `tests/run-tests.sh` when adding a new suite.
