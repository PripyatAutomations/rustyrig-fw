/* RustyRig event/connection adapter. IRC state and wire handlers live in librustyirc.
 * Licensed under MIT, or GPL when built with mongoose. */
#include <librustyaxe/core.h>
#include <rrclient/irc.h>
#include <rrclient/irc.dcc.h>
#include <librustyirc/protocol.h>

static bool send_wire(void *context, const char *wire, size_t size) {
   return size >= 2 && irc_send(context, "%.*s", (int)(size - 2), wire);
}
static void emit(void *context, irc_conn_t *irc, const char *event, const char *data) {
   rrconn_t *c = context;
   /* NICK listeners identify self by the old prefix before updating their UI. */
   bool nick_event = !strcmp(event, "irc.nick");
   if (!nick_event) snprintf(c->nick, sizeof(c->nick), "%s", rustyirc_connection_nick(irc));
   if (rustyirc_connection_registered(irc)) c->authenticated = true;
   event_emit(event, c, data);
   if (nick_event) snprintf(c->nick, sizeof(c->nick), "%s", rustyirc_connection_nick(irc));
}
/* Lazily construct state for existing embedding call sites, including server-side
 * dictionary translation. The client still owns transport lifetime. */
irc_conn_t *rrclient_irc_state(const rrconn_t *connection) {
   rrconn_t *c = (rrconn_t *)connection;
   if (!c || c->is_ws) return NULL;
   if (!c->irc_conn) {
      rustyirc_callbacks_t cb = { .send = send_wire, .on_event = emit, .context = c };
      c->irc_conn = rustyirc_connection_new(c->fd, &cb);
      rustyirc_connection_set_nick(c->irc_conn, c->nick);
   }
   if (c->irc_conn) {
      rustyirc_connection_set_fd(c->irc_conn, c->fd);
      if (c->server) rustyirc_connection_set_sasl(c->irc_conn, c->server->sasl_user,
         c->server->sasl_pass, c->server->sasl_mechanism);
   }
   return c->irc_conn;
}
extern void rrclient_irc_adapter_shutdown(void);
void irc_shutdown(void) { rrclient_irc_adapter_shutdown(); rustyirc_shutdown(); irc_dcc_shutdown(); }
bool irc_register_default_callbacks(void) { return rustyirc_register_default_callbacks(); }
bool irc_register_default_numeric_callbacks(void) { return rustyirc_register_default_numeric_callbacks(); }
bool irc_register_callback(rr_irc_callback_t *cb) { return rustyirc_register_callback(cb); }
bool irc_remove_callback(rr_irc_callback_t *cb) { return rustyirc_remove_callback(cb); }
bool irc_set_conn_pool(rrlist_t *pool) { return !pool; }
bool irc_dispatch_message(rrconn_t *c, irc_message_t *m) {
   return rustyirc_dispatch_message(rrclient_irc_state(c), m);
}
bool irc_process_message(rrconn_t *c, const char *line) {
   irc_message_t *m = irc_parse_message(line);
   if (!m) return true;
   bool failed = irc_dispatch_message(c, m);
   irc_message_free(m);
   return failed;
}
void irc_emit_message(const char *event, rrconn_t *c, const irc_message_t *m) {
   rustyirc_emit_message(event, rrclient_irc_state(c), m);
}
void irc_capabilities_clear(rrconn_t *c) {
   if (c) { rustyirc_connection_free(c->irc_conn); c->irc_conn = NULL; }
}
void irc_capabilities_shutdown(void) { rustyirc_capabilities_shutdown(); }
void irc_capabilities_reset(rrconn_t *c) { rustyirc_capabilities_reset(rrclient_irc_state(c)); }
void irc_capabilities_poll(rrconn_t *c) { rustyirc_capabilities_poll(rrclient_irc_state(c)); }
void irc_capabilities_message(rrconn_t *c, const irc_message_t *m) { rustyirc_capabilities_message(rrclient_irc_state(c), m); }
bool irc_supports_rustyrig(const rrconn_t *c) { return rustyirc_supports_rustyrig(rrclient_irc_state(c)); }
char irc_prefix_mode(const rrconn_t *c, char symbol) { return rustyirc_prefix_mode(rrclient_irc_state(c), symbol); }
const char *irc_modes_symbol(const rrconn_t *c, const char *modes) { return rustyirc_modes_symbol(rrclient_irc_state(c), modes); }
bool irc_mode_has_argument(const rrconn_t *c, char mode, bool adding) { return rustyirc_mode_has_argument(rrclient_irc_state(c), mode, adding); }
size_t irc_line_limit(const rrconn_t *c) { return rustyirc_line_limit(rrclient_irc_state(c)); }
const char *irc_server_property(const rrconn_t *c, const char *name) { return rustyirc_server_property(rrclient_irc_state(c), name); }
const char *irc_server_capability(const rrconn_t *c, const char *name) { return rustyirc_server_capability(rrclient_irc_state(c), name); }
const char *irc_server_capab(const rrconn_t *c, const char *name) { return rustyirc_server_capab(rrclient_irc_state(c), name); }
bool irc_capability_enabled(const rrconn_t *c, const char *name) { return rustyirc_capability_enabled(rrclient_irc_state(c), name); }
char *irc_dcc_audio_encode(const char *parent, dict *d) { return rustyirc_dcc_audio_encode(parent, d); }
dict *irc_dcc_audio_decode(const char *payload) { return rustyirc_dcc_audio_decode(payload); }
bool irc_send_dcc_audio(rrconn_t *c, const char *target, const char *parent, dict *d) { return rustyirc_send_dcc_audio(rrclient_irc_state(c), target, parent, d); }
bool irc_dcc_audio_message(rrconn_t *c, const irc_message_t *m) { return rustyirc_dcc_audio_message(rrclient_irc_state(c), m); }
