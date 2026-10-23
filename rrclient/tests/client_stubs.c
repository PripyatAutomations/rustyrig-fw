/* Optional dependencies for isolated client fixtures. Tests supply strong definitions for the behavior they exercise; these defaults never start UI/I/O. */
#include <stdio.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <rrclient/irc.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
#include <rrclient/userlist.h>
#include <rrclient/irc.whois.h>
#include <rrclient/irc.stats.h>
#define WEAK __attribute__((weak))
WEAK enum GuiMode ui_mode = UI_MODE_TUI;
WEAK struct rr_user *global_userlist;
WEAK rrconn_t *ws_conn;
WEAK const char *rrclient_current_room(void) {
   return ui_active_window_name();
}
WEAK const char *rrclient_active_room(void) {
   return rrclient_current_room();
}
WEAK const char *ui_active_window_name(void) {
   return "status";
}
WEAK const char *rrclient_connection_name(const rrconn_t *connection) {
   (void)connection;

   return "test";
}
WEAK rrconn_t *rrclient_connection_find(const char *profile) {
   (void)profile;

   return ws_conn;
}
WEAK void rrclient_connection_dispatch(rrconn_t *connection, void (*callback) (void *), void *data) {
   (void)connection;
   callback(data);
}
WEAK int rrclient_connection_state(const char *profile) {
   (void)profile;

   return ws_conn && ws_conn->authenticated ? 1 : 0;
}
WEAK bool disconnect_server(const char *profile) {
   (void)profile;

   return false;
}
WEAK void userlist_note_spoke(const char *room, const char *name) {
   (void)room;
   (void)name;
}
WEAK uint64_t userlist_spoke_order(const char *room, const char *name) {
   (void)room;
   (void)name;

   return 0;
}
WEAK bool userlist_is_oper(const char *name) {
   (void)name;

   return false;
}
WEAK void userlist_mark_oper(const char *name, bool oper) {
   (void)name;
   (void)oper;
}
WEAK struct rr_user *userlist_find_in_room(const char *name, const char *room) {
   for (struct rr_user *user = global_userlist ; user ; user = user->next) {
      if (!strcasecmp(user->name, name) && (!room || !strcasecmp(user->room, room))) {
         return user;
      }
   }

   return NULL;
}
WEAK const char *userlist_irc_prefix(const struct rr_user *user) {
   return irc_modes_symbol(ws_conn, user->privs + 4);
}
WEAK void userlist_display_name(const struct rr_user *user, char *out, size_t capacity) {
   snprintf(out, capacity, "%s%s", !strncmp(user->privs, "irc:", 4) ? userlist_irc_prefix(user) : "", user->name);
}
WEAK void rrclient_irc_query_opened(rrconn_t *connection, const char *name) {
   (void)connection;
   (void)name;
}
WEAK void rrclient_irc_stats_register(void) {
}
WEAK void rrclient_irc_stats_highlight(rrconn_t *connection, const char *room) {
   (void)connection;
   (void)room;
}

WEAK bool rrclient_media_local_tx(const char *uuid) {
   (void)uuid;

   return false;
}

/* Event probes have no connection-owned channel tabs unless a test overrides this. */
WEAK const char *rrclient_window_find_for_connection(const rrconn_t *connection, const char *room) {
   (void)connection;
   (void)room;

   return NULL;
}
