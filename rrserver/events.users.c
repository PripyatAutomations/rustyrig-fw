//
// rrserver/events.users.c: users event listeners from
// librrprotocol
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/ws.mediachan.h>
#include <rrserver/database.h>
#include <rrserver/backend.h>
#include <rrserver/rig.properties.h>
#include <rrserver/rig.rooms.h>
#include <rrserver/rig.vfo.h>
#include <rrserver/rig.registry.h>
#include <rrserver/globalstate.h>
#include <rrserver/ptt.h>
#include <rrserver/supervision.h>
#include <rrserver/acl.h>
#include <rrserver/bridge.access.h>
#include <rrserver/bridge.manager.h>
#include <rrserver/media.direct.h>
#include <rrserver/relay.h>
#include <rrserver/usage.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <libfwdspmgr/fwdsp-ctl.h>

extern struct GlobalState rig;

#include <rrserver/events.internal.h>

#ifdef USE_SQLITE
/* Validate account names against protocol length and character limits. */
static bool user_name_valid(const char *name) {
   if (!name || !*name || strlen(name) > HTTP_USER_LEN) {
      return false;
   }

   for (const unsigned char *p = (const unsigned char *)name ; *p ; p++) {
      if (!isalnum(*p) && *p != '_' && *p != '-') {
         return false;
      }
   }

   return true;
}

/* Privileges are stored as a comma-separated list. Keep the list deliberately conservative here because this command changes authorization state. */
bool rrserver_user_privilege_list_valid(const char *privileges, bool allow_empty) {
   if (!privileges) {
      return false;
   }

   if (!*privileges) {
      return allow_empty;
   }

   if (strlen(privileges) > USER_PRIV_LEN) {
      return false;
   }

   const char *p = privileges;
   while (*p) {
      const char *end = strchr(p, ',');
      size_t len = end ? (size_t)(end - p) : strlen(p);

      if (len == 0 || len >= 64) {
         return false;
      }

      for (size_t i = 0 ; i < len ; i++) {
         unsigned char ch = (unsigned char)p[i];

         if (!isalnum(ch) && ch != '.' && ch != '_' && ch != '-' && ch != '*') {
            return false;
         }
      }

      if (end && end[1] == '\0') {
         return false;
      }
      p = end ? end + 1 : p + len;
   }
   return true;
}

/* Match one privilege token in a comma-separated list. */
static bool user_privilege_contains(const char *privileges, const char *wanted) {
   if (!privileges || !wanted || !*wanted) {
      return false;
   }
   char copy[USER_PRIV_LEN + 1];
   strlcpy(copy, privileges, sizeof(copy));
   char *save = NULL;

   for (char *token = strtok_r(copy, ",", &save) ; token ; token = strtok_r(NULL, ",", &save)) {
      if (strcasecmp(token, wanted) == 0) {
         return true;
      }
   }

   return false;
}

/* Detect grants that require owner-level account administration. */
bool rrserver_user_privilege_has_elevated(const char *privileges) {
   return privileges && (match_priv(privileges, "admin") || match_priv(privileges, "owner") ||
      match_priv(privileges, "bridge.admin") || match_priv(privileges, "websdr.connect") || match_priv(privileges, "websdr.admin"));
}

/* Build a bounded privilege union without duplicate tokens. */
static bool user_privilege_add_tokens(const char *base, const char *extra, char *out, size_t out_len) {
   if (!base || !extra || !out || out_len == 0) {
      return false;
   }
   snprintf(out, out_len, "%s", base);
   char copy[USER_PRIV_LEN + 1];
   strlcpy(copy, extra, sizeof(copy));
   char *save = NULL;

   for (char *token = strtok_r(copy, ",", &save) ; token ; token = strtok_r(NULL, ",", &save)) {
      if (user_privilege_contains(out, token)) {
         continue;
      }
      size_t used = strlen(out);
      int written = snprintf(out + used, out_len - used, "%s%s", used ? "," : "", token);

      if (written < 0 || (size_t)written >= out_len - used) {
         return false;
      }
   }

   return true;
}

