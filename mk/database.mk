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

PTT_LINES := 50
LOG_LINES := 50
CHAT_LINES := 50

dump-ptt:
	@echo "*** PTT Log (Last ${PTT_LINES} lines) ***"
	echo "select * from ptt_log order by start_time desc limit ${PTT_LINES};" | sqlite3 "${MASTER_DB}"

dump-log:
	@echo "*** Main Log (Last ${LOG_LINES} lines) ***"
	echo "select * from audit_log order by timestamp desc limit ${LOG_LINES};" | sqlite3 "${MASTER_DB}"

dump-chat:
	@echo "*** CHAT Log (Last ${CHAT_LINES} lines) ***"
	echo "select * from chat_log order by msg_ts desc limit ${CHAT_LINES};" | sqlite3 "${MASTER_DB}"

dump-all: dump-log dump-chat dump-ptt
