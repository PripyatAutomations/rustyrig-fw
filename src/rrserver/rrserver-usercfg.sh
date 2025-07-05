#!/bin/bash
set -e

# XXX: We need to find the rrserver.cfg and use it to locate the password file instead of hardcoding it
AUTHFILE="${AUTHFILE:-config/http.users}"
LOCKFILE="$AUTHFILE.lock"

# Lock and unlock functions
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
trap unlock_file EXIT

# Helpers
get_next_uid() {
   awk -F: 'BEGIN { max=0 } /^[[:space:]]*#/ { next } /^[[:space:]]*$/ { next } { if ($1+0 > max) max = $1+0 } END { print max+1 }' "$AUTHFILE" 2>/dev/null
}
prompt() {
   local label="$1"
   local default="$2"
   echo -n "$label [$default]: "
   read ans
   PROMPT_RESULT="${ans:-$default}"
}

# Actions
update_password() {
    local username="$1" new_hash="$2" tmpfile
    tmpfile=$(mktemp)

    awk -F: -v OFS=: -v user="$username" -v hash="$new_hash" '
    /^[[:space:]]*$/ || /^[[:space:]]*#/ { print; next }
    {
        if ($2 == user) { $4 = hash; updated = 1 }
        print
    }
    END { if (!updated) exit 2 }
    ' "$AUTHFILE" > "$tmpfile"

    if [ $? -eq 2 ]; then echo "User '$username' not found." && rm -f "$tmpfile" && return 1
    else cp -a "$AUTHFILE" "$AUTHFILE.bak.$(date +%Y%m%d%H%M%S)"; mv "$tmpfile" "$AUTHFILE"; echo "Password updated."; fi
}

delete_user() {
    local username="$1" tmpfile
    tmpfile=$(mktemp)

    awk -F: -v user="$username" '
    /^[[:space:]]*$/ || /^[[:space:]]*#/ { print; next }
    $2 != user { print }
    $2 == user { deleted = 1 }
    END { if (!deleted) exit 2 }
    ' "$AUTHFILE" > "$tmpfile"

    if [ $? -eq 2 ]; then echo "User '$username' not found." && rm -f "$tmpfile" && return 1
    else cp -a "$AUTHFILE" "$AUTHFILE.bak.$(date +%Y%m%d%H%M%S)"; mv "$tmpfile" "$AUTHFILE"; echo "User '$username' deleted."; fi
}

toggle_user_lock() {
    local username="$1" enable="$2" tmpfile
    tmpfile=$(mktemp)

    awk -F: -v OFS=: -v user="$username" -v enable="$enable" '
    /^[[:space:]]*$/ || /^[[:space:]]*#/ { print; next }
    {
        if ($2 == user) { $3 = enable; changed = 1 }
        print
    }
    END { if (!changed) exit 2 }
    ' "$AUTHFILE" > "$tmpfile"

    if [ $? -eq 2 ]; then echo "User '$username' not found." && rm -f "$tmpfile" && return 1
    else cp -a "$AUTHFILE" "$AUTHFILE.bak.$(date +%Y%m%d%H%M%S)"; mv "$tmpfile" "$AUTHFILE"
         [ "$enable" = "1" ] && echo "User '$username' unlocked." || echo "User '$username' locked."
    fi
}

list_users() {
    echo "UID  USERNAME  ENABLED  EMAIL  PERMISSIONS"
    awk -F: '!/^[[:space:]]*#/ && NF >= 7 { printf "%-4s %-9s %-7s %-20s %-s\n", $1, $2, $3, $5, $7 }' "$AUTHFILE"
}

show_help() {
    cat <<EOF
Usage: $0 [OPTIONS]

Manage users in $AUTHFILE

OPTIONS:
  -h, --help           Show this help message
  -l                   List users
  -r <username>        Remove user
  -L <username>        Lock user (disable)
  -U <username>        Unlock user (enable)

--username USER        Set username
--password PASS        Set password (plaintext, will be hashed)
--email EMAIL          Set email
--permissions PERMS    Comma-separated permissions
--maxclones N          Max clones (default: 3)
--enabled y|n          Account enabled (default: y)

If no options are given, runs interactively.
EOF
}

# Default values
ENABLED_ANSWER="y"
EMAIL="none"
PERMS="none"
MAXCLONES="3"
USERNAME=""
PASSWORD=""
INTERACTIVE=true

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) show_help; exit 0 ;;
        -l)        lock_file; list_users; exit 0 ;;
        -r)        lock_file; delete_user "$2"; exit $? ;;
        -L)        lock_file; toggle_user_lock "$2" 0; exit $? ;;
        -U)        lock_file; toggle_user_lock "$2" 1; exit $? ;;
        --username) USERNAME="$2"; INTERACTIVE=false; shift ;;
        --password) PASSWORD="$2"; INTERACTIVE=false; shift ;;
        --email) EMAIL="$2"; shift ;;
        --permissions) PERMS="$2"; shift ;;
        --maxclones) MAXCLONES="$2"; shift ;;
        --enabled) ENABLED_ANSWER="$2"; shift ;;
        *) echo "Unknown option: $1"; show_help; exit 1 ;;
    esac
    shift
done

lock_file

if [ "$INTERACTIVE" = true ]; then
    echo "*** Adding or updating user (interactive mode) ***"

    NEXT_UID=$(get_next_uid)
    echo "Next available uid: ${NEXT_UID}"

    prompt "Enter username" ""
    USERNAME="$PROMPT_RESULT"

    prompt "Enable account? (y/n)" "$ENABLED_ANSWER"
    ENABLED_ANSWER="$PROMPT_RESULT"

    prompt "Email" "$EMAIL"
    EMAIL="$PROMPT_RESULT"

    prompt "Max clones" "$MAXCLONES"
    MAXCLONES="$PROMPT_RESULT"

    prompt "Permissions (comma-separated)" "$PERMS"
    PERMS="$PROMPT_RESULT"

    ENABLED=0
    [ "$ENABLED_ANSWER" = "y" ] && ENABLED=1

    TRIES=0
    MAXTRIES=3
    while [ $TRIES -lt $MAXTRIES ]; do
        printf "Enter password: "
        stty -echo; read PASS1; stty echo; echo
        printf "Re-enter password: "
        stty -echo; read PASS2; stty echo; echo

        [ "$PASS1" = "$PASS2" ] && [ -n "$PASS1" ] && PASSWORD="$PASS1" && break

        echo "Passwords did not match or were empty. Try again."
        TRIES=$((TRIES+1))
    done
    [ $TRIES -eq $MAXTRIES ] && echo "Too many failures." && exit 1
else
    [ -z "$USERNAME" ] && echo "Error: --username is required." && exit 1
    [ -z "$PASSWORD" ] && echo "Error: --password is required." && exit 1
    [ "$ENABLED_ANSWER" = "y" ] && ENABLED=1 || ENABLED=0
fi

# Hash password
PASSHASH=$(printf "%s" "$PASSWORD" | sha1sum | awk '{print $1}')
LINE="$(get_next_uid):$USERNAME:$ENABLED:$PASSHASH:$EMAIL:$MAXCLONES:$PERMS"

# Update or add user
if grep -q -E "^([^#].*:)${USERNAME}(:|:.*:)" "$AUTHFILE"; then
    update_password "$USERNAME" "$PASSHASH" || exit 1
else
    echo "$LINE" >> "$AUTHFILE" || { echo "Failed to write to $AUTHFILE"; exit 1; }
    echo "User $USERNAME added."
fi
