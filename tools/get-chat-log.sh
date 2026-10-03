#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
SOURCE_ROOT=$(dirname -- "$SCRIPT_DIR")

if [ "$(basename -- "$SCRIPT_DIR")" = tools ] && [ -f "$SOURCE_ROOT/GNUmakefile" ]; then
   DBPATH="$SOURCE_ROOT/db/"
   DBFILE="${DBPATH}master.db"
else
   PROFILE=${PROFILE:-radio}
   CONFIG_DIR=${RUSTYRIG_CONFIG_DIR:-/etc/rustyrig}
   CONFIG="${CONFIG_DIR}/${PROFILE}.config.json"
   if [ ! -r "$CONFIG" ]; then
      echo "Cannot read profile config: $CONFIG" >&2
      exit 1
   fi
   DBPATH=$(jq -er '.database.master.path | select(type == "string" and length > 0)' "$CONFIG") || {
      echo "No database.master.path in $CONFIG" >&2
      exit 1
   }
   DBFILE=$DBPATH
fi

sqlite3 "$DBFILE" 'select * from chat_log'
