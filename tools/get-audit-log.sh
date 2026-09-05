#!/bin/sh
sqlite3 db/master.db 'select * from audit_log order by timestamp asc'
