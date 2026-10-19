//
// rrserver/events.admin.c: admin event listeners from
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
#include <glib.h>
#include <errno.h>

#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/ws.mediachan.h>
#include <rrserver/database.h>
#include <rrserver/serial.h>
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
#include <rrserver/platform.controls.h>
#include <rrserver/objects.h>
#include <rrserver/usage.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <libfwdspmgr/fwdsp-ctl.h>

extern struct GlobalState rig;

#include <rrserver/events.internal.h>

/* PARITY: rrclient/gtk/gtk.admin.c and www/js/webui.admin.js admin results. SQLite and access control stay in the server; no credential fields leave it. */
#ifdef USE_SQLITE
/* Send one structured administration result without credential fields. */
bool rrserver_admin_send(rrconn_t *client, const char *table, const char *action, dict *row) {
   dict *reply = dict_new();

   if (!reply) {
      return false;
   }
   char *payload = row ? dict2json(row) : NULL;
   dict_add(reply, "msg.type", "talk");
   dict_add(reply, "talk.cmd", "admin");
   dict_add(reply, "talk.target", table);
   dict_add(reply, "talk.action", action);
   dict_add(reply, "talk.data", payload ? payload : "{}");
   bool sent = ws_send_dict(NULL, client, reply, WEBSOCKET_OP_TEXT);
   free(payload);
   dict_free(reply);

   return sent;
}

/* Route editor failures back to its table, rather than only the chat error stream. */
static void admin_user_error(rrconn_t *client, const char *message) {
   dict *row = dict_new();
   if (row) {
      dict_add(row, "message", message);
      rrserver_admin_send(client, "users", "error", row);
      dict_free(row);
   }
   ws_send_error(client, "%s", message);
}

/* Partial account edits are atomic. Account identity and credential hashes are never editable here. */
static void admin_user_edit(rrconn_t *client, dict *request) {
   const char *name = dict_get(request, "talk.target", "");
   int uid = http_getuid(name);
   dict *fields = json2dict(dict_get(request, "talk.data", "{}"));

   if (!fields || uid < 0) {
      admin_user_error(client, "Account unavailable or invalid editor request");
      dict_free(fields);
      return;
   }
   if (!rrserver_user_target_allowed(client->user, &http_users[uid], false)) {
      admin_user_error(client, "Only owners may edit protected accounts (including admin accounts)");
      dict_free(fields);
      return;
   }
   const char *email = dict_get(fields, "email", http_users[uid].email);
   const char *privs = dict_get(fields, "permissions", http_users[uid].privs);
   const char *enabled = dict_get(fields, "enabled", http_users[uid].enabled ? "1" : "0");
   const char *required = dict_get(fields, "password_change_required", http_users[uid].password_change_required ? "1" : "0");
   char previous_sessions[32], previous_expiry[32];
   snprintf(previous_sessions, sizeof(previous_sessions), "%d", http_users[uid].max_sessions);
   snprintf(previous_expiry, sizeof(previous_expiry), "%lld", (long long)http_users[uid].password_expires);
   const char *sessions = dict_get(fields, "maxsessions", previous_sessions);
   const char *expiry = dict_get(fields, "password_expires", previous_expiry);
   if (dict_get_type(fields, "permissions") != VAL_END &&
       !has_priv(client->user->uid, "owner") && rrserver_user_privilege_has_elevated(privs)) {
      admin_user_error(client, "Only owners may grant protected privileges: owner, admin, bridge.admin, websdr.connect, websdr.admin");
      dict_free(fields);
      return;
   }
   char *end;
   errno = 0;
   long maximum = strtol(sessions, &end, 10);
   bool valid = *sessions && !*end && !errno && maximum >= 1 && maximum <= HTTP_MAX_SESSIONS;
   errno = 0;
   long long expires = *expiry ? strtoll(expiry, &end, 10) : 0;
   valid = valid && (!*expiry || (!*end && !errno && expires >= 0));
   valid = valid && strlen(email) <= USER_EMAIL_LEN && !strchr(email, '\n') && !strchr(email, '\r') &&
      (!strcmp(enabled, "0") || !strcmp(enabled, "1")) && (!strcmp(required, "0") || !strcmp(required, "1")) &&
      rrserver_user_privilege_list_valid(privs, true);

   if (uid == client->user->uid && !strcmp(enabled, "0")) {
      valid = false;
   }

   if (!valid) {
      admin_user_error(client, "Invalid account fields or attempted self-lock (sessions must be 1..32; expiry is Unix seconds)");
      dict_free(fields);

      return;
   }
   bool ok = db_user_edit(masterdb, name, fields);

   if (ok) {
      const char *editable[] = {"email", "permissions", "enabled", "maxsessions", "password_change_required", "password_expires"};
      char changed[256] = "";
      for (unsigned i = 0; i < sizeof(editable) / sizeof(editable[0]); i++) {
         if (dict_get_type(fields, editable[i]) == VAL_END) continue;
         if (*changed) strlcat(changed, ",", sizeof(changed));
         strlcat(changed, editable[i], sizeof(changed));
      }
      if (*changed) Log(LOG_AUDIT, "auth.users", "%s edited account %s fields %s", client->chatname, name, changed);

      if (rrserver_user_reload_database(client)) {
         rrserver_admin_send(client, "users", "changed", NULL);
      } else {
         admin_user_error(client, "Account saved to SQLite, but reloading active account state failed; refresh before retrying");
      }
   } else {
      admin_user_error(client, "Unable to save account to SQLite; see the server database log");
   }
   dict_free(fields);
}

