// Explicit receiver actions use the selected server; directory browsing is passive.
#include <glib.h>
#include <librustyaxe/core.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>
#include <rrclient/receiver.directory.h>
#include <rrclient/receiver.connect.h>
static struct {
   char *profile, *request;
   rrconn_t *connection;
   uint64_t started;
   bool save;
} pending;
static rr_event_token_t result_token, close_token;
static unsigned serial;
static void clear_pending(void) {
   g_free(pending.profile);
   g_free(pending.request);
   memset(&pending, 0, sizeof(pending));
}
static void report(const char *message, rrconn_t *connection) {
   ui_print(NULL, "%s", message);
   event_emit("client.receiver-directory.connection", connection, message);
}
bool rrclient_receiver_connect_room(const char *url, bool save, const char *room) {
   const receiver_directory_entry_t *receiver = receiver_directory_find_url(url);
   rrconn_t *connection = rrclient_active_connection();
   const char *profile = connection ? rrclient_connection_name(connection) : NULL;

   if (!connection || !connection->is_ws || !connection->authenticated) {
      report("Select a connected RustyRig server first.", connection);

      return false;
   }

   if (receiver && !receiver_directory_entry_accessible(receiver)) {
      report("Receiver is restricted, unavailable, or has no published API permission.", connection);

      return false;
   }

   if (pending.request && mono_us() - pending.started < 15000000) {
      report("A receiver request is already pending.", connection);

      return false;
   }
   clear_pending();
   char id[48];
   snprintf(id, sizeof(id), "receiver-%u", ++serial);
   dict *fields = dict_new(), *message = dict_new();
   dict_add(fields, "url", receiver ? receiver->url : url);
   dict_add(fields, "driver", receiver ? receiver->driver : "kiwi");
   dict_add(fields, "request", id);
   /* Directory information is descriptive, not an authorization source.
    * The server records actual initiator and request time itself. */
   if (receiver) {
      const char *names[] = {"name", "location", "country", "contact", "antenna", "hardware", "grid"};
      const char *values[] = {receiver->name, receiver->location, receiver->country,
         receiver->contact, receiver->antenna, receiver->hardware, receiver->grid};
      for (unsigned i = 0; i < G_N_ELEMENTS(names); i++) {
         if (values[i] && *values[i]) {
            char key[48];
            snprintf(key, sizeof(key), "directory.%s", names[i]);
            dict_add(fields, key, values[i]);
         }
      }
      char number[48];
      snprintf(number, sizeof(number), "%d", receiver->users);
      dict_add(fields, "directory.users", number);
      snprintf(number, sizeof(number), "%d", receiver->users_max);
      dict_add(fields, "directory.users_max", number);
      snprintf(number, sizeof(number), "%llu", (unsigned long long)receiver->low_hz);
      dict_add(fields, "directory.low_hz", number);
      snprintf(number, sizeof(number), "%llu", (unsigned long long)receiver->high_hz);
      dict_add(fields, "directory.high_hz", number);
      snprintf(number, sizeof(number), "%lld", receiver->last_seen);
      dict_add(fields, "directory.last_seen", number);
   }

   if (!save && room && *room) dict_add(fields, "room", room);
   char *json = dict2json(fields);
   dict_add(message, "msg.type", "talk");
   dict_add(message, "talk.cmd", "admin");
   dict_add(message, "talk.action", save ? "websdr-add" : "websdr-connect");
   dict_add(message, "talk.data", json);
   pending.profile = g_strdup(profile);
   pending.request = g_strdup(id);
   pending.connection = connection;
   pending.started = mono_us();
   pending.save = save;
   bool sent = ws_send_dict(NULL, connection, message, WEBSOCKET_OP_TEXT);
   free(json);
   dict_free(fields);
   dict_free(message);

   if (!sent) {
      clear_pending();
   }
   report(sent ? "Receiver request sent to the selected server…" : "Unable to send receiver request.", connection);

   return sent;
}
bool rrclient_receiver_connect(const char *url, bool save) {
   return rrclient_receiver_connect_room(url, save, NULL);
}
static void join_receiver(void *data) {
   char *args[] = {
      (char *)"join", data
   };
   cmd_join(2, args);
}
static void receiver_result(const char *event, const char *text, rrconn_t *connection, void *unused) {
   (void)event;
   (void)unused;

   if (pending.request && mono_us() - pending.started >= 15000000) {
      clear_pending();

      return;
   }

   if (!pending.request || pending.connection != connection || rrclient_connection_find(pending.profile) != connection) {
      return;
   }
   dict *message = text ? json2dict(text) : NULL;

   if (!message || strcmp(dict_get(message, "talk.action", ""), "websdr-result")) {
      dict_free(message);

      return;
   }
   dict *result = json2dict(dict_get(message, "talk.data", ""));

   if (result && !strcmp(dict_get(result, "request", ""), pending.request)) {
      const char *error = dict_get(result, "error", ""), *room = dict_get(result, "room", "");

      if (*error) {
         report(error, connection);
      } else if (pending.save) {
         report("Receiver added to server configuration. Connect when ready to listen.", connection);
      } else if (ws_room_name_valid(room) && connection->authenticated) {
         rrclient_connection_dispatch(connection, join_receiver, (void *)room);
         report("Joining the receiver rig; audio starts on demand.", connection);
      }
      clear_pending();
   }
   dict_free(result);
   dict_free(message);
}
static void receiver_closed(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)data;
   (void)unused;

   if (pending.connection == connection) {
      clear_pending();
   }
}
void rrclient_receiver_connect_init(void) {
   result_token = event_on_token("admin.result", receiver_result, NULL);
   close_token = event_on_token("disconnected", receiver_closed, NULL);
}
void rrclient_receiver_connect_shutdown(void) {
   if (result_token) {
      event_off_token(result_token);
   }

   if (close_token) {
      event_off_token(close_token);
   }
   result_token = close_token = NULL;
   clear_pending();
}
