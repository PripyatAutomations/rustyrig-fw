#!/bin/sh
sqlite3 db/master.db 'select * from ptt_log'
