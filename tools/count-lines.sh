#!/bin/bash
SZ_C=$(ls librustyaxe/*.c rrserver/* inc/rrserver/*.h inc/common/*.h|grep -v mongoose|xargs cat|wc -l)
SZ_PL=$(cat tools/*.pl | wc -l)
SZ_JS=$(cat www/js/webui*.js |wc -l)
SZ_HTML=$(cat www/index.html | wc -l)
SZ_CSS=$(cat www/css/*.css | wc -l)
SZ_SH=$(cat *.sh tools/*.sh | wc -l)
SZ_SQL=$(cat sql/*.sql | wc -l)
SZ_C_RRCLI=$(ls rrclient/*.c inc/rrclient/*.h|grep -v mongoose|xargs cat|wc -l)
SZ_TTL=$((${SZ_C} + ${SZ_PL} + ${SZ_JS} + ${SZ_HTML} + ${SZ_CSS} + ${SZ_SH} + ${SZ_SQL} + ${SZ_C_RRCLI}))
echo "Lines of code in $(basename $(pwd)) project. rrserver: C ${SZ_C}. rrclient: C ${SZ_C_RRCLI}. WebUI: JavaScript ${SZ_JS}, HTML ${SZ_HTML}, CSS ${SZ_CSS}. BuildEnv: Perl ${SZ_PL}, Shell ${SZ_SH}, SQL ${SZ_SQL}. Grand Total of ${SZ_TTL} lines of new code."

