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

## Usage

```sh
make test                       # build configured programs, then run maintained suites
TEST_SUITES="fwdsp rrclient" make test # select suites for make test
./tests/run-tests.sh            # run every suite, including librustyaxe
./tests/run-tests.sh rrclient   # run one suite
```

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
