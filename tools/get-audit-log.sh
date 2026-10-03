#!/bin/sh
DBDIR=/var/lib/rustyrig/db/

sqlite3 ${DBDIR}/master.db 'select * from audit_log order by timestamp asc'
