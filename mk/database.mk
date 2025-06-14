MASTER_DB := $(strip $(shell cat ${CF} | jq -r ".database.master.path"))
## ifeq (${USE_SQLITE},true)
CFLAGS += -DUSE_SQLITE
LDFLAGS += -lsqlite3
## endif

${MASTER_DB}:
	mkdir -p $(shell dirname "${MASTER_DB}")
	sqlite3 $@ < sql/sqlite.master.sql

dump-ptt:
	echo "select * from ptt_log order BY start_time;" | sqlite3 ${MASTER_DB}

dump-log:
	echo "select * from audit_log;" | sqlite3 ${MASTER_DB}
