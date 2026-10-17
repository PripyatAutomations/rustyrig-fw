#!/bin/bash
if [ -z "$1" ]; then
   echo "Usage: $0 <username>"
   echo "   Clear NOOB flag on a user"
   exit 1
fi

sqlite3 /var/lib/rustyrig/master.db "UPDATE users SET permissions = trim(replace(',' || permissions || ',', ',noob,', ','), ',' ) WHERE name = '$1';"
