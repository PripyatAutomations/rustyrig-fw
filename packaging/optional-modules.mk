# Optional module registry shared by native builds and Debian packaging.
# Each entry names its SDK, build-profile switch, make feature and package payload.
OPTIONAL_MODULES := hamlib
OPTIONAL_hamlib_PKGCONFIG := hamlib
OPTIONAL_hamlib_LABEL := Hamlib radio support
OPTIONAL_hamlib_DEB_DEPS := libhamlib-dev
OPTIONAL_hamlib_CONFIG := .backend.hamlib
OPTIONAL_hamlib_FEATURE := USE_HAMLIB
OPTIONAL_hamlib_SWITCH := WITH_HAMLIB
OPTIONAL_hamlib_PACKAGE := rustyrig-server-hamlib
OPTIONAL_hamlib_ARTIFACT := bin/rrserver-hamlib.so
OPTIONAL_hamlib_DEST := usr/lib/rustyrig/modules/rrserver/rrserver-hamlib.so
