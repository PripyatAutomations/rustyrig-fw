#!/bin/bash
set -e

AUTHFILE="${AUTHFILE:-config/http.users}"
LOCKFILE="$AUTHFILE.lock"
TRIES=0
MAXTRIES=3

# Lock the file (simple flock-style lock)
lock_file() {
   exec 9>"$LOCKFILE" || exit 1
   flock -n 9 || {
      echo "Could not acquire lock on $AUTHFILE"
      exit 1
   }
}

unlock_file() {
   rm -f "$LOCKFILE"
   exec 9>&-
}

get_next_uid() {
   awk -F: 'BEGIN { max=0 } { if ($1+0 > max) max = $1+0 } END { print max+1 }' "$AUTHFILE" 2>/dev/null
}

prompt() {
   local label="$1"
   local default="$2"
   echo "$label [$default]"
   #printf "%s [%s]: " "$label" "$default"
   read ans
   if [ -z "$ans" ]; then
      PROMPT_RESULT="$default"
   else
      PROMPT_RESULT="$ans"
   fi
}

echo "*** Adding user to $AUTHFILE ***"
lock_file

# Back up the database
cp -a "$AUTHFILE" "$AUTHFILE.bak.$(date +%Y%m%d%H%M%S)"

NEXT_UID=$(get_next_uid)
echo "Next available uid: ${NEXT_UID}"


prompt "Enter username" ""
USERNAME="$PROMPT_RESULT"

prompt "Enable account? (y/n)" "y"
ENABLED_ANSWER="$PROMPT_RESULT"

prompt "Email" "none"
EMAIL="$PROMPT_RESULT"

prompt "Max clones" "3"
MAXCLONES="$PROMPT_RESULT"

prompt "Permissions (comma-separated)" "none"
PERMS="$PROMPT_RESULT"

ENABLED=0
[ "$ENABLED_ANSWER" = "y" ] && ENABLED=1

while [ $TRIES -lt $MAXTRIES ]; do
   printf "Enter password: "
   stty -echo; read PASS1; stty echo; echo
   printf "Re-enter password: "
   stty -echo; read PASS2; stty echo; echo

   [ "$PASS1" = "$PASS2" ] && [ -n "$PASS1" ] && break

   echo "Passwords did not match or were empty. Try again."
   TRIES=$((TRIES+1))
done

[ $TRIES -eq $MAXTRIES ] && echo "Too many failures." && unlock_file && exit 1

PASSHASH=$(printf "%s" "$PASS1" | sha1sum | awk '{print $1}')
LINE="$NEXT_UID:$USERNAME:$ENABLED:$PASSHASH:$EMAIL:$MAXCLONES:$PERMS"

echo "$LINE" >> "$AUTHFILE" || { unlock_file; echo "Failed to write to $AUTHFILE"; exit 1; }

unlock_file
echo "User $USERNAME added with uid $NEXT_UID."
exit 0
