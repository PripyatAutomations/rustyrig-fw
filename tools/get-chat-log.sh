#!/bin/sh
sqlite3 db/master.db 'select * from chat_log'
