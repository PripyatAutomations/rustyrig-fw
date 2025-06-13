#!/bin/bash
SZ_C=$(ls *.c common/* inc/rustyrig/*.h inc/common/*.h|grep -v mongoose|xargs cat|wc -l)
SZ_PL=$(cat buildconf.pl | wc -l)
SZ_JS=$(cat www/js/webui*.js |wc -l)
SZ_HTML=$(cat www/index.html | wc -l)
SZ_CSS=$(cat www/css/*.css | wc -l)
SZ_SH=$(cat *.sh | wc -l)
SZ_SQL=$(cat sql/*.sql | wc -l)
SZ_C_RRCLI=$(ls gtk-client/*.c inc/rrclient/*.h|grep -v mongoose|xargs cat|wc -l)
SZ_TTL=$((${SZ_C} + ${SZ_PL} + ${SZ_JS} + ${SZ_HTML} + ${SZ_CSS} + ${SZ_SH} + ${SZ_SQL} + ${SZ_C_RRCLI}))
echo "LOC in $(basename $(pwd)): C=${SZ_C} Perl=${SZ_PL} JS=${SZ_JS} HTML=${SZ_HTML} CSS=${SZ_CSS} Shell=${SZ_SH} SQL=${SZ_SQL}. Client: C=${SZ_C_RRCLI}. Grand Total: ${SZ_TTL}"
