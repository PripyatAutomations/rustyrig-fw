#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
count() {
    find "$@" -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' \) -print0 |
        xargs -0 -r cat | wc -l
}
fwdsp=$(count libfwdspmgr fwdsp)
bridge=$(count rrbridge)
client=$(count rrclient rrclient/gtk)
server=$(count rrserver)
perl_lines=$(cat tools/*.pl | wc -l)
shell_lines=$(cat ./*.sh tools/*.sh | wc -l)
sql_lines=$(cat sql/*.sql | wc -l)
make_lines=$(cat GNUmakefile */rules.mk | wc -l)
total=$((fwdsp + bridge + client + server + perl_lines + shell_lines + sql_lines + make_lines))
printf 'RustyRig source lines: %d (fwdsp=%d bridge=%d client=%d server=%d Perl=%d shell=%d SQL=%d Make=%d)\n' \
    "$total" "$fwdsp" "$bridge" "$client" "$server" "$perl_lines" "$shell_lines" "$sql_lines" "$make_lines"
