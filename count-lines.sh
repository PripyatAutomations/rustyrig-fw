#!/bin/bash
SZ_C=$(ls *.[ch]|grep -v mongoose|xargs cat|wc -l); SZ_PL=$(cat buildconf.pl | wc -l); SZ_JS=$(cat www/js/{audio,emoji,webui}.js |wc -l); SZ_HTML=$(cat www/index.html | wc -l); SZ_CSS=$(cat www/css/*.css | wc -l); echo "LOC in rustyrig-fw: C ${SZ_C} Perl ${SZ_PL} JavaScript ${SZ_JS} HTML ${SZ_HTML} CSS ${SZ_CSS}"
