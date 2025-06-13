#!/bin/bash
#
# Print out bugs marked with "XXX:" or "BUG:" and print a little context
#
# We use pygments for python to colorize
#
LINES_BEFORE=5
LINES_AFTER=10

PYGMENTIZE_BIN="$(which pygmentize)"
[ -z "${LESS}" ] && LESS=$(which less)

THEME=friendly
[ -d "$(pwd)/pygments_styles" ] && export PYTHONPATH=$(pwd)/pygments_styles:$PYTHONPATH
[ ! -z "${PYGMENTIZE_BIN}" ] && PYGMENTIZE_CMD="pygmentize -f terminal256 -P style=$THEME"

try_colorize() {
   local file="$1"
   local language
   case "$file" in
      *.c|*.h) language=c ;;
      *.pl)    language=perl ;;
      *.js)    language=javascript ;;
      *.css)   language=css ;;
      *.sh)    language=bash ;;
      *)       language=text ;;
   esac
   egrep -Hni '(XXX|BUG):' -A${LINES_AFTER} -B${LINES_BEFORE} "$file" | \
      ${PYGMENTIZE_BIN:+$PYGMENTIZE_CMD -l $language} || cat
}



( find . -name \*.\[ch\] | grep -v mongoose | grep -v '^./ext/'; 
  find ./www/js -name 'webui*.js';
  find ./www/css -name '*.css') |
while read -r line; do
   try_colorize "$line"
done | ${LESS} -R
