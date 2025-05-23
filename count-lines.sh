#!/bin/bash
SZ_C=$(ls *.c inc/*.h|grep -v mongoose|xargs cat|wc -l)
SZ_PL=$(cat buildconf.pl | wc -l)
SZ_JS=$(cat www/js/webui*.js |wc -l)
SZ_HTML=$(cat www/index.html | wc -l)
SZ_CSS=$(cat www/css/*.css | wc -l)
SZ_SH=$(cat *.sh | wc -l)
SZ_TTL=$((${SZ_C} + ${SZ_PL} + ${SZ_JS} + ${SZ_HTML} + ${SZ_CSS} + ${SZ_SH}))

echo "LOC in $(basename $(pwd)): C ${SZ_C} Perl ${SZ_PL} JavaScript ${SZ_JS} HTML ${SZ_HTML} CSS ${SZ_CSS} BASH ${SZ_SH} for a total of ${SZ_TTL} lines."