#endif

/* Session IDs are non-secret fingerprints; never send bearer login tokens. */
static char *admin_session_id(const rrconn_t *session) {
   char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256,
      session->token, -1);
   if (digest) digest[20] = '\0';
   return digest;
}
static bool admin_can_kick(const rrconn_t *actor, const rrconn_t *target) {
   if (!target || !target->authenticated || !target->is_ws || target == actor)
      return false;
   if (!target->user) return false;
   /* Admins cannot remove protected accounts. Even owners cannot kick
    * another owner through this control. */
   if (has_priv(target->user->uid, "owner")) return false;
   return has_priv(actor->user->uid, "owner") ||
      !has_priv(target->user->uid, "admin");
}
static void admin_sessions_list(rrconn_t *client) {
   rrserver_admin_send(client, "sessions", "begin", NULL);
   for (rrconn_t *session = http_client_list; session; session = session->next) {
      if (!session->is_ws || !session->authenticated) continue;
      char *id = admin_session_id(session);
      dict *row = dict_new();
      if (!row || !id) { dict_free(row); g_free(id); continue; }
      char connected_at[32];
      snprintf(connected_at, sizeof(connected_at), "%lld",
         (long long)session->connected);
      dict_add(row, "id", id);
      dict_add(row, "username", session->chatname);
      dict_add(row, "ip", session->user_ip);
      dict_add(row, "client", session->user_agent ? session->user_agent : "Unknown");
      dict_add(row, "server", server_name ? server_name : "Local");
      char exports[256];
      rrserver_serial_client_exports(session, exports, sizeof(exports));
      dict_add(row, "serial_ports", exports);
      dict_add(row, "connected", connected_at);
      dict_add(row, "kickable", admin_can_kick(client, session) ? "yes" : "no");
      rrserver_admin_send(client, "sessions", "row", row);
      dict_free(row);
      g_free(id);
   }
   rrserver_admin_send(client, "sessions", "end", NULL);
}
static void admin_sessions_kick(rrconn_t *client, const char *target,
   bool all) {
   unsigned kicked = 0;
   /* Save the next link before disconnecting the current client. */
   for (rrconn_t *session = http_client_list; session;) {
      rrconn_t *next = session->next;
      if (admin_can_kick(client, session)) {
         char *id = admin_session_id(session);
         bool matches = all || (id && target && !strcmp(id, target));
         g_free(id);
         if (matches && ws_kick_client(session, "Session disconnected by administrator"))
            kicked++;
      }
      session = next;
   }
   Log(LOG_AUDIT, "admin.sessions", "%s disconnected %u session(s)",
      client->chatname, kicked);
   admin_sessions_list(client);
}

/* Owner-only management of reusable scoped policies.
 * Arguments are individually bound; remote subjects do not require accounts. */
