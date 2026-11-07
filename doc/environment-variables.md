# Environment variables used by RustyRig

This inventory covers explicit environment reads in the client, server, DSP
worker, helper scripts and test harnesses. Runtime behavior belongs in the
configuration file unless an external process/session genuinely provides it.

## Production

| Variable | Reader | Purpose |
| --- | --- | --- |
| `HOME` | rrclient config/save paths; rrserver serial and audio; fwdsp audio sockets | Home-relative paths and `$HOME` expansion |
| `DISPLAY` | rrclient/main.c | Detect a graphical session for GTK frontend selection |
| `RRCLIENT_RECEIVER_DIRECTORY_URL` | rrclient/receiver.directory.c | Development/source override for `receiver-directory.url` |
| `PROFILE` | tools/rr-get-{ptt,chat,audit}-log | Choose a deployed config profile (default `radio`) |
| `RUSTYRIG_CONFIG_DIR` | same three log tools | Config directory (default `/etc/rustyrig`) |

Additionally, GStreamer itself may read standard `GST_DEBUG` and
`GST_DEBUG_DUMP_DOT_DIR`. RustyRig configures `GST_DEBUG` from its
logging settings where applicable. The worker no longer forces DOT dumps
into the current working directory; users can opt in by setting the normal
GStreamer variable.

## Test-only

| Variable | Reader | Purpose |
| --- | --- | --- |
| `RR_MESH_SMOKE_ONLY` | rrserver/tests/mesh_native_live.py | Skip extended mesh security tests |
| `FWDSP_SOAK_SECONDS`, `FWDSP_SOAK_CODEC` | fwdsp/tests/switching.c | Codec stress-test controls |
| `RR_RTP_TEST_ENCRYPTED` | rrclient/tests/{dcc_audio,media_direct}.c | Test encrypted RTP |
| `RR_FILES_SCREENSHOT` | rrclient/tests/gtk_files.c | Optional screenshot output |
| `RRCLIENT_KIWI_TEST_LIVE_JSON` | rrclient/tests/receiver_directory.c | Optional live receiver directory fixture |
| `RRCLIENT_RECEIVER_DIRECTORY_URL` | receiver-directory tests | Test-specific source override |
| `PATH` | rrserver/tests/bridge_validation.c | Controlled helper-program lookup |

Removed in the November 2026 cleanup:
- `RRCLIENT_GTK_WIDTH_DIAGNOSTIC` and its temporary GTK width callback.
- `RR_MESH_FIXED_PORTS`, which selected an obsolete 4420/4422 migration
  scenario in a mesh live test.
- Unconditional `GST_DEBUG_DUMP_DOT_DIR=.` in the fwdsp worker.

The last two removals change only the legacy test path and the DSP worker's
default debugging environment, not the receiver/network protocol.