/* Build a bounded privilege list excluding requested tokens. */
static bool user_privilege_remove_tokens(const char *base, const char *remove, char *out, size_t out_len) {
   if (!base || !remove || !out || out_len == 0) {
      return false;
   }
   out[0] = '\0';
   char base_copy[USER_PRIV_LEN + 1];
   char remove_copy[USER_PRIV_LEN + 1];
   strlcpy(base_copy, base, sizeof(base_copy));
   strlcpy(remove_copy, remove, sizeof(remove_copy));
   char *save = NULL;

   for (char *token = strtok_r(base_copy, ",", &save) ; token ; token = strtok_r(NULL, ",", &save)) {
      if (user_privilege_contains(remove_copy, token)) {
         continue;
      }
      size_t used = strlen(out);
      int written = snprintf(out + used, out_len - used, "%s%s", used ? "," : "", token);

      if (written < 0 || (size_t)written >= out_len - used) {
         return false;
      }
   }

   return true;
}

/* Identify accounts protected from ordinary administrator edits. */
static bool user_is_elevated(const http_user_t *user) {
   return user && rrserver_user_privilege_has_elevated(user->privs);
}

/* Check actor/target hierarchy for account or password changes. */
bool rrserver_user_target_allowed(const http_user_t *actor, const http_user_t *target, bool password_change) {
   if (!actor || !target) {
      return false;
   }

   if (has_priv(actor->uid, "owner")) {
      return true;
   }

   if (user_is_elevated(target)) {
      return password_change && actor->uid == target->uid;
   }

   return true;
}

/* Generate the existing eight-digit temporary password using arc4random. */
static bool user_temp_password(char *password, size_t length) {
   if (!password || length < 9) {
      return false;
   }
   unsigned value = arc4random_uniform(100000000U);

   return snprintf(password, length, "%08u", value) > 0;
}

/* Reload user state after a write and report failures to the requester. */
bool rrserver_user_reload_database(rrconn_t *cptr) {
   if (db_get_users(masterdb) < 0) {
      ws_send_error(cptr, "USER: account database reload failed");

      return false;
   }

   return true;
}

/* Disconnect active local sessions belonging to a changed account. */
static void user_disconnect_sessions(const http_user_t *target, const char *reason) {
   if (!target) {
      return;
   }

   for (rrconn_t *cur = http_client_list ; cur ; ) {
      rrconn_t *next = cur->next;

      if (cur->user == target) {
         ws_kick_client(cur, reason);
      }
      cur = next;
   }
}

#endif /* USE_SQLITE */