static bool admin_privset_action(rrconn_t *client, const char *action, dict *request) {
   if (strncmp(action, "privset-", 8)) return false;
   if (!has_priv(client->user->uid, "owner")) {
      ws_send_error(client, "Only owners may manage resource privilege sets");
      return true;
   }
   const char *name = dict_get(request, "talk.target", "");
   const char *data = dict_get(request, "talk.data", "");
   if (!*name || strlen(name) > 96 || strlen(data) > 512) {
      ws_send_error(client, "Invalid privilege set name or arguments");
      return true;
   }
   const char *sql = NULL;
   char a[130] = "", b[130] = "", d[130] = "", extra[2] = "";
   int args = sscanf(data, "%129s %129s %129s %1s", a,b,d,extra);
   if (!strcmp(action, "privset-create")) {
      sql = "INSERT INTO privilege_sets(name,description,created_by) VALUES(?1,?2,?3)";
   } else if (!strcmp(action, "privset-delete")) {
      if (*data) { ws_send_error(client, "privset-delete takes only a set name"); return true; }
      sql = "DELETE FROM privilege_sets WHERE name=?1";
   } else if (!strcmp(action, "privset-entry")) {
      if (args != 2 || (strcmp(a,"allow") && strcmp(a,"deny")) ||
          strlen(b) > 128) {
         ws_send_error(client, "Usage: /admin privset-entry SET allow|deny PRIVILEGE");
         return true;
      }
      sql = "INSERT INTO privilege_set_entries(set_id,effect,privilege) "
         "SELECT id,?2,?3 FROM privilege_sets WHERE name=?1 "
         "ON CONFLICT(set_id,privilege) DO UPDATE SET effect=excluded.effect";
   } else if (!strcmp(action, "privset-unentry")) {
      if (args != 1) { ws_send_error(client, "Usage: /admin privset-unentry SET PRIVILEGE"); return true; }
      sql = "DELETE FROM privilege_set_entries WHERE set_id="
         "(SELECT id FROM privilege_sets WHERE name=?1) AND privilege=?2";
   } else if (!strcmp(action, "privset-assign") || !strcmp(action, "privset-unassign")) {
      if (args != 3 ||
          !( !strcmp(b,"site") || !strcmp(b,"rig") || !strcmp(b,"vfo") ||
             !strcmp(b,"room") || !strcmp(b,"media") || !strcmp(b,"serial") ||
             !strcmp(b,"file") || !strcmp(b,"bridge")) ||
          !( !strcmp(b,"rig") && !strcmp(d,"*")) &&
          (strlen(d) != 36 || !rr_object_uuid_valid(d))) {
         ws_send_error(client, "Usage: /admin privset-assign SET user@host KIND RESOURCE-UUID (or rig *)");
         return true;
      }
      sql = !strcmp(action,"privset-assign")
         ? "INSERT INTO privilege_set_assignments "
           "(set_id,subject_user,subject_host,resource_kind,resource_uuid,assigned_by) "
           "SELECT id,?2,?3,?4,?5,?6 FROM privilege_sets WHERE name=?1 "
           "ON CONFLICT(set_id,subject_user,subject_host,resource_kind,resource_uuid) "
           "DO UPDATE SET assigned_by=excluded.assigned_by,assigned_at=unixepoch(),expires_at=NULL"
         : "DELETE FROM privilege_set_assignments WHERE set_id="
           "(SELECT id FROM privilege_sets WHERE name=?1) AND subject_user=?2 "
           "COLLATE NOCASE AND subject_host=?3 COLLATE NOCASE "
           "AND resource_kind=?4 AND resource_uuid=?5";
   } else {
      ws_send_error(client, "Unknown privilege set operation");
      return true;
   }
   char username[130] = "", host[130] = "";
   if (strstr(action, "assign")) {
      char *at = strchr(a, '@');
      if (at) {
         *at++ = 0;
         if (!*at || strlen(at) >= sizeof(host)) {
            ws_send_error(client, "Invalid remote site");
            return true;
         }
         snprintf(host, sizeof(host), "%s", at);
      }
      if (!*a || strlen(a) > HTTP_USER_LEN) {
         ws_send_error(client, "Invalid subject username");
         return true;
      }
      snprintf(username, sizeof(username), "%s", a);
   }
   sqlite3_stmt *st = NULL;
   int rc = sqlite3_prepare_v2(masterdb, sql, -1, &st, NULL);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) {
      if (!strcmp(action,"privset-create")) {
         rc = sqlite3_bind_text(st,2,data,-1,SQLITE_TRANSIENT);
         if (rc == SQLITE_OK) rc = sqlite3_bind_text(st,3,client->chatname,-1,SQLITE_TRANSIENT);
      } else if (!strcmp(action,"privset-entry")) {
         rc = sqlite3_bind_text(st,2,a,-1,SQLITE_TRANSIENT);
         if (rc == SQLITE_OK) rc = sqlite3_bind_text(st,3,b,-1,SQLITE_TRANSIENT);
      } else if (!strcmp(action,"privset-unentry")) {
         rc = sqlite3_bind_text(st,2,a,-1,SQLITE_TRANSIENT);
      } else if (strstr(action,"assign")) {
         rc = sqlite3_bind_text(st,2,username,-1,SQLITE_TRANSIENT);
         if (rc == SQLITE_OK) rc = sqlite3_bind_text(st,3,host,-1,SQLITE_TRANSIENT);
         if (rc == SQLITE_OK) rc = sqlite3_bind_text(st,4,b,-1,SQLITE_TRANSIENT);
         if (rc == SQLITE_OK) rc = sqlite3_bind_text(st,5,d,-1,SQLITE_TRANSIENT);
         if (rc == SQLITE_OK && !strcmp(action,"privset-assign"))
            rc = sqlite3_bind_text(st,6,client->chatname,-1,SQLITE_TRANSIENT);
      }
   }
   if (rc == SQLITE_OK) rc = sqlite3_step(st);
   bool ok = rc == SQLITE_DONE && sqlite3_changes(masterdb) > 0;
   if (st) sqlite3_finalize(st);
   if (!ok) {
      Log(LOG_WARN,"privset","%s for %s failed: %s",action,name,sqlite3_errmsg(masterdb));
      ws_send_error(client, "Privilege set operation failed; check name, subject, or duplicates");
   } else {
      db_add_audit_event(masterdb,client->chatname,action,name);
      dict *row = dict_new();
      if (row) {
         dict_add(row,"name",name);
         dict_add(row,"operation",action);
         rrserver_admin_send(client,"privsets","updated",row);
         dict_free(row);
      }
   }
   return true;
}

