//
// rrclient/connman.h
//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__rrclient_connman_h)
#define __rrclient_connman_h
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#if     defined(USE_GTK)
#include <gtk/gtk.h>
#endif // defined(USE_GTK)
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

/* Profile names come from server URLs, not optional login usernames. */
static inline bool rrclient_server_profile_name(const char *key, char *name, size_t capacity) {
   if (!key || strncmp(key, "server:", 7)) {
      return false;
   }
   size_t length = strlen(key);

   if (length <= 18 || strcmp(key + length - 11, ".server.url") || length - 18 >= capacity) {
      return false;
   }
   memcpy(name, key + 7, length - 18);
   name[length - 18] = '\0';

   return true;
}

/* Connection selection and iteration use configuration section names. */
/* Optional when a component is linked alone by a unit test. */
extern bool rrclient_context_is_selected(void) __attribute__((weak));
static inline bool rrclient_present_context(void) {
   return !rrclient_context_is_selected || rrclient_context_is_selected();
}
extern const char *rrclient_selected_server(void);
extern const char *rrclient_connection_name(const rrconn_t *connection);
extern rrconn_t *rrclient_connection_find(const char *name);
extern const char *rrclient_connection_iter(unsigned index);
/* 0 offline, -1 connecting/retrying, 1 online. */
extern int rrclient_connection_state(const char *name);
extern bool rrclient_connection_select(const char *name);
extern bool rrclient_connection_cycle(void);
extern bool rrclient_connection_cycle_status(bool status_active);
extern const char *rrclient_window_name(const char *room);
extern const char *rrclient_window_room(const char *window);
extern void rrclient_connection_select_window(const char *window);

// Connected sessions
extern char active_server[512];
extern rr_connection_t *active_connections;
extern bool disconnect_server(const char *server);
extern bool rrclient_connect_url(const char *profile, const char *url);
/* NULL inherits configured settings; a nonempty override is mandatory on retries. */
extern bool rrclient_connect_url_proxy(const char *profile, const char *url, const char *proxy);
extern bool connect_server(const char *server);
#if     defined(USE_MONGOOSE)
extern rrconn_t *ws_conn, *ws_tx_conn;
#endif
extern const char *get_server_property(const char *server, const char *prop);
extern bool connect_or_disconnect(const char *server);
extern void connman_register_events(void);
extern void connman_shutdown(void);
extern void rrclient_quit_servers(const char *reason);
extern void rrclient_flush_quit(void);
extern void rrclient_poll_events_reconnect(void);

extern bool config_network_cb(const char *path, int line, const char *section, const char *buf);

#endif // !defined(__rrclient_connman_h)
