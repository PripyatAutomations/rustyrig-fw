#!/bin/bash
SZ_C=$(ls rrserver/*.[ch] | grep -v mongoose | xargs cat | wc -l)
SZ_C_SHARED=$(ls librustyaxe/*.[ch] | grep -v mongoose | xargs cat | wc -l)
SZ_C_RRCLI=$((ls rrclient/*.[ch]; find modsrc -type f -name \*.h; find modsrc -type f -name \*.c) | grep -v mongoose | xargs cat | wc -l)
SZ_PL=$(cat tools/*.pl | wc -l)
SZ_JS=$(cat www/js/webui*.js |wc -l)
SZ_HTML=$(cat www/index.html | wc -l)
SZ_CSS=$(cat www/css/*.css | wc -l)
SZ_SH=$(cat *.sh tools/*.sh | wc -l)
SZ_SQL=$(cat sql/*.sql | wc -l)
SZ_TTL=$((${SZ_C_SHARED} + ${SZ_C} + ${SZ_C_RRCLI} + ${SZ_PL} + ${SZ_JS} + ${SZ_HTML} + ${SZ_CSS} + ${SZ_SH} + ${SZ_SQL}))
echo "Lines of code in librustyaxe: C ${SZ_C_SHARED}, rrserver: C ${SZ_C}, rrclient: C ${SZ_C_RRCLI}. WebUI: JavaScript ${SZ_JS}, HTML ${SZ_HTML}, CSS ${SZ_CSS}. BuildEnv: Perl ${SZ_PL}, Shell ${SZ_SH}, SQL ${SZ_SQL}. Grand Total of ${SZ_TTL} lines of unique code."