/* Direct resource grants complement reusable set assignments. Both resolve
 * the same authoritative subject, and explicit deny takes precedence. */
static bool admin_resource_action(rrconn_t *client, const char *action, dict *request) {
   if (strcmp(action, "resource-grant") && strcmp(action, "resource-revoke")) return false;
   if (!has_priv(client->user->uid, "owner")) {
      ws_send_error(client, "Only owners may edit scoped resource grants");
      return true;
   }
   const char *subject = dict_get(request, "talk.target", "");
   const char *data = dict_get(request, "talk.data", "");
   char username[HTTP_USER_LEN + 1] = "", host[128] = "";
   char kind[16] = "", uuid[64] = "", privilege[129] = "", effect[16] = "", extra[2] = "";
   if (!subject || !*subject || strlen(subject) > 256) {
      ws_send_error(client, "Resource grants require user@host subject");
      return true;
   }
   const char *at = strchr(subject, '@');
   size_t userlen = at ? (size_t)(at - subject) : strlen(subject);
   if (!userlen || userlen > HTTP_USER_LEN || (at && (!at[1] ||
       strchr(at + 1, '@') || strlen(at + 1) >= sizeof(host)))) {
      ws_send_error(client, "Invalid subject; use user or user@host");
      return true;
   }
   memcpy(username, subject, userlen);
   if (at) snprintf(host, sizeof(host), "%s", at + 1);
   int n = sscanf(data, "%15s %63s %128s %15s %1s",
      kind,uuid,privilege,effect,extra);
   bool granting = !strcmp(action, "resource-grant");
   if (n != (granting ? 4 : 3) ||
       !( !strcmp(kind,"site") || !strcmp(kind,"rig") || !strcmp(kind,"vfo") ||
          !strcmp(kind,"room") || !strcmp(kind,"media") || !strcmp(kind,"serial") ||
          !strcmp(kind,"file") || !strcmp(kind,"bridge")) ||
       !rr_object_uuid_valid(uuid) ||
       (granting && strcmp(effect,"allow") && strcmp(effect,"deny"))) {
      ws_send_error(client, "Usage: /admin resource-grant USER@HOST KIND UUID PRIVILEGE allow|deny");
      return true;
   }
   const char *sql = granting
      ? "INSERT INTO resource_privileges "
        "(subject_user,subject_host,resource_kind,resource_uuid,privilege,effect,granted_by) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7) "
        "ON CONFLICT(subject_user,subject_host,resource_kind,resource_uuid,privilege) "
        "DO UPDATE SET effect=excluded.effect,granted_by=excluded.granted_by,"
        "granted_at=unixepoch(),expires_at=NULL"
      : "DELETE FROM resource_privileges WHERE subject_user=?1 COLLATE NOCASE "
        "AND subject_host=?2 COLLATE NOCASE AND resource_kind=?3 "
        "AND resource_uuid=?4 AND privilege=?5";
   sqlite3_stmt *st = NULL;
   int rc = sqlite3_prepare_v2(masterdb,sql,-1,&st,NULL);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(st,1,username,-1,SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(st,2,host,-1,SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(st,3,kind,-1,SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(st,4,uuid,-1,SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(st,5,privilege,-1,SQLITE_TRANSIENT);
   if (rc == SQLITE_OK && granting) rc = sqlite3_bind_text(st,6,effect,-1,SQLITE_TRANSIENT);
   if (rc == SQLITE_OK && granting)
      rc = sqlite3_bind_text(st,7,client->chatname,-1,SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) rc = sqlite3_step(st);
   bool ok = rc == SQLITE_DONE && sqlite3_changes(masterdb) == 1;
   if (st) sqlite3_finalize(st);
   if (!ok) {
      Log(LOG_WARN,"privset","%s %s failed: %s",action,subject,sqlite3_errmsg(masterdb));
      ws_send_error(client, "Unable to update resource grant");
      return true;
   }
   db_add_audit_event(masterdb,client->chatname,action,subject);
   dict *row = dict_new();
   if (row) {
      dict_add(row,"subject",subject);
      dict_add(row,"resource",uuid);
      dict_add(row,"privilege",privilege);
      rrserver_admin_send(client,"grants","updated",row);
      dict_free(row);
   }
   return true;
}

static void admin_control_reply(rrconn_t *client, const rr_platform_control_state_t *s,
      const char *action) {
   dict *row = dict_new();
   if (!row) return;
   char number[32];
   dict_add(row,"name",s->name);
   dict_add(row,"label",s->label);
   dict_add(row,"unit",s->unit);
   dict_add(row,"privilege",s->privilege);
   snprintf(number,sizeof(number),"%d",s->current); dict_add(row,"current",number);
   snprintf(number,sizeof(number),"%d",s->minimum); dict_add(row,"minimum",number);
   snprintf(number,sizeof(number),"%d",s->maximum); dict_add(row,"maximum",number);
   snprintf(number,sizeof(number),"%d",s->step); dict_add(row,"step",number);
   dict_add_bool(row,"writable",s->writable);
   rrserver_admin_send(client,"controls",action,row);
   dict_free(row);
}
static bool admin_control_visit(const rr_platform_control_state_t *s, void *user) {
   admin_control_reply((rrconn_t *)user,s,"row");
   return false;
}

/* Handle account/room actions or paginate an allowlisted database view. */
static void rrserver_handle_admin_cmd(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)user;

   if (!client || !client->authenticated || !client->user || !has_priv(client->user->uid, "admin|owner")) {
      return;
   }
#ifdef USE_SQLITE
   dict *request = data ? json2dict(data) : NULL;

   if (!request) {
      return;
   }
   const char *action = dict_get(request, "talk.action", "");
   /* Node controls are exported through one generic, typed namespace.
    * The old /admin backlight spelling remains an alias for clients that
    * have not yet switched to /admin control display.brightness. */
   const char *admin_data = dict_get(request, "talk.data", "");
   if (!strcmp(admin_data,"controls")) {
      rrserver_admin_send(client,"controls","begin",NULL);
      rrserver_platform_control_foreach(admin_control_visit,client);
      rrserver_admin_send(client,"controls","end",NULL);
      dict_free(request);
      return;
   }
   if (!strncmp(admin_data,"control ",8)) {
      char name[64], extra[2];
      int requested = -1;
      int count = sscanf(admin_data + 8,"%63s %d %1s",name,&requested,extra);
      if ((count != 1 && count != 2) || (count == 2 && requested < 0)) {
         ws_send_error(client,"Usage: /admin control NAME [nonnegative-integer]");
         dict_free(request);
         return;
      }
      rr_platform_control_state_t state;
      bool ok = count == 1
         ? rrserver_platform_control_get(name,&state)
         : rrserver_platform_control_set(name,requested,&state);
      if (!ok) {
         ws_send_error(client,"Node control unavailable, read-only, or outside supported range");
      } else {
         if (count == 2) {
            db_add_audit_event(masterdb,client->chatname,"platform.control",name);
            rrserver_objects_platform_changed(&state);
         }
         admin_control_reply(client,&state,"state");
      }
      dict_free(request);
      return;
   }
   if (admin_resource_action(client, action, request)) {
      dict_free(request);
      return;
   }
   if (admin_privset_action(client, action, request)) {
      dict_free(request);
      return;
   }
   if (!strcmp(action, "session-kick") ||
       !strcmp(action, "session-kick-all")) {
      admin_sessions_kick(client, dict_get(request, "talk.target", ""),
         !strcmp(action, "session-kick-all"));
      dict_free(request);
      return;
   }
   if (!strcmp(dict_get(request, "talk.data", ""), "sessions")) {
      admin_sessions_list(client);
      dict_free(request);
      return;
   }

   if (!strcmp(action, "user-edit")) {
      admin_user_edit(client, request);
      dict_free(request);

      return;
   }

   if (!strcmp(action, "room-remove")) {
      const char *room = dict_get(request, "talk.target", "");
      bool exists, deleted;

      if (db_room_status(masterdb, room, &exists, &deleted) && exists) {
         dict *removal = dict_new();

         if (removal) {
            dict_add(removal, "msg.type", "talk");
            dict_add(removal, "talk.cmd", "room-removed");
            dict_add(removal, "talk.user", client->chatname);
            dict_add_ulong(removal, "msg.ts", now);
            dict_add(removal, "talk.room", room);
            dict_add(removal, "talk.action", "admin-remove");
            dict_add_bool(removal, "talk.force", deleted);
            dict_add_bool(removal, "talk.history", deleted);
            const char *token = dict_get(request, "talk.data", "");

            if (*token) {
               dict_add(removal, "talk.confirmation", token);
            }
            char *json = dict2json(removal);
            rrserver_handle_room_delete("room.delete", json, client, NULL);
            free(json);
            dict_free(removal);
         }
      } else {
         ws_send_error(client, "Room is unavailable for removal");
      }
      dict_free(request);

      return;
   }
   char input[256], table[16] = "users", cursor_text[32] = "", extra[2];
   snprintf(input, sizeof(input), "%s", dict_get(request, "talk.data", ""));
   dict_free(request);
   int argc = sscanf(input, "%15s %31s %1s", table, cursor_text, extra);
   char *end = NULL;
   errno = 0;
   long long before = cursor_text[0] ? strtoll(cursor_text, &end, 10) : 0;
   const char *sql = NULL;

   if (!strcasecmp(table, "users")) {
      strcpy(table, "users");
      sql =
         "SELECT rowid AS cursor,uid,name,enabled,permissions,email,maxsessions,password_set,password_expires,password_change_required FROM users WHERE (?=0 OR rowid<?) ORDER BY rowid DESC LIMIT 100";
   } else if (!strcasecmp(table, "quotas")) {
      strcpy(table, "quotas");
      sql =
         "SELECT u.rowid AS cursor,u.name,COALESCE(t.credits,'unset') AS tx_remaining,COALESCE(g.tx_seconds,0) AS tx_used,COALESCE(g.bandwidth_remaining,'unlimited') AS bandwidth_remaining,COALESCE(g.tx_text_bytes+g.tx_binary_bytes,0) AS bandwidth_tx_used,COALESCE(g.rx_text_bytes+g.rx_binary_bytes,0) AS bandwidth_rx_used FROM users u LEFT JOIN tx_credits t ON t.name=u.name COLLATE NOCASE LEFT JOIN user_usage g ON g.name=u.name COLLATE NOCASE WHERE (?=0 OR u.rowid<?) ORDER BY u.rowid DESC LIMIT 100";
   } else if (!strcasecmp(table, "rooms")) {
      strcpy(table, "rooms");
      sql = "SELECT rowid AS cursor,name,uuid,deleted,topic,has_vfos,vfo_mask FROM rooms WHERE (?=0 OR rowid<?) ORDER BY rowid DESC LIMIT 100";
   } else if (!strcasecmp(table, "privsets")) {
      strcpy(table, "privsets");
      sql = "SELECT id AS cursor,name,description,created_by,created_at FROM privilege_sets "
         "WHERE (?=0 OR id<?) ORDER BY id DESC LIMIT 100";
   } else if (!strcasecmp(table, "setentries")) {
      strcpy(table, "setentries");
      sql = "SELECT e.rowid AS cursor,s.name AS set_name,e.privilege,e.effect "
         "FROM privilege_set_entries e JOIN privilege_sets s ON s.id=e.set_id "
         "WHERE (?=0 OR e.rowid<?) ORDER BY e.rowid DESC LIMIT 100";
   } else if (!strcasecmp(table, "assignments")) {
      strcpy(table, "assignments");
      sql = "SELECT a.id AS cursor,s.name AS set_name,a.subject_user,a.subject_host,"
         "a.resource_kind,a.resource_uuid,a.expires_at FROM privilege_set_assignments a "
         "JOIN privilege_sets s ON s.id=a.set_id "
         "WHERE (?=0 OR a.id<?) ORDER BY a.id DESC LIMIT 100";
   } else if (!strcasecmp(table, "grants")) {
      strcpy(table, "grants");
      sql = "SELECT id AS cursor,subject_user,subject_host,resource_kind,"
         "resource_uuid,privilege,effect,expires_at FROM resource_privileges "
         "WHERE (?=0 OR id<?) ORDER BY id DESC LIMIT 100";
   } else if (!strcasecmp(table, "audit")) {
      strcpy(table, "audit");
      sql = "SELECT id AS cursor,id,timestamp,username,event_type,details FROM audit_log WHERE (?=0 OR id<?) ORDER BY id DESC LIMIT 100";
   } else if (!strcasecmp(table, "ptt")) {
      strcpy(table, "ptt");
      sql =
         "SELECT id AS cursor,id,server,start_time,username,vfo,frequency,mode,duration,stop_reason,recording_id,supervision_ticket,supervising_elmers FROM ptt_log WHERE (?=0 OR id<?) ORDER BY id DESC LIMIT 100";
   }

   if (argc > 2 || !sql || errno == ERANGE || before < 0 || (end && (*end || end == cursor_text))) {
      ws_send_error(client, "Usage: /admin users|rooms|quotas|audit|ptt|privsets|setentries|assignments|grants [before-id]");

      return;
   }
   sqlite3_stmt *statement = NULL;

   if (!masterdb || sqlite3_prepare_v2(masterdb, sql, -1, &statement, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "Admin %s listing prepare failed: %s", table, masterdb ? sqlite3_errmsg(masterdb) : "database is not open");
      ws_send_error(client, "Database administration is unavailable; see the server database log");

      return;
   }
   if (sqlite3_bind_int64(statement, 1, before) != SQLITE_OK || sqlite3_bind_int64(statement, 2, before) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "Admin %s listing bind failed: %s", table, sqlite3_errmsg(masterdb));
      sqlite3_finalize(statement);
      ws_send_error(client, "Unable to prepare database listing");
      return;
   }
   dict *metadata = dict_new();

   if (!metadata) {
      sqlite3_finalize(statement);

      return;
   }
   dict_add_bool(metadata, "append", before != 0);

   if (!rrserver_admin_send(client, table, "begin", metadata)) {
      dict_free(metadata);
      sqlite3_finalize(statement);

      return;
   }
   int result, count = 0;
   long long last = 0;
   while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
      dict *row = dict_new();

      if (!row) {
         break;
      }

      for (int col = 0 ; col < sqlite3_column_count(statement) ; col++) {
         const char *value = (const char *)sqlite3_column_text(statement, col);
         dict_add(row, sqlite3_column_name(statement, col), value ? value : "");
      }

      last = sqlite3_column_int64(statement, 0);
      bool sent = rrserver_admin_send(client, table, "row", row);
      dict_free(row);

      if (!sent) {
         break;
      }
      count++;
   }
   if (result != SQLITE_DONE && result != SQLITE_ROW) {
      Log(LOG_CRIT, "db", "Admin %s listing step failed (SQLite %d): %s", table, result, sqlite3_errmsg(masterdb));
      ws_send_error(client, "Unable to finish database listing");
   }
   if (sqlite3_finalize(statement) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "Admin %s listing finalize failed: %s", table, sqlite3_errmsg(masterdb));
   }
   char next[32];
   snprintf(next, sizeof(next), "%lld", count == 100 ? last : 0);
   dict_add(metadata, "next", next);
   rrserver_admin_send(client, table, "end", metadata);
   dict_free(metadata);
#else
   (void)data;
   ws_send_error(client, "Database administration requires SQLite");
#endif
}

/* Register this domain's server-owned protocol listeners. */
void rrserver_events_admin_register(void) {
   event_on("admin.cmd", rrserver_handle_admin_cmd, NULL);
}
