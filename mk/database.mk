ifeq (${USE_SQLITE},true)
CFLAGS += -DUSE_SQLITE
LDFLAGS += -lsqlite3
MASTER_DB := $(strip $(shell cat ${CF} | jq -r ".database.master.path"))
endif

${MASTER_DB}:
	mkdir -p $(shell dirname "${MASTER_DB}")
	sqlite3 $@ < sql/sqlite.master.sql

dump-ptt:
	echo -e ".headers on\nselect * from ptt_log order BY start_time;" | sqlite3 ${MASTER_DB}

dump-log:
	echo -e ".headers on\nselect * from audit_log;" | sqlite3 ${MASTER_DB}
