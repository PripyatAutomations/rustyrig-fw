#MASTER_DB := $(strip $(shell cat ${CF} | jq -r ".path.db.master"))
#MASTER_TEMPLATE := $(strip $(shell cat ${CF} | jq -r ".path.db.master.template"))
MASTER_DB=db/master.db
MASTER_TEMPLATE=sql/sqlite.master.sql

## ifeq (${USE_SQLITE},true)
CFLAGS += -DUSE_SQLITE
LDFLAGS += -lsqlite3
## endif

${MASTER_DB}:
	mkdir -p $(shell dirname "${MASTER_DB}")
	mkdir -p audit-logs build db
	sqlite3 $@ < "${MASTER_TEMPLATE}"

dump-ptt:
	echo "select * from ptt_log order BY start_time;" | sqlite3 "${MASTER_DB}"

dump-log:
	echo "select * from audit_log;" | sqlite3 "${MASTER_DB}"

dump-chat:
	echo "select * from chat_log;" | sqlite3 "${MASTER_DB}"
