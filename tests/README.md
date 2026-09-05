# rustyrig-fw test suite

This directory collects reusable tests for the parts of the project.

## Layout

Each component gets its own subdirectory mirroring the source tree:

```
tests/
├── run-tests.sh        # runs every registered test suite
├── common.sh           # reusable shell helpers (assertions, counters)
├── librustyaxe/        # unit tests for the rustyaxe library
└── fwdsp/              # fwdsp tests
```

Existing in-tree suites (e.g. `librustyaxe/tests/`, `fwdsp/tests/`) are
invoked through their own Makefiles so they remain the source of truth.

## Usage

```sh
./tests/run-tests.sh            # run everything
./tests/run-tests.sh librustyaxe  # run one suite
```

## Writing a new suite

Create `tests/<component>/` and either:

1. A Makefile with a `check` target (C tests), or
2. Shell scripts named `test_*.sh` that `source ../common.sh` and use the
   assertion helpers (`assert_eq`, `assert_contains`, `assert_ok`).

`run-tests.sh` picks up both automatically.
