#!/bin/bash
SZ_C_RRFW=$(ls rrserver/*.[ch] | xargs cat | wc -l)
SZ_C_LRR=$(ls librustyaxe/*.[ch] | xargs cat | wc -l)
SZ_C_LRP=$(ls librrprotocol/*.[ch] | xargs cat | wc -l)
SZ_C_FWDSP=$(ls libfwdspmgr/*.[ch] fwdsp/*.[ch] | xargs cat | wc -l)
SZ_C_RRCLI=$((ls rrclient/*.[ch]) | xargs cat | wc -l)

SZ_PL=$(cat tools/*.pl | wc -l)
SZ_HTML=$(cat www/index.html | wc -l)
SZ_CSS=$(cat www/css/*.css | wc -l)
SZ_SH=$(cat *.sh tools/*.sh | wc -l)
SZ_SQL=$(cat sql/*.sql | wc -l)
SZ_MK=$(cat GNUmakefile */rules.mk | wc -l)

SZ_C_TTL=$((${SZ_C_LRP} + ${SZ_C_LRR} + ${SZ_C_RRFW} + ${SZ_C_RRCLI} + ${SZ_C_FWDSP}))
SZ_JS_TTL=$(cat www/js/webui*.js | wc -l)
SZ_WEB_TTL=$((${SZ_HTML} + ${SZ_CSS} + ${SZ_JS_TTL}))
SZ_TTL=$((${SZ_C_TTL} + ${SZ_PL} + ${SZ_WEB_TTL} + ${SZ_SH} + ${SZ_SQL} + ${SZ_MK}))


echo -e "Lines of code in rustyrig-fw: " \
        "[librustyaxe C: ${SZ_C_LRR}] " \
        "[librrprotocol C: ${SZ_C_LRP}] " \
        "[fwdsp C: ${SZ_C_FWDSP}] " \
        "[rrserver C: ${SZ_C_RRFW}] " \
        "[rrclient C: ${SZ_C_RRCLI}] " \
        "[WebUI JS: ${SZ_JS_TTL}, HTML: ${SZ_HTML}, CSS: ${SZ_CSS}] " \
        "[BuildEnv Perl: ${SZ_PL}, gmake: ${SZ_MK}, SH: ${SZ_SH}, SQL: ${SZ_SQL}] " \
        "--- Grand Total: ${SZ_TTL} (C: ${SZ_C_TTL}, JS: ${SZ_JS_TTL} Perl: ${SZ_PL})"
