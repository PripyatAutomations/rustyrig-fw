#!/bin/bash
SZ_C_RRFW=$(ls rrserver/*.[ch] | grep -v mongoose | xargs cat | wc -l)
SZ_C_LRR=$(ls librustyaxe/*.[ch] | grep -v mongoose | xargs cat | wc -l)
SZ_C_LRP=$(ls librrprotocol/*.[ch] | grep -v mongoose | xargs cat | wc -l)
SZ_C_RRCLI=$((ls rrclient/*.[ch]) | grep -v mongoose | xargs cat | wc -l)
SZ_PL=$(cat tools/*.pl | wc -l)
SZ_JS_WEBUI=$(cat www/js/webui*.js | wc -l)
SZ_HTML=$(cat www/index.html | wc -l)
SZ_CSS=$(cat www/css/*.css | wc -l)
SZ_SH=$(cat *.sh tools/*.sh | wc -l)
SZ_SQL=$(cat sql/*.sql | wc -l)
SZ_TTL=$((${SZ_C_LRP} + ${SZ_C_LRR} + ${SZ_RR_FW} + ${SZ_C_RRCLI} + ${SZ_PL} + ${SZ_JS} + ${SZ_HTML} + ${SZ_CSS} + ${SZ_SH} + ${SZ_SQL}))
SZ_C_TTL=$((${SZ_C_LRP} + ${SZ_C_LRR} + ${SZ_C_RRFW} + ${SZ_C_RRCLI}))
SZ_JS_TTL=${SZ_JS_WEBUI}

echo -e "Lines of code in rustyrig-fw: " \
        "[librustyaxe C: ${SZ_C_LRR}] " \
        "[librrprotocol C: ${SZ_C_LRP}] " \
        "[rrserver C: ${SZ_C_RRFW}] " \
        "[rrclient C: ${SZ_C_RRCLI}] " \
        "[WebUI JS: ${SZ_JS_WEBUI}, HTML: ${SZ_HTML}, CSS: ${SZ_CSS}] " \
        "[BuildEnv Perl: ${SZ_PL}, SH: ${SZ_SH}, SQL: ${SZ_SQL}] " \
        "\tGrand Total: ${SZ_TTL} (C: ${SZ_C_TTL}, JS: ${SZ_JS_TTL})"
