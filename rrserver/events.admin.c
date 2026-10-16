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

/* Full account edits are atomic. Account identity and credential hashes are never editable here. */
static void admin_user_edit(rrconn_t *client, dict *request) {
   const char *name = dict_get(request, "talk.target", "");
   int uid = http_getuid(name);
   dict *fields = json2dict(dict_get(request, "talk.data", "{}"));

   if (!fields || uid < 0 || !rrserver_user_target_allowed(client->user, &http_users[uid], false)) {
      ws_send_error(client, "Account unavailable or protected");
      dict_free(fields);

      return;
   }
   const char *email = dict_get(fields, "email", "");
   const char *privs = dict_get(fields, "permissions", "");
   const char *enabled = dict_get(fields, "enabled", "");
   const char *required = dict_get(fields, "password_change_required", "");
   const char *sessions = dict_get(fields, "maxsessions", "");
   const char *expiry = dict_get(fields, "password_expires", "");
   char *end;
   errno = 0;
   long maximum = strtol(sessions, &end, 10);
   bool valid = *sessions && !*end && !errno && maximum >= 1 && maximum <= HTTP_MAX_SESSIONS;
   errno = 0;
   long long expires = *expiry ? strtoll(expiry, &end, 10) : 0;
   valid = valid && (!*expiry || (!*end && !errno && expires >= 0));
   valid = valid && strlen(email) <= USER_EMAIL_LEN && !strchr(email, '\n') && !strchr(email, '\r') &&
      (!strcmp(enabled, "0") || !strcmp(enabled, "1")) && (!strcmp(required, "0") || !strcmp(required, "1")) &&
      rrserver_user_privilege_list_valid(privs, true) && (has_priv(client->user->uid, "owner") || !rrserver_user_privilege_has_elevated(privs));

   if (uid == client->user->uid && !strcmp(enabled, "0")) {
      valid = false;
   }

   if (!valid) {
      ws_send_error(client, "Invalid account fields, protected privileges, or attempted self-lock (sessions must be 1..32; expiry is Unix seconds)");
      dict_free(fields);

      return;
   }
   sqlite3_stmt *stmt = NULL;
   bool ok = masterdb && sqlite3_prepare_v2(masterdb,
      "UPDATE users SET email=?,permissions=?,enabled=?,maxsessions=?,password_change_required=?,password_expires=? WHERE name=? COLLATE NOCASE", -1, &stmt,
      NULL) == SQLITE_OK;

   if (ok) {
      sqlite3_bind_text(stmt, 1, email, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt, 2, !strcasecmp(privs, "none") ? "" : privs, -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(stmt, 3, atoi(enabled));
      sqlite3_bind_int(stmt, 4, maximum);
      sqlite3_bind_int(stmt, 5, atoi(required));

      if (expires) {
         sqlite3_bind_int64(stmt, 6, expires);
      } else {
         sqlite3_bind_null(stmt, 6);
      }
      sqlite3_bind_text(stmt, 7, name, -1, SQLITE_TRANSIENT);
      ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(masterdb) == 1;
   }
   sqlite3_finalize(stmt);

   if (ok) {
      Log(LOG_AUDIT, "auth.users", "%s edited account %s", client->chatname, name);

      if (rrserver_user_reload_database(client)) {
         rrserver_admin_send(client, "users", "changed", NULL);
      }
   } else {
      ws_send_error(client, "Unable to save account");
   }
   dict_free(fields);
}

#endif

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
      sql = "SELECT rowid AS cursor,name,deleted,topic,has_vfos,vfo_mask FROM rooms WHERE (?=0 OR rowid<?) ORDER BY rowid DESC LIMIT 100";
   } else if (!strcasecmp(table, "audit")) {
      strcpy(table, "audit");
      sql = "SELECT id AS cursor,id,timestamp,username,event_type,details FROM audit_log WHERE (?=0 OR id<?) ORDER BY id DESC LIMIT 100";
   } else if (!strcasecmp(table, "ptt")) {
      strcpy(table, "ptt");
      sql =
         "SELECT id AS cursor,id,server,start_time,username,vfo,frequency,mode,duration,stop_reason,recording_id,supervision_ticket,supervising_elmers FROM ptt_log WHERE (?=0 OR id<?) ORDER BY id DESC LIMIT 100";
   }

   if (argc > 2 || !sql || errno == ERANGE || before < 0 || (end && (*end || end == cursor_text))) {
      ws_send_error(client, "Usage: /admin users|rooms|quotas|audit|ptt [before-id]");

      return;
   }
   sqlite3_stmt *statement = NULL;

   if (!masterdb || sqlite3_prepare_v2(masterdb, sql, -1, &statement, NULL) != SQLITE_OK) {
      ws_send_error(client, "Database administration is unavailable");

      return;
   }
   sqlite3_bind_int64(statement, 1, before);
   sqlite3_bind_int64(statement, 2, before);
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
   sqlite3_finalize(statement);

   if (result != SQLITE_DONE && result != SQLITE_ROW) {
      ws_send_error(client, "Unable to finish database listing");
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
