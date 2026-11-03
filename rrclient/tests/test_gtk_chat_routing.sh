#!/usr/bin/env bash
set -euo pipefail
exec bash "$(dirname "$0")/test_gtk_formatting.sh" --irc-routing