/* Parse account commands and enforce actor/target privilege boundaries. */
static void rrserver_handle_user_cmd(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;
#ifndef USE_SQLITE
   (void)data;

   if (cptr) {
      ws_send_error(cptr, "USER: account management requires SQLite support");
   }

   return;
#else

   if (!cptr || !cptr->user || !data) {
      return;
   }

   if (!masterdb) {
      ws_send_error(cptr, "USER: account database is not open");

      return;
   }

   /* user.cmd is emitted with event_emit_dict(), so data is the serialized event envelope.  Parse that envelope before splitting the command tail;
    * tokenizing the JSON itself made every subcommand look like a missing or unknown account (for example `/user list` became an empty target). */
   dict *request = json2dict(data);

   if (!request) {
      ws_send_error(cptr, "USER: invalid command payload");

      return;
   }
   const char *command_data = dict_get(request, "user.data", NULL);

   if (!command_data) {
      command_data = "";
   }

   char input[HTTP_WS_MAX_MSG + 1];
   snprintf(input, sizeof(input), "%s", command_data);
   dict_free(request);
   char *argv[8] = {
      0
   };
   int argc = 0;
   char *save = NULL;

   for (char *token = strtok_r(input, " \t\r\n", &save) ; token && argc < (int)(sizeof(argv) / sizeof(argv[0])) ; token = strtok_r(NULL, " \t\r\n", &save)) {
      argv[argc++] = token;
   }

   bool self_password = argc == 3 && !strcasecmp(argv[0], "pass") && !strcasecmp(argv[1], cptr->user->name);

   if (!has_priv(cptr->user->uid, "admin|owner") && !self_password) {
      ws_send_error(cptr, "Account administration requires admin or owner; use /user pass <your-user> <password> for your own password");

      return;
   }

   if (argc == 0 || strcasecmp(argv[0], "help") == 0) {
      ws_send_notice(cptr,
         "Usage: /user list | add <user> [privileges] | remove <user> | lock <user> | unlock <user> | privs <user> list|add|remove|set [privileges] | "
         "oldpw | resetpw <user> | pass <user> <password>");

      return;
   }

   if (strcasecmp(argv[0], "list") == 0) {
      if (argc != 1) {
         ws_send_notice(cptr, "Usage: /user list");

         return;
      }
      ws_send_notice(cptr, "Users:");

      for (int i = 0 ; i < HTTP_MAX_USERS ; i++) {
         http_user_t *entry = &http_users[i];

         if (!entry->name[0]) {
            continue;
         }
         char expiry[32] = "never";

         if (entry->password_expires > 0) {
            struct tm tm_value;
            localtime_r(&entry->password_expires, &tm_value);
            strftime(expiry, sizeof(expiry), "%Y-%m-%d", &tm_value);
         }
         ws_send_notice(cptr, "  %-16s %-7s sessions=%d privs=%s password-expires=%s%s", entry->name, entry->enabled ? "enabled" : "locked", entry->sessions,
            entry->privs[0] ? entry->privs : "none", expiry, entry->password_change_required ? " (change required)" : "");
      }

      return;
   }

   if (strcasecmp(argv[0], "oldpw") == 0) {
      if (argc != 1) {
         ws_send_notice(cptr, "Usage: /user oldpw");

         return;
      }
      int configured_age = cfg_get_int("security.max-pw-age", 90);
      unsigned max_age = configured_age > 0 ? (unsigned)configured_age : 0;

      if (max_age == 0) {
         ws_send_notice(cptr, "USER: password age reporting is disabled");

         return;
      }
      time_t cutoff = now - (time_t) max_age * 86400;
      int found = 0;

      for (int i = 0 ; i < HTTP_MAX_USERS ; i++) {
         http_user_t *entry = &http_users[i];

         if (!entry->name[0] || entry->password_set <= 0 || entry->password_set > cutoff) {
            continue;
         }
         int days = (int)((now - entry->password_set) / 86400);
         ws_send_notice(cptr, "  %-16s password set %d days ago%s", entry->name, days, entry->password_change_required ? " (change required)" : "");
         found++;
      }

      if (!found) {
         ws_send_notice(cptr, "USER: no passwords are older than %u days", max_age);
      }

      return;
   }

   if (strcasecmp(argv[0], "add") == 0) {
      if (argc < 2 || argc > 3 || !user_name_valid(argv[1])) {
         ws_send_notice(cptr, "Usage: /user add <user> [privileges]");

         return;
      }

      if (http_getuid(argv[1]) >= 0) {
         ws_send_notice(cptr, "USER: account already exists: %s", argv[1]);

         return;
      }
      const char *privileges = argc == 3 ? argv[2] : "view,chat";

      if (strlen(privileges) > USER_PRIV_LEN || (!has_priv(cptr->user->uid, "owner") && rrserver_user_privilege_has_elevated(privileges))) {
         ws_send_notice(cptr, "USER: administrators cannot create owner or administrator accounts");

         return;
      }
      int uid = db_user_next_uid(masterdb);

      if (uid < 1 || uid >= HTTP_MAX_USERS) {
         ws_send_notice(cptr, "USER: no user slots are available");

         return;
      }
      char password[9];
      char *password_hash = NULL;
      bool ok = user_temp_password(password, sizeof(password));

      if (ok) {
         password_hash = hash_passwd(password);
      }
      ok = ok && password_hash && db_user_create(masterdb, uid, argv[1], true, password_hash, "no@example.com", 1, privileges, true, now + 7 * 86400);
      free(password_hash);

      if (!ok) {
         ws_send_notice(cptr, "USER: failed to add %s", argv[1]);

         return;
      }
      Log(LOG_AUDIT, "auth.users", "%s added user %s with privileges %s", cptr->chatname, argv[1], privileges);
      ws_send_notice(cptr, "USER: added %s with temporary password %s (password change required at next login)", argv[1], password);

      rrserver_user_reload_database(cptr);

      return;
   }

   const char *target_name = argc > 1 ? argv[1] : NULL;
   int target_uid = target_name ? http_getuid(target_name) : -1;
   http_user_t *target = target_uid >= 0 ? &http_users[target_uid] : NULL;
   bool password_change = strcasecmp(argv[0], "pass") == 0 || strcasecmp(argv[0], "resetpw") == 0;

   if (!target_name || !target || !target->name[0]) {
      ws_send_notice(cptr, "USER: account not found: %s", target_name ? target_name : "(missing)");

      return;
   }

   if (strcasecmp(argv[0], "privs") == 0) {
      if (argc < 3 || argc > 4 || (strcasecmp(argv[2], "list") != 0 && argc != 4) || (argc == 4 && strcasecmp(argv[2], "list") == 0)) {
         ws_send_notice(cptr, "Usage: /user privs <user> list|add|remove|set [privileges]");

         return;
      }

      bool actor_is_owner = has_priv(cptr->user->uid, "owner");

      if (strcasecmp(argv[2], "list") == 0) {
         ws_send_notice(cptr, "USER: %s privileges: %s", target->name, target->privs[0] ? target->privs : "none");

         return;
      }

      if (user_is_elevated(target) && !actor_is_owner) {
         ws_send_notice(cptr, "USER: only owners may change protected privileges (owner, admin, bridge.admin, websdr.connect, websdr.admin)");

         return;
      }

      const char *requested = argv[3];

      if (!rrserver_user_privilege_list_valid(requested, false) || (strcasecmp(requested, "none") == 0 && strcasecmp(argv[2], "set") != 0)) {
         ws_send_notice(cptr, "USER: invalid privilege list: %s", requested ? requested : "(missing)");

         return;
      }

      char updated[USER_PRIV_LEN + 1];

      if (strcasecmp(argv[2], "set") == 0) {
         if (strcasecmp(requested, "none") == 0) {
            updated[0] = '\0';
         } else {
            strlcpy(updated, requested, sizeof(updated));
         }
      } else if (strcasecmp(argv[2], "add") == 0) {
         if (!user_privilege_add_tokens(target->privs, requested, updated, sizeof(updated))) {
            ws_send_notice(cptr, "USER: resulting privilege list is too long");

            return;
         }
      } else if (strcasecmp(argv[2], "remove") == 0) {
         if (!user_privilege_remove_tokens(target->privs, requested, updated, sizeof(updated))) {
            ws_send_notice(cptr, "USER: resulting privilege list is too long");

            return;
         }
      } else {
         ws_send_notice(cptr, "Usage: /user privs <user> list|add|remove|set [privileges]");

         return;
      }

      if (!actor_is_owner && rrserver_user_privilege_has_elevated(updated)) {
         ws_send_notice(cptr, "USER: only owners may grant protected privileges (owner, admin, bridge.admin, websdr.connect, websdr.admin)");

         return;
      }

      bool ok = db_user_set_privileges(masterdb, target->name, updated);

      if (ok) {
         Log(LOG_AUDIT, "auth.users", "%s changed privileges for %s to %s", cptr->chatname, target->name, updated[0] ? updated : "none");
      }

      if (!ok || !rrserver_user_reload_database(cptr)) {
         ws_send_notice(cptr, "USER: failed to update privileges for %s", target->name);

         return;
      }
      ws_send_notice(cptr, "USER: %s privileges for %s: %s", argv[2], target->name, updated[0] ? updated : "none");

      return;
   }

   if (!rrserver_user_target_allowed(cptr->user, target, password_change)) {
      ws_send_notice(cptr, "USER: insufficient privilege to modify %s", target->name);

      return;
   }

   if (strcasecmp(argv[0], "lock") == 0 || strcasecmp(argv[0], "unlock") == 0) {
      if (argc != 2 || (user_is_elevated(target) && !has_priv(cptr->user->uid, "owner"))) {
         ws_send_notice(cptr, "USER: admins cannot lock owners or administrators");

         return;
      }

      if (target == cptr->user) {
         ws_send_notice(cptr, "USER: you cannot lock or unlock your own account");

         return;
      }
      bool enabled = strcasecmp(argv[0], "unlock") == 0;

      bool ok = db_user_set_enabled(masterdb, target->name, enabled);

      if (ok) {
         Log(LOG_AUDIT, "auth.users", "%s %s user %s", cptr->chatname, enabled ? "unlocked" : "locked", target->name);
      }

      if (!ok || !rrserver_user_reload_database(cptr)) {
         ws_send_notice(cptr, "USER: failed to %s %s", enabled ? "unlock" : "lock", target->name);

         return;
      }
      ws_send_notice(cptr, "USER: %s %s", enabled ? "Unlocked" : "Locked", target->name);

      return;
   }

   if (strcasecmp(argv[0], "remove") == 0) {
      if (argc != 2 || (user_is_elevated(target) && !has_priv(cptr->user->uid, "owner"))) {
         ws_send_notice(cptr, "USER: admins cannot remove owners or administrators");

         return;
      }

      if (target == cptr->user) {
         ws_send_notice(cptr, "USER: you cannot remove your own account");

         return;
      }
      char removed_name[HTTP_USER_LEN + 1];
      strlcpy(removed_name, target->name, sizeof(removed_name));

      if (!db_user_remove(masterdb, removed_name)) {
         ws_send_notice(cptr, "USER: failed to remove %s", removed_name);

         return;
      }
      Log(LOG_AUDIT, "auth.users", "%s removed user %s", cptr->chatname, removed_name);
      user_disconnect_sessions(target, "Your account was removed by an administrator");

      if (!rrserver_user_reload_database(cptr)) {
         ws_send_notice(cptr, "USER: removed %s, but account reload failed", removed_name);

         return;
      }
      ws_send_notice(cptr, "USER: removed %s", removed_name);

      return;
   }

   if (strcasecmp(argv[0], "resetpw") == 0) {
      if (argc != 2) {
         ws_send_notice(cptr, "Usage: /user resetpw <user>");

         return;
      }
      char password[9];

      if (!user_temp_password(password, sizeof(password))) {
         ws_send_notice(cptr, "USER: unable to generate a temporary password");

         return;
      }
      char *password_hash = hash_passwd(password);
      bool ok = password_hash && db_user_update_password(masterdb, target->name, password_hash, true, now + 7 * 86400);
      free(password_hash);

      if (!ok) {
         ws_send_notice(cptr, "USER: failed to reset password for %s", target->name);

         return;
      }
      Log(LOG_AUDIT, "auth.users", "%s reset password for %s", cptr->chatname, target->name);
      ws_send_notice(cptr, "USER: temporary password for %s: %s (password change required at next login)", target->name, password);

      rrserver_user_reload_database(cptr);

      return;
   }

   if (strcasecmp(argv[0], "pass") == 0) {
      if (argc != 3 || !argv[2] || strlen(argv[2]) < 8 || strlen(argv[2]) > 128) {
         ws_send_notice(cptr, "Usage: /user pass <user> <password> (8-128 characters)");

         return;
      }
      char *password_hash = hash_passwd(argv[2]);
      bool ok = password_hash && db_user_update_password(masterdb, target->name, password_hash, false, 0);
      free(password_hash);

      if (!ok) {
         ws_send_notice(cptr, "USER: failed to change password for %s", target->name);

         return;
      }
      Log(LOG_AUDIT, "auth.users", "%s changed password for %s", cptr->chatname, target->name);
      ws_send_notice(cptr, "USER: password changed for %s", target->name);

      rrserver_user_reload_database(cptr);

      return;
   }

   ws_send_notice(cptr, "USER: unknown subcommand %s", argv[0]);
#endif
}

/* Register this domain's server-owned protocol listeners. */
void rrserver_events_users_register(void) {
   event_on("user.cmd", rrserver_handle_user_cmd, NULL);
}
