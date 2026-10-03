#!/bin/sh
set -eu

case "$0" in
   ./*)
      DBPATH=./db/
      DBFILE="${DBPATH}master.db"
      ;;
   *)
      PROFILE=${PROFILE:-radio}
      CONFIG="config/${PROFILE}.config.json"
      if [ ! -r "$CONFIG" ]; then
         echo "Cannot read profile config: $CONFIG" >&2
         exit 1
      fi
      DBPATH=$(jq -er '.database.master.path | select(type == "string" and length > 0)' "$CONFIG") || {
         echo "No database.master.path in $CONFIG" >&2
         exit 1
      }
      DBFILE=$DBPATH
      ;;
esac

sqlite3 "$DBFILE" 'select * from audit_log order by timestamp asc'
