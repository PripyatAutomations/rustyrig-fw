//
// rrclient/main.c: Core of the client
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#include <rrclient/database.h>
#include <reset.files.h>
#include <rrclient/backup.h>
#include <getopt.h>
#include <stddef.h>
#include <rrclient/identity.store.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librustyaxe/io.serial.h>
#include <librustyui/termkey.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.binframe.h>
#include <librrprotocol/cfg.fwdsp.h>
#include <librrprotocol/rtp.h>
#include <librustyaxe/cfg.modules.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <glib.h>
extern defconfig_t defcfg[];
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif
#define MAX_WINDOWS 32
#define INPUT_HISTORY_MAX 64
#include <rrclient/ui.h>
#include <rrclient/media.h>
#include <rrclient/audio.h>
#include <rrclient/frontend.h>
#include <rrclient/receiver.directory.h>
#include <rrclient/ui.layout.h>
#include <librustyui/form.h>
#include <librustyui/canvas.h>
#include <rrbridge/spectrum.row.h>
#include <rrclient/receiver.connect.h>
#include <rrclient/rooms.h>
#include <rrclient/local.bridge.h>
#include <rrclient/dcc.h>
#include <rrclient/media.direct.h>
#include <rrclient/config.layers.h>
#include <rrclient/database.h>
#include <rrclient/cat.h>
#include <rrclient/connman.h>
#include <rrclient/userlist.h>
#include <rrclient/vfo.h>
#include <rrclient/ui.statusbar.h>
#ifdef RR_HAVE_RUSTYSCRIPT
#include <rrscript/host.h>
void rrclient_script_init(void);
void rrclient_script_shutdown(void);
#endif

#ifdef USE_MONGOOSE
extern struct mg_mgr mgr;
#endif // defined(USE_MONGOOSE)

/* A local TX inhibit. A zero-percent GTK power setting cannot key RF, but release commands are always allowed through. */
int rrclient_tx_power_percent = 100;
bool rrclient_ptt_allowed(void) {
   return rrclient_tx_power_percent > 0;
}

void rrclient_send_ptt_command(rrconn_t *connection, const char *vfo, bool on, const char *room) {
   if (on && !rrclient_ptt_allowed()) {
      Log(LOG_WARN, "ptt", "PTT ON inhibited locally: TX power is 0%%");

      return;
   }

   /* A routing control signal is the outgoing command on this exact pin, not the radio's reported TX state. Never publish when send fails. */
   if (ws_send_ptt_cmd_in_room(connection, vfo, on, room)) {
      rrclient_routing_ptt_signal_t signal = {
         .source = RRCLIENT_ROUTING_PTT_LOCAL, .raised = on
      };
      rr_event_message_t message = {
         .name = RRCLIENT_ROUTING_PTT_SIGNAL,
         .sender = "core", .payload = &signal, .length = sizeof(signal)
      };
      event_emit_message(&message);
   }
}

/* A modem PTT wire owns its specific keying session. Releasing PTT must go back to the same server/room/VFO even if the selected VFO changes. Core context
 * only; no GTK or global ws_conn access. */
typedef struct {
   char source[64], rx_id[64], profile[512], room[128], vfo[2];
} rr_modem_ptt_session_t;
static rr_modem_ptt_session_t modem_ptt_sessions[8];

static void rrclient_publish_routed_ptt(const char *source, bool raised) {
   rrclient_routing_ptt_signal_t signal = {
      .raised = raised
   };
   g_strlcpy(signal.source, source, sizeof(signal.source));
   rr_event_message_t msg = {
      .name = RRCLIENT_ROUTING_PTT_SIGNAL, .sender = "core",
      .payload = &signal, .length = sizeof(signal)
   };
   event_emit_message(&msg);
}

bool rrclient_modem_ptt_send(const char *source, const char *rx_id, bool on) {
   if (!source || !*source || strlen(source) >= 64) {
      return false;
   }
   rr_modem_ptt_session_t *own = NULL, *free_slot = NULL;

   for (guint i = 0 ; i < G_N_ELEMENTS(modem_ptt_sessions) ; i++) {
      rr_modem_ptt_session_t *slot = &modem_ptt_sessions[i];

      if (!strcmp(slot->source, source)) {
         own = slot;
         break;
      }

      if (!slot->source[0] && !free_slot) {
         free_slot = slot;
      }
   }

   if (!on) {
      if (!own) {
         return false;
      }
      rrconn_t *conn = rrclient_connection_find(own->profile);
      gboolean sent = conn && conn->is_ws && ws_send_ptt_cmd_in_room(conn, own->vfo, false, own->room);
      rrclient_publish_routed_ptt(source, false);
      memset(own, 0, sizeof(*own));

      return sent;
   }

   if (!free_slot || own || !rx_id || !*rx_id || !rrclient_ptt_allowed()) {
      return false;
   }

   /* Only one modem may own a selected TX encoder at a time. */
   for (guint i = 0 ; i < G_N_ELEMENTS(modem_ptt_sessions) ; i++) {
      if (modem_ptt_sessions[i].source[0]) {
         return false;
      }
   }

   if (!rrclient_media_current_codec(true)) {
      return false;
   }
   char vfo[2] = {
      vfo_state_get_active(), 0
   };

   /* Never take over a key held by a manual UI PTT or another session. */
   if (vfo_state_get_bool(vfo, "cat.state.ptt", false)) {
      return false;
   }
   struct rrclient_rx_stream streams[64] = {
      0
   };
   size_t count = audio_rx_streams(streams, G_N_ELEMENTS(streams));

   for (size_t i = 0 ; i < count && i < G_N_ELEMENTS(streams) ; i++) {
      const struct rrclient_rx_stream *rx = &streams[i];

      if (strcmp(rx_id, rx->id) || !rx->active_tx || !rrclient_media_rx_is_active_tx(rx->uuid) || !rrclient_room_tx_control(rx->room)) {
         continue;
      }
      rrconn_t *conn = rrclient_connection_find(rx->server);

      if (!conn || !conn->is_ws || !ws_send_ptt_cmd_in_room(conn, vfo, true, rx->room)) {
         return false;
      }
      g_strlcpy(free_slot->source, source, sizeof(free_slot->source));
      g_strlcpy(free_slot->rx_id, rx_id, sizeof(free_slot->rx_id));
      g_strlcpy(free_slot->profile, rx->server, sizeof(free_slot->profile));
      g_strlcpy(free_slot->room, rx->room, sizeof(free_slot->room));
      g_strlcpy(free_slot->vfo, vfo, sizeof(free_slot->vfo));
      rrclient_publish_routed_ptt(source, true);

      return true;
   }

   return false;
}

/* GTK issues requests only; the core domain owns connection and TX state. */
typedef struct {
   char room[128];
   char vfo;
   char on;
} RrPttRequest;
static void rrclient_ptt_request_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message->payload || message->length != sizeof(RrPttRequest)) {
      return;
   }
   RrPttRequest request;
   memcpy(&request, message->payload, sizeof(request));

   if (request.on != 0 && request.on != 1) {
      return;
   }

   if (request.vfo < 'A' || request.vfo > 'D') {
      return;
   }

   if (!memchr(request.room, '\0', sizeof(request.room))) {
      return;
   }

   if (!rrclient_connection_context() || ws_connected != 1) {
      return;
   }
   char vfo[2] = {
      request.vfo, '\0'
   };

   /* PTT targets the rig's TX-control room, not the currently selected chat tab. rrclient_current_room() can resolve to the authoritative chat room when this
    * event runs on core, which is not necessarily the room the GTK rig controls. */
   if (request.on && (!rrclient_room_tx_control(request.room) || request.vfo != vfo_state_get_active())) {
      Log(LOG_WARN, "ptt", "Rejecting PTT ON: no TX-control access or VFO changed (room=%s vfo=%c)", request.room, request.vfo);

      return;
   }
   Log(LOG_DEBUG, "ptt", "Core PTT request: %s room=%s vfo=%c", request.on ? "ON" : "OFF", request.room, request.vfo);
   rrclient_send_ptt_command(rrclient_connection_context(), vfo, request.on != 0, request.room);
}

/* Supervision decisions arrive from GTK without an rrconn_t pointer. */
typedef struct {
   char profile[128], room[128], id[96];
   uint8_t accept;
} SupervisionReply;
static void rrclient_supervision_reply_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message->payload || message->length != sizeof(SupervisionReply)) {
      return;
   }
   SupervisionReply reply;
   memcpy(&reply, message->payload, sizeof(reply));

   if (!memchr(reply.profile, 0, sizeof(reply.profile)) ||
      !memchr(reply.room, 0, sizeof(reply.room)) || !memchr(reply.id, 0, sizeof(reply.id)) || !reply.profile[0] || !reply.id[0] || reply.accept > 1) {
      return;
   }
   rrconn_t *connection = rrclient_connection_find(reply.profile);

   if (!connection || !connection->authenticated) {
      return;
   }
   dict *d = dict_new();

   if (!d) {
      return;
   }
   char tail[128];
   snprintf(tail, sizeof(tail), "%s %s", reply.accept ? "accept" : "decline", reply.id);
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "supervise");
   dict_add(d, "talk.room", reply.room);
   dict_add(d, "talk.data", tail);
   ws_send_dict(NULL, connection, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
}

extern const char *configs[];   // from defcfg.c
extern const int num_configs;
extern bool cfg_ui_bell_chat;

extern void connman_autoconnect(void);
extern void rrclient_register_events(void);
extern bool audio_init(void);   // rrclient/audio.c
extern void webcam_client_register_events(void);   // webcam.c
extern bool rrclient_autoconnect(void);
extern void rrclient_poll_events(void);
extern void rrclient_poll_events_reconnect(void);
extern void ws_client_init(void);
extern bool parse_chat_input_real(const char *msg);   // cmd.c
#include <rrclient/cmd.h>
extern bool cfg_servers_init(void) __attribute__((weak));   // cfg.servers.c (optional:
                                                            // IRC server list)
extern bool cfg_network_save_init(void);  // cfg.network.c
extern const char *config_file; // librustyaxe/config.c
extern bool tui_over_ssh;       // librustyaxe/tui.c
// GTK (and any other graphical frontend) lives in a dynamically loaded
// module; core rrclient never links a GUI toolkit.
static bool force_tui = false;  // -T: never load a frontend module
static rr_module_t *frontend_module;   // set once the module loads
struct timespec mono_now;
bool rrclient_cleanup(void);
const char *cfg_log_audio = NULL;
bool cfg_mirc_colors = true;
bool cfg_ui_gtk_vfo_on_top = true;
int cfg_tick_interval = 100;
bool dying = false;
bool restarting = false;
int cfg_ui_vfo_viscosity = -1;
int cfg_ui_edit_delay = 3;      // Seconds to suppress freq updates after local edit
int cfg_ui_ptt_ack_timeout = 2; // Seconds to wait for a PTT ack before reverting
// (gtk.ptt-btn.c)
int cfg_ui_ptt_hold_delay = 500; // Milliseconds before a PTT shortcut is treated as
                                 // hold-to-talk
time_t now = 0;
time_t poll_block_delay = 0;    // CAT polling suppression delay in seconds

/* Keep all long-lived client configuration mirrors in one place.  This is called once after startup config has loaded and again after every complete config
 * reload. */
static bool rrclient_config_refresh(const char *key) {
   (void)key;
   const char *log_audio = cfg_get_exp("log.audio");
   free((void *)cfg_log_audio);
   cfg_log_audio = log_audio;
#ifdef USE_GSTREAMER

   if (cfg_log_audio) {
#ifdef _WIN32
      SetEnvironmentVariable("GST_DEBUG", cfg_log_audio);
      SetEnvironmentVariable("GST_DEBUG_DUMP_DOT_DIR", ".");
#else
      setenv("GST_DEBUG", cfg_log_audio, 1);
      setenv("GST_DEBUG_DUMP_DOT_DIR", ".", 1);
#endif
   }
#endif

   cfg_ui_vfo_viscosity = cfg_get_int("ui.vfo.visocity", 1000);
   poll_block_delay = (cfg_ui_vfo_viscosity > 0) ? (cfg_ui_vfo_viscosity / 1000) : 0;
   cfg_ui_edit_delay = cfg_get_int("ui.edit-delay", 3);
   cfg_ui_ptt_ack_timeout = cfg_get_int("ui.ptt-ack-timeout", 2);
   cfg_ui_ptt_hold_delay = cfg_get_int("ui.ptt-hold-delay", 500);

   if (cfg_ui_ptt_hold_delay < 0) {
      cfg_ui_ptt_hold_delay = 0;
   }
   cfg_ui_bell_chat = cfg_get_bool("ui.bell.chat", false);
   cfg_tick_interval = cfg_get_int("core.tick-interval", 100);

   /* This setting is shared by the GTK and TUI input implementations. */
   tui_set_shared_input_history(cfg_get_bool("ui.shared-input-history", true));

   Log(LOG_DEBUG, "main", "Refreshed cached client configuration");

   return true;
}

/* The process main/default GLib context is the core domain. Messages from frontend workers enter here with owned envelopes, not rrconn_t pointers. The TUI and
 * GTK configurations share this destination. */
typedef struct {
   event_dispatch_fn_t fn;
   void *arg;
} CoreEventDelivery;
static gboolean rrclient_core_deliver_event(gpointer opaque) {
   CoreEventDelivery *delivery = opaque;
   gint64 started = g_get_monotonic_time();
   delivery->fn(delivery->arg);
   gint64 elapsed = g_get_monotonic_time() - started;
   if (elapsed > 20000) {
      Log(LOG_WARN, "core.scheduler", "Core event delivery took %" G_GINT64_FORMAT "us", elapsed);
   }

   return G_SOURCE_REMOVE;
}

static void rrclient_core_dispatch_event(event_dispatch_fn_t fn, void *arg, void *user) {
   GMainContext *context = user;
   CoreEventDelivery *delivery = g_new(CoreEventDelivery, 1);
   *delivery = (CoreEventDelivery) {
      fn, arg
   };
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, rrclient_core_deliver_event, delivery, g_free);
   g_source_attach(source, context);
   g_source_unref(source);
}

/* Dedicated core context for GTK; TUI uses the default context. */
static GMainContext *rr_core_context;
GMainContext *rrclient_core_context(void) {
   return rr_core_context ? rr_core_context : g_main_context_default();
}

static guint rr_core_timeout(guint interval, gint priority, GSourceFunc callback) {
   GSource *source = g_timeout_source_new(interval);
   g_source_set_priority(source, priority);
   g_source_set_callback(source, callback, NULL, NULL);
   guint id = g_source_attach(source, rrclient_core_context());
   g_source_unref(source);

   return id;
}

static gboolean ws_poll_cb(gpointer user_data) {
   /* Diagnostic latency of the core scheduler, independent of GTK's widget paint callbacks. Rate-limit reports to once per 10 seconds. */
   static gint64 last_poll_us, last_report_us;
   gint64 tick_us = g_get_monotonic_time();

   if (last_poll_us && tick_us - last_poll_us > 100000 && tick_us - last_report_us > 10000000) {
      Log(LOG_WARN, "core.scheduler", "Network scheduler poll gap=%" G_GINT64_FORMAT "us tid=%p", tick_us - last_poll_us, (void *)g_thread_self());
      last_report_us = tick_us;
   }
   last_poll_us = tick_us;

   if (dying) {
      return G_SOURCE_REMOVE;
   }
   gint64 started = g_get_monotonic_time();
   rrclient_poll_events();
   gint64 polled = g_get_monotonic_time();
   // GTK and TUI share the nonblocking transport sweep and reconnect engine.
   rrclient_poll_events_reconnect();
   gint64 finished = g_get_monotonic_time();
   if (polled - started > 20000 || finished - polled > 20000) {
      Log(LOG_WARN, "core.scheduler", "Slow network sweep: events=%" G_GINT64_FORMAT "us reconnect=%" G_GINT64_FORMAT "us",
          polled - started, finished - polled);
   }

   return G_SOURCE_CONTINUE;
}

/*
 * Periodic fwdsp housekeeping shared by both GTK and TUI modes.
 */
static gboolean fwdsp_maintenance_cb(gpointer user_data) {
   (void)user_data;

   fwdsp_reap_children();
   fwdsp_sweep_expired();

   return G_SOURCE_CONTINUE;
}

bool ptt_active = false;
time_t poll_block_expire = 0;   // Here we set this to now +
                                // config:cat.poll-blocking to prevent rig
                                // polling from sclearing local controls

/* Scripted receiver directory: stable URL keys, no catalog traversal in draw. */
static rrui_form_t *sdr_form,*sdr_detail,*sdr_mark_form;
static tui_window_t *sdr_tab;
static bool tui_sdr_availability,tui_sdr_connected,tui_sdr_favorites,tui_sdr_proxy_bridge;
static char tui_sdr_search[128],tui_sdr_message[256];
static char *sdr_signature,*sdr_target;
static gint64 sdr_checked;
static void sdr_rebuild(bool force);
static void sdr_action(rrui_form_t *,const char *,const char *,void *);
static const char *tui_sdr_country(const receiver_directory_entry_t *r) {
   return r && r->country && *r->country ? r->country : "Unknown";
}

static bool tui_sdr_is_connected(const receiver_directory_entry_t *r) {
   return r && r->url && (rrclient_local_bridge_connected(r->url) || rrclient_receiver_is_connected_any(r->url));
}

static bool tui_sdr_contains(const char *text, const char *needle) {
   if (!text) {
      return false;
   }
   char *lower = g_ascii_strdown(text, -1);
   bool found = g_strrstr(lower, needle) != NULL;
   g_free(lower);

   return found;
}

static bool tui_sdr_matches(const receiver_directory_entry_t *r) {
   /* GTK shows deleted/stale catalog entries when Availability is off. Only the Availability predicate should hide them. */
   if (!r) {
      return false;
   }

   if (tui_sdr_availability && !receiver_directory_entry_accessible(r)) {
      return false;
   }

   if (tui_sdr_connected && !tui_sdr_is_connected(r)) {
      return false;
   }

   if (tui_sdr_favorites && !r->favorite) {
      return false;
   }

   if (!tui_sdr_search[0]) {
      return true;
   }
   char *needle = g_ascii_strdown(tui_sdr_search, -1);
   const char *fields[] = {
      r->name, r->location, r->country, r->url,
      r->grid, r->contact, r->hardware, r->description, NULL
   };
   bool match = false;

   for (unsigned i = 0 ; i < G_N_ELEMENTS(fields) - 1 ; i++) {
      if (fields[i] && tui_sdr_contains(fields[i], needle)) {
         match = true;
         break;
      }
   }

   g_free(needle);

   return match;
}

static gint tui_sdr_country_compare(gconstpointer av, gconstpointer bv) {
   const char *a = av, *b = bv;

   return g_ascii_strcasecmp(a, b);
}

static gint tui_sdr_entry_compare(gconstpointer av, gconstpointer bv) {
   const size_t a = *(const size_t *)av, b = *(const size_t *)bv;
   const receiver_directory_entry_t *ra = receiver_directory_receiver(a);
   const receiver_directory_entry_t *rb = receiver_directory_receiver(b);
   int cmp = g_ascii_strcasecmp(ra && ra->location ? ra->location : "", rb && rb->location ? rb->location : "");

   return cmp ? cmp : g_ascii_strcasecmp(ra && ra->name ? ra->name : "", rb && rb->name ? rb->name : "");
}


static size_t sdr_field(rrui_form_t *f,const char *id){
   for(size_t i=0;i<rrui_form_count(f);i++){
      rrui_field_t x;
      if(rrui_form_field(f,i,&x)&&!strcmp(x.widget->id,id))return i;
   }
   return SIZE_MAX;
}
static const receiver_directory_entry_t *tui_sdr_current(void){
   rrui_field_t x;
   if(!sdr_form||!rrui_form_field(sdr_form,sdr_field(sdr_form,"receivers"),&x)||x.selected==SIZE_MAX)return NULL;
   const char *key=rrui_form_value(sdr_form,"receivers");
   return key&&*key?receiver_directory_find_url(key):NULL;
}
static char *sdr_clean(const char *raw){
   char *valid=g_utf8_make_valid(raw?raw:"",-1);
   GString *out=g_string_new("");
   for(const char *p=valid;*p;p=g_utf8_next_char(p)){
      gunichar ch=g_utf8_get_char(p);
      g_string_append_unichar(out,g_unichar_iscntrl(ch)||g_unichar_type(ch)==G_UNICODE_FORMAT?' ':ch);
   }
   g_free(valid);return g_string_free(out,FALSE);
}
static void sdr_label(rrui_form_t *f,const char *id,const char *value){
   char *clean=sdr_clean(value);
   if(g_utf8_strlen(clean,-1)>512)*g_utf8_offset_to_pointer(clean,512)=0;
   rrui_form_set_value(f,id,clean);g_free(clean);
}
static void sdr_notice(const char *message){
   sdr_label(sdr_form,"message",message);tui_redraw_request();
}
static void tui_sdr_connect_receiver(const receiver_directory_entry_t *r) {

   if (!r || !r->url) {
      return;
   }

   if (rrclient_local_bridge_connected(r->url)) {
      rrclient_local_bridge_disconnect_url(r->url);

      return;
   }

   if (rrclient_receiver_is_connected(r->url)) {
      rrclient_receiver_disconnect(r->url);

      return;
   }

   if (!receiver_directory_entry_accessible(r)) {
      g_strlcpy(tui_sdr_message, "Receiver is restricted or flagged; see Info.", sizeof(tui_sdr_message));

      return;
   }

   if (tui_sdr_proxy_bridge) {
      if (!rrclient_receiver_connect_room_ex(r->url, false, NULL, true)) {
         g_strlcpy(tui_sdr_message, "Unable to start proxy rrbridge: select an authenticated RustyRig server.", sizeof(tui_sdr_message));
      } else {
         g_strlcpy(tui_sdr_message, "Requesting remote SDR bridge...", sizeof(tui_sdr_message));
      }

      return;
   }

   /* Local rrbridge is the default, with no rrserver required. */
   if (!rrclient_local_bridge_connect_driver(r->url, r->driver && *r->driver ? r->driver : "kiwi")) {
      g_strlcpy(tui_sdr_message, "Unable to start local rrbridge (check executable/module and Client Log).", sizeof(tui_sdr_message));
   } else {
      g_strlcpy(tui_sdr_message, "Starting local SDR bridge...", sizeof(tui_sdr_message));
   }
}


static void sdr_rebuild(bool force){
   if(!sdr_form)return;
   const char *wanted=rrui_form_value(sdr_form,"receivers");
   char *saved=g_strdup(wanted?wanted:"");
   GHashTable *groups=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_array_unref);
   GList *countries=NULL;
   size_t total=receiver_directory_count(),matched=0;
   GString *sig=g_string_new("");
   for(size_t i=0;i<total;i++){
      const receiver_directory_entry_t *r=receiver_directory_receiver(i);
      if(!r||!r->url||!*r->url||!tui_sdr_matches(r))continue;
      matched++;
      const char *country=tui_sdr_country(r);
      GArray *indices=g_hash_table_lookup(groups,country);
      if(!indices){
         indices=g_array_new(FALSE,FALSE,sizeof(size_t));
         g_hash_table_insert(groups,g_strdup(country),indices);
         countries=g_list_prepend(countries,g_strdup(country));
      }
      g_array_append_val(indices,i);
      g_string_append_printf(sig,"%s\037%s\037%s\037%s\037%s\037%d,%d,%d,%d,%d\n",r->url,country,r->name?r->name:"",r->location?r->location:"",r->local_state?r->local_state:"",r->favorite,tui_sdr_is_connected(r),receiver_directory_entry_accessible(r),r->users,r->users_max);
   }
   char *digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,sig->str,sig->len);
   g_string_free(sig,TRUE);
   bool changed=force||g_strcmp0(sdr_signature,digest);
   if(changed){
      g_free(sdr_signature);sdr_signature=g_strdup(digest);
      rrui_form_list_clear(sdr_form,"receivers");
      countries=g_list_sort(countries,tui_sdr_country_compare);
      size_t row=0,first=SIZE_MAX,chosen=SIZE_MAX;
      for(GList *c=countries;c;c=c->next){
         GArray *indices=g_hash_table_lookup(groups,c->data);
         char *key=g_strdup_printf("@country:%s",(char *)c->data);
         char *label=g_strdup_printf("%s (%u)",(char *)c->data,indices->len);
         char *clean=sdr_clean(label);
         if(rrui_form_list_header(sdr_form,"receivers",key,clean))row++;
         g_free(key);g_free(label);g_free(clean);
         g_array_sort(indices,tui_sdr_entry_compare);
         for(guint j=0;j<indices->len;j++){
            const receiver_directory_entry_t *r=receiver_directory_receiver(g_array_index(indices,size_t,j));
            bool connected=tui_sdr_is_connected(r),available=receiver_directory_entry_accessible(r);
            const char *state=connected?"Connected":r->deleted?"Deleted":r->flagged?"Flagged":r->offline?"Reported offline":
               r->local_state&&strcmp(r->local_state,"unknown")&&strcmp(r->local_state,"good")?r->local_state:
               !r->driver||strcmp(r->driver,"kiwi")?"Unsupported":r->ext_api<=0?"No API access":
               available?"Available":"Unavailable";
            label=g_strdup_printf(" %s %-28s  %-20s %d/%d  %s",r->favorite?"*":" ",r->name&&*r->name?r->name:r->url,r->location?r->location:"",r->users,r->users_max,state);
            clean=sdr_clean(label);
            if(rrui_form_list_append_color(sdr_form,"receivers",r->url,clean,connected?0x70d898:available?UINT32_MAX:0xb5a0a0)){
               if(first==SIZE_MAX)first=row;
               if(!strcmp(saved,r->url))chosen=row;
               row++;
            }
            g_free(clean);g_free(label);
         }
      }
      if(first!=SIZE_MAX)rrui_form_select(sdr_form,sdr_field(sdr_form,"receivers"),chosen!=SIZE_MAX?chosen:first);
      tui_redraw_request();
   }
   g_free(digest);g_free(saved);g_list_free_full(countries,g_free);g_hash_table_unref(groups);
   char *summary=g_strdup_printf("%zu / %zu receivers | %s",matched,total,receiver_directory_status());
   char *clean=sdr_clean(summary);
   if(g_utf8_strlen(clean,-1)>512)*g_utf8_offset_to_pointer(clean,512)=0;
   if(g_strcmp0(rrui_form_value(sdr_form,"summary"),clean)){
      rrui_form_set_value(sdr_form,"summary",clean);tui_redraw_request();
   }
   g_free(clean);g_free(summary);
}
static void sdr_detail_action(rrui_form_t *f,const char *id,const char *action,void *unused){
   (void)id;(void)unused;
   if(!strcmp(action,"close")||!strcmp(action,"dialog.close")){
      sdr_detail=NULL;rrui_form_free(f);tui_redraw_request();
   }
}
static void sdr_show_detail(const receiver_directory_entry_t *r){
   const rrclient_ui_required_t req[]={{"name",RRUI_LABEL,false},{"url",RRUI_LABEL,false}};
   sdr_detail=rrclient_ui_layout_load("sdr-info.rrs",RRUI_DIALOG,req,G_N_ELEMENTS(req),sdr_detail_action);
   if(!sdr_detail)return;
   const char *ids[]={"name","url","location","country","grid","contact","antenna","hardware","description","state","reason"};
   const char *values[]={r->name,r->url,r->location,r->country,r->grid,r->contact,r->antenna,r->hardware,r->description,r->local_state,r->local_reason};
   for(unsigned i=0;i<G_N_ELEMENTS(ids);i++)sdr_label(sdr_detail,ids[i],values[i]);
   if(!rrui_tui_open(sdr_detail)){rrui_form_free(sdr_detail);sdr_detail=NULL;}
}
static void sdr_mark_action(rrui_form_t *f,const char *id,const char *action,void *unused){
   (void)id;(void)unused;
   const char *state=rrui_form_value(f,"states");
   if(!strcmp(action,"apply")&&sdr_target&&state&&(!strcmp(state,"good")||!strcmp(state,"unreachable")||!strcmp(state,"restricted")||!strcmp(state,"unknown"))){
      bool ok=receiver_directory_set_state(sdr_target,state,!strcmp(state,"unknown")?"":"User report; receiver was not probed");
      sdr_notice(ok?"Receiver mark saved.":"Unable to save receiver mark.");
   }
   sdr_mark_form=NULL;rrui_form_free(f);g_clear_pointer(&sdr_target,g_free);sdr_rebuild(true);
}
static void sdr_show_mark(const receiver_directory_entry_t *r){
   const rrclient_ui_required_t req[]={{"states",RRUI_LIST,false}};
   sdr_mark_form=rrclient_ui_layout_load("sdr-mark.rrs",RRUI_DIALOG,req,G_N_ELEMENTS(req),sdr_mark_action);
   if(!sdr_mark_form)return;
   g_free(sdr_target);sdr_target=g_strdup(r->url);
   const char *states[]={"good","unreachable","restricted","unknown"};
   for(unsigned i=0;i<G_N_ELEMENTS(states);i++)rrui_form_list_append(sdr_mark_form,"states",states[i],states[i]);
   rrui_form_select(sdr_mark_form,sdr_field(sdr_mark_form,"states"),3);
   if(!rrui_tui_open(sdr_mark_form)){rrui_form_free(sdr_mark_form);sdr_mark_form=NULL;g_clear_pointer(&sdr_target,g_free);}
}
static void sdr_action(rrui_form_t *f,const char *id,const char *action,void *unused){
   (void)id;(void)unused;
   if(!strcmp(action,"reset")){
      rrui_form_set_value(f,"search","");
      rrui_form_set_checked(f,"available",false);rrui_form_set_checked(f,"connected",false);rrui_form_set_checked(f,"favorites",false);
   }
   tui_sdr_availability=rrui_form_checked(f,"available");
   tui_sdr_connected=rrui_form_checked(f,"connected");
   tui_sdr_favorites=rrui_form_checked(f,"favorites");
   tui_sdr_proxy_bridge=rrui_form_checked(f,"proxy");
   const char *search=rrui_form_value(f,"search");
   g_strlcpy(tui_sdr_search,search?search:"",sizeof(tui_sdr_search));
   const receiver_directory_entry_t *r=tui_sdr_current();
   if(!strcmp(action,"random")){
      /* Reservoir sampling: one uniform choice among eligible filtered
       * receivers; never toggle an existing connection off. */
      size_t eligible=0;
      const receiver_directory_entry_t *pick=NULL;
      for(size_t i=0;i<receiver_directory_count();i++){
         const receiver_directory_entry_t *candidate=receiver_directory_receiver(i);
         if(!candidate||!candidate->url||!receiver_directory_entry_accessible(candidate)||tui_sdr_is_connected(candidate)||!tui_sdr_matches(candidate))continue;
         if(g_random_double()<1.0/(double)++eligible)pick=candidate;
      }
      if(!pick){sdr_notice("No accessible, disconnected SDR matches the filters.");return;}
      char *description=receiver_directory_display(pick->url);
      tui_sdr_message[0]=0;tui_sdr_connect_receiver(pick);
      char *notice=g_strdup_printf("%s: %s",tui_sdr_message[0]?tui_sdr_message:"Random SDR",description);
      sdr_notice(notice);g_free(notice);g_free(description);
   }else if(!strcmp(action,"connect")){
      char *description=r?receiver_directory_display(r->url):NULL;
      tui_sdr_message[0]=0;tui_sdr_connect_receiver(r);
      char *notice=g_strdup_printf("%s%s%s",tui_sdr_message[0]?tui_sdr_message:"Connection change requested.",description?" — ":"",description?description:"");
      sdr_notice(notice);g_free(notice);g_free(description);
   }else if(!strcmp(action,"info")&&r){sdr_show_detail(r);return;}
   else if(!strcmp(action,"mark")&&r){sdr_show_mark(r);return;}
   else if(!strcmp(action,"favorite")&&r){
      bool ok=receiver_directory_set_favorite(r->url,!r->favorite);
      sdr_notice(ok?"Favorite updated.":"Unable to update favorite.");
   }else if(!strcmp(action,"refresh")){
      receiver_directory_refresh_force(true);sdr_notice("Directory refresh requested.");
   }
   sdr_rebuild(false);
}
static bool sdr_init(tui_window_t *w){
   const rrclient_ui_required_t req[]={{"receivers",RRUI_LIST,false},{"search",RRUI_ENTRY,false},{"available",RRUI_CHECKBOX,false},{"connected",RRUI_CHECKBOX,false},{"favorites",RRUI_CHECKBOX,false},{"proxy",RRUI_CHECKBOX,false}};
   sdr_form=rrclient_ui_layout_load("sdr-directory.rrs",RRUI_PANEL,req,G_N_ELEMENTS(req),sdr_action);
   if(!sdr_form)return false;
   if(!rrui_tui_tab_attach(w,sdr_form)){rrui_form_free(sdr_form);sdr_form=NULL;return false;}
   sdr_tab=w;sdr_rebuild(true);rrui_form_set_focus(sdr_form,sdr_field(sdr_form,"receivers"));return true;
}
static void sdr_tick(void){
   if(sdr_form&&tui_active_window()==sdr_tab&&g_get_monotonic_time()-sdr_checked>=1000000){
      sdr_checked=g_get_monotonic_time();sdr_rebuild(false);
   }
}
static void sdr_shutdown(void){
   if(sdr_tab)rrui_tui_tab_detach(sdr_tab);
   rrui_form_free(sdr_form);rrui_form_free(sdr_detail);rrui_form_free(sdr_mark_form);
   sdr_form=sdr_detail=sdr_mark_form=NULL;sdr_tab=NULL;
   g_clear_pointer(&sdr_signature,g_free);g_clear_pointer(&sdr_target,g_free);
}
static void tui_sdr_safe_text(const char *s, int width) {
   for (int i = 0 ; i < width && s && s[i] ; i++) {
      unsigned char c = (unsigned char)s[i];
      putchar(c >= 32 && c < 127 ? c : ' ');
   }
}


/* Legacy ANSI colors shared by the old server chooser and local SDR controls.
 * Keep until both remaining views use RustyUI rendering. */
#define SDR_NORMAL "\033[0m"
#define SDR_TITLE "\033[1;36m"
#define SDR_HILITE "\033[1;37;44m"
#define SDR_BUTTON "\033[1;37;44m"
#define SDR_FOCUS "\033[1;30;46m"
#define SDR_ENABLED "\033[1;30;102m"
#define SDR_WARNING "\033[1;33m"
#define SDR_NAME "\033[1;96m"
#define SDR_USERS "\033[1;93m"
#define SDR_GOOD "\033[1;92m"
#define SDR_MUTED "\033[2;37m"

/* Temporary TUI server chooser. Created on demand and destroyed on exit;
 * the Status tab remains the stable home for connection selection. */
#define RR_TUI_SERVER_PICKER "Server Picker"
typedef struct {
   char *name;
   char *url;
} tui_server_entry_t;
static GPtrArray *tui_server_entries;
static size_t tui_server_selected, tui_server_scroll;
static int tui_server_button_focus = -1;
static int tui_server_buttons_start[3], tui_server_buttons_end[3];
static char tui_server_notice[160];

static void tui_server_entry_free(gpointer item) {
   tui_server_entry_t *entry = item;

   if (!entry) {
      return;
   }
   g_free(entry->name);
   g_free(entry->url);
   g_free(entry);
}

static gint tui_server_entry_cmp(gconstpointer ap, gconstpointer bp) {
   const tui_server_entry_t *a = *(tui_server_entry_t * const *)ap;
   const tui_server_entry_t *b = *(tui_server_entry_t * const *)bp;

   return g_ascii_strcasecmp(a->name, b->name);
}

static void tui_server_reload(void) {
   if (!tui_server_entries) {
      tui_server_entries = g_ptr_array_new_with_free_func(tui_server_entry_free);
   }
   g_ptr_array_set_size(tui_server_entries, 0);
   int rank = 0;
   const char *key;
   char *url;
   while ((rank = dict_enumerate(cfg, rank, &key, &url)) >= 0) {
      char name[512];

      if (!rrclient_server_profile_name(key, name, sizeof(name))) {
         continue;
      }
      tui_server_entry_t *entry = g_new0(tui_server_entry_t, 1);
      entry->name = g_strdup(name);
      entry->url = g_strdup(url ? url : "");
      g_ptr_array_add(tui_server_entries, entry);
   }
   g_ptr_array_sort(tui_server_entries, tui_server_entry_cmp);

   if (tui_server_selected >= tui_server_entries->len) {
      tui_server_selected = tui_server_entries->len ? tui_server_entries->len - 1 : 0;
   }

   if (tui_server_scroll > tui_server_selected) {
      tui_server_scroll = tui_server_selected;
   }
}

static tui_server_entry_t *tui_server_current(void) {
   return tui_server_entries && tui_server_selected < tui_server_entries->len ? g_ptr_array_index(tui_server_entries, tui_server_selected) : NULL;
}

static void tui_server_picker_close(void) {
   tui_window_t *picker = tui_window_find(RR_TUI_SERVER_PICKER);

   if (!picker) {
      return;
   }
   /* Focus before freeing the currently active window. */
   tui_window_focus("status");
   tui_window_destroy(picker);
   tui_redraw_request();
}

static void tui_server_picker_choose(void) {
   tui_server_entry_t *entry = tui_server_current();

   if (!entry) {
      return;
   }
   char *name = g_strdup(entry->name);
   int state = rrclient_connection_state(name);

   if (state) {
      if (rrclient_connection_select(name)) {
         tui_server_picker_close();
      } else {
         g_strlcpy(tui_server_notice, "Unable to select this connection.", sizeof(tui_server_notice));
      }
   } else {
      /* Existing /server behavior: false indicates a successful request. */
      if (!connect_server(name)) {
         tui_server_picker_close();
      } else {
         g_strlcpy(tui_server_notice, "Unable to initiate connection; check the Status log.", sizeof(tui_server_notice));
      }
   }
   g_free(name);
}

static void tui_server_picker_disconnect(void) {
   tui_server_entry_t *entry = tui_server_current();

   if (entry && rrclient_connection_state(entry->name)) {
      disconnect_server(entry->name);
   } else {
      g_strlcpy(tui_server_notice, "Selected profile is not connected.", sizeof(tui_server_notice));
   }
}

static void tui_server_picker_action(int action) {
   switch (action) {
      case 0: {
         tui_server_picker_choose();
         break;
      }
      case 1: {
         tui_server_picker_disconnect();
         break;
      }
      case 2: {
         tui_server_picker_close();
         break;
      }
   }
}

static void tui_server_picker_draw(tui_window_t *w, int rows, int cols, void *unused) {
   (void)w;
   (void)unused;
   tui_server_reload();
   printf("\033[2;1H" SDR_TITLE " Server Picker" SDR_NORMAL "   Select a profile (Enter), or Esc / Alt-X to close\033[K");
   printf("\033[3;1H" SDR_TITLE "    %-23s %-15s %-9s %s" SDR_NORMAL "\033[K", "Profile", "Connection", "Active", "URL");
   const char *selected = rrclient_selected_server();
   int height = rows - 6;

   if (height < 1) {
      height = 1;
   }

   if (tui_server_selected < tui_server_scroll) {
      tui_server_scroll = tui_server_selected;
   }

   if (tui_server_selected >= tui_server_scroll + (size_t)height) {
      tui_server_scroll = tui_server_selected - (size_t)height + 1;
   }

   if (!tui_server_entries->len) {
      printf("\033[4;1H" SDR_WARNING " No server profiles configured. Add server:<name>.server.url to configuration." SDR_NORMAL);
   }

   for (int y = 4 ; y < rows - 2 ; y++) {
      size_t idx = tui_server_scroll + (size_t)(y - 4);

      if (idx >= tui_server_entries->len) {
         break;
      }
      const tui_server_entry_t *entry = g_ptr_array_index(tui_server_entries, idx);
      int state = rrclient_connection_state(entry->name);
      const char *status = state == 1 ? "Online" : state < 0 ? "Connecting" : "Offline";
      printf("\033[%d;1H", y);
      fputs(idx == tui_server_selected ? SDR_HILITE : state == 1 ? SDR_ENABLED : SDR_NORMAL, stdout);
      printf(" %c  %-23.23s %-15.15s %-9.9s ", idx == tui_server_selected ? '>' : ' ', entry->name, status, selected && !strcmp(selected, entry->name) ?
         "Selected" : "");
      int remain = cols - 59;

      if (remain > 0) {
         printf("%.*s", remain, entry->url);
      }
      fputs(SDR_NORMAL "\033[K", stdout);
   }

   const char *labels[] = {
      "Select/Connect", "Disconnect", "Cancel"
   };
   int x = 2;

   for (int i = 0 ; i < 3 ; i++) {
      int width = (int)strlen(labels[i]) + 2;
      tui_server_buttons_start[i] = tui_server_buttons_end[i] = 0;

      if (x + width - 1 <= cols) {
         tui_server_buttons_start[i] = x;
         tui_server_buttons_end[i] = x + width - 1;
         printf("\033[%d;%dH%s[%s]" SDR_NORMAL, rows - 1, x, i == tui_server_button_focus ? SDR_FOCUS : SDR_BUTTON, labels[i]);
      }
      x += width + 2;
   }

   printf("\033[%d;1H", rows);

   if (tui_server_notice[0]) {
      fputs(SDR_WARNING, stdout);
      tui_sdr_safe_text(tui_server_notice, cols - 1);
      fputs(SDR_NORMAL, stdout);
   } else {
      printf("%u configured profiles  |  Up/Down: select  Tab: buttons  Space/Enter: activate", tui_server_entries->len);
   }
   fputs("\033[K", stdout);
}

static bool tui_server_picker_key(tui_window_t *w, unsigned key, unsigned mods, void *unused) {
   (void)w;
   (void)unused;

   if (key == TERMKEY_SYM_ESCAPE || key == 27) {
      tui_server_picker_close();

      return true;
   }

   if (key == TERMKEY_SYM_TAB || key == '\t') {
      int step = mods & TERMKEY_KEYMOD_SHIFT ? -1 : 1;
      tui_server_button_focus += step;

      if (tui_server_button_focus >= 3) {
         tui_server_button_focus = -1;
      }

      if (tui_server_button_focus < -1) {
         tui_server_button_focus = 2;
      }

      return true;
   }

   if (mods) {
      return false;
   }
   size_t n = tui_server_entries ? tui_server_entries->len : 0;

   if (key == TERMKEY_SYM_UP) {
      tui_server_button_focus = -1;

      if (tui_server_selected) {
         tui_server_selected--;
      }
   } else if (key == TERMKEY_SYM_DOWN) {
      tui_server_button_focus = -1;

      if (n && tui_server_selected + 1 < n) {
         tui_server_selected++;
      }
   } else if (key == TERMKEY_SYM_PAGEUP) {
      tui_server_button_focus = -1;
      size_t page = tui_rows() > 6 ? tui_rows() - 6 : 1;
      tui_server_selected = tui_server_selected > page ? tui_server_selected - page : 0;
   } else if (key == TERMKEY_SYM_PAGEDOWN) {
      tui_server_button_focus = -1;
      size_t page = tui_rows() > 6 ? tui_rows() - 6 : 1;

      if (n) {
         tui_server_selected = page < n - 1 - tui_server_selected ? tui_server_selected + page : n - 1;
      }
   } else if (key == TERMKEY_SYM_ENTER || key == 10 || key == ' ') {
      tui_server_picker_action(tui_server_button_focus < 0 ? 0 : tui_server_button_focus);
   } else {
      return false;
   }

   return true;
}

static bool tui_server_picker_mouse(tui_window_t *w, int row, int col, int button, int action, unsigned mods, void *unused) {
   (void)w;
   (void)mods;
   (void)unused;

   if (action != TERMKEY_MOUSE_PRESS) {
      return false;
   }

   if (button == 4 || button == 5) {
      if (button == 4 && tui_server_selected) {
         tui_server_selected--;
      }

      if (button == 5 && tui_server_entries && tui_server_selected + 1 < tui_server_entries->len) {
         tui_server_selected++;
      }

      return true;
   }

   if (button != 1) {
      return false;
   }

   if (row >= 4 && row < tui_rows() - 2) {
      size_t idx = tui_server_scroll + (size_t)(row - 4);

      if (tui_server_entries && idx < tui_server_entries->len) {
         tui_server_selected = idx;
         tui_server_button_focus = -1;
      }

      return true;
   }

   if (row == tui_rows() - 1) {
      for (int i = 0 ; i < 3 ; i++) {
         if (tui_server_buttons_start[i] && col >= tui_server_buttons_start[i] && col <= tui_server_buttons_end[i]) {
            tui_server_button_focus = i;
            tui_server_picker_action(i);

            return true;
         }
      }
   }

   return false;
}

extern bool rrclient_tui_script_picker_toggle(void);
extern bool rrclient_tui_identity_first_run(void);
extern void rrclient_tui_security_init(void);
extern void rrclient_tui_workspace_init(void);
extern void rrclient_tui_workspace_refresh(void);
extern void rrclient_tui_workspace_shutdown(void);
extern void rrclient_tui_script_picker_shutdown(void);
void rrclient_tui_server_picker_toggle(void) {
   if (ui_mode != UI_MODE_TUI) {
      return;
   }

   if (tui_window_find(RR_TUI_SERVER_PICKER)) {
      tui_server_picker_close();

      return;
   }
   if (rrclient_tui_script_picker_toggle()) return;
   tui_server_selected = tui_server_scroll = 0;
   tui_server_button_focus = -1;
   tui_server_notice[0] = 0;
   tui_window_t *window = tui_window_create(RR_TUI_SERVER_PICKER);

   if (!window) {
      return;
   }
   window->view_draw = tui_server_picker_draw;
   window->view_key = tui_server_picker_key;
   window->view_mouse = tui_server_picker_mouse;
   window->view_full_height = true;
   const char *selected = rrclient_selected_server();
   tui_server_reload();

   if (selected) {
      for (size_t i = 0 ; i < tui_server_entries->len ; i++) {
         tui_server_entry_t *e = g_ptr_array_index(tui_server_entries, i);

         if (!strcmp(selected, e->name)) {
            tui_server_selected = i;
            break;
         }
      }
   }
   tui_window_focus(RR_TUI_SERVER_PICKER);
   tui_redraw_request();
}

extern void rrclient_tui_audio_controls_toggle(void);
static bool rrclient_tui_audio_hotkey(tui_window_t *win, unsigned key, unsigned mods, void *data) {
   (void)win;
   (void)key;
   (void)mods;
   (void)data;
   rrclient_tui_audio_controls_toggle();
   return true;
}

static bool rrclient_tui_server_picker_hotkey(tui_window_t *win, unsigned key, unsigned mods, void *data) {
   (void)win;
   (void)key;
   (void)mods;
   (void)data;
   rrclient_tui_server_picker_toggle();

   return true;
}

/* Local SDR VFO controls use the core bridge APIs, never an rrserver. */
static char tui_local_vfo = 'A';
static bool tui_local_freq_edit;
static char tui_local_freq[32], tui_local_message[180];
static int tui_local_focus = -1;
static void tui_local_sdr_draw(tui_window_t *,int,int,void *);
static unsigned tui_local_digit=3;
static int tui_local_freq_row,tui_local_freq_col,tui_local_buttons_row,tui_local_plot_rows,tui_local_plot_cols;
static unsigned tui_local_zoom;
static bool tui_local_zoom_known;
static uint64_t tui_local_zoom_span;
static gint64 tui_local_zoom_requested;
static rr_bridge_spectrum_row tui_local_history[256];
static unsigned tui_local_head,tui_local_history_count;
static gint64 tui_local_last_row,tui_local_last_draw;
static rr_event_token_t tui_local_rf_token;
static const uint64_t tui_local_steps[]={100000000,10000000,1000000,100000,10000,1000,100,10,1};
static void tui_local_clear_rf(void){
   tui_local_head=tui_local_history_count=0;tui_local_last_row=0;
}
static void tui_local_rf_row(const rr_event_message_t *message,void *unused){
   (void)unused;
   if(!message||!message->payload||message->length!=sizeof(rr_bridge_spectrum_event)){
      return;
   }
   const rr_bridge_spectrum_event *p=message->payload;
   if(p->vfo!=rrclient_room_active_vfo("&local-sdr")||!rr_bridge_spectrum_valid(&p->row)){
      return;
   }
   if(tui_local_vfo!=p->vfo){tui_local_vfo=p->vfo;tui_local_clear_rf();tui_local_zoom=0;tui_local_zoom_known=false;tui_local_zoom_requested=0;tui_local_freq_edit=false;}
   gint64 now=g_get_monotonic_time();
   uint64_t span=p->row.high_hz-p->row.low_hz;
   if(tui_local_zoom_requested){
      uint64_t tolerance=MAX(UINT64_C(2),tui_local_zoom_span/100);
      if((span>tui_local_zoom_span?span-tui_local_zoom_span:tui_local_zoom_span-span)>tolerance&&now-tui_local_zoom_requested<2000000){
         return;
      }
      tui_local_zoom_requested=0;
   }
   if(!tui_local_zoom_known){
      /* Kiwi starts at zoom 6; infer its current zoom from the actual RF
       * span and the receiver's published coverage, not the UI default. */
      unsigned zoom=0;
      /* A normal Kiwi covers 30 MHz, with some models covering 32 MHz.
       * Pick the nearest power-of-two span. */
      uint64_t full=30000000;
      while(zoom<14&&full>span+span/2){
         full=(full+1)/2;
         zoom++;
      }
      tui_local_zoom=zoom;
      tui_local_zoom_known=true;
   }
   if(tui_local_history_count){
      const rr_bridge_spectrum_row *prev=&tui_local_history[(tui_local_head+255)%256];
      if(prev->low_hz!=p->row.low_hz||prev->high_hz!=p->row.high_hz||now-tui_local_last_row>5000000){
         tui_local_clear_rf();
      }
   }
   tui_local_history[tui_local_head]=p->row;
   tui_local_head=(tui_local_head+1)%256;
   tui_local_history_count=MIN(tui_local_history_count+1,256u);
   tui_local_last_row=now;
   if(now-tui_local_last_draw>=100000){
      tui_local_last_draw=now;
      tui_window_t *w=tui_active_window();
      if(w&&w->view_draw==tui_local_sdr_draw){
         tui_redraw_request();
      }
   }
}
static uint32_t tui_local_palette(unsigned power){
   /* Fixed color ramp; avoid per-row normalization hiding weak signals. */
   static const unsigned rgb[][3]={{12,20,43},{24,45,105},{22,112,165},{29,190,135},{238,220,53},{255,100,35},{255,245,220}};
   unsigned k=power*6/255,rem=power*6%255;
   if(k>=6)return 0xfff5dc;
   unsigned r=(rgb[k][0]*(255-rem)+rgb[k+1][0]*rem)/255;
   unsigned g=(rgb[k][1]*(255-rem)+rgb[k+1][1]*rem)/255;
   unsigned b=(rgb[k][2]*(255-rem)+rgb[k+1][2]*rem)/255;
   return (r<<16)|(g<<8)|b;
}
enum {
   LOCAL_DEC, LOCAL_INC, LOCAL_MODE, LOCAL_FREQ, LOCAL_VFO, LOCAL_BACK, LOCAL_BUTTONS
};
static int tui_local_button_start[LOCAL_BUTTONS], tui_local_button_end[LOCAL_BUTTONS];
static char tui_local_vfos[26];
static size_t tui_local_count;

static void tui_local_update_vfos(void) {
   char names[26][64];
   tui_local_count = rrclient_local_bridge_streams(names, tui_local_vfos, G_N_ELEMENTS(tui_local_vfos));

   if (tui_local_count > G_N_ELEMENTS(tui_local_vfos)) {
      tui_local_count = G_N_ELEMENTS(tui_local_vfos);
   }

   if (!tui_local_count) {
      return;
   }
   char active=rrclient_room_active_vfo("&local-sdr");
   if(tui_local_vfo!=active){
      tui_local_vfo=active;tui_local_clear_rf();tui_local_zoom=0;tui_local_zoom_known=false;tui_local_zoom_requested=0;
      tui_local_freq_edit=false;
   }
}

static void tui_local_select_vfo(char vfo) {
   if(!rrclient_local_bridge_select_vfo(vfo))return;
   tui_local_update_vfos();
   tui_redraw_request();
}

static bool tui_local_tune_delta(int64_t delta) {
   uint64_t hz = 0;

   if (!rrclient_local_bridge_state_vfo(tui_local_vfo, &hz, NULL)) {
      return false;
   }

   if (delta < 0 && hz < (uint64_t) (-delta)) {
      return false;
   }
   uint64_t target = delta < 0 ? hz - (uint64_t) (-delta) : hz + (uint64_t) delta;

   return rrclient_local_bridge_set_frequency(tui_local_vfo, target);
}

static bool tui_local_set_frequency(void) {
   char *end = NULL;
   double mhz = g_ascii_strtod(tui_local_freq, &end);

   if (!tui_local_freq[0] || !end || *end || mhz <= 0.0 || mhz >= 100000.0) {
      g_strlcpy(tui_local_message, "Enter a valid frequency in MHz, e.g. 14.074000.", sizeof(tui_local_message));

      return false;
   }
   uint64_t hz = (uint64_t) (mhz * 1000000.0 + 0.5);

   if (!rrclient_local_bridge_set_frequency(tui_local_vfo, hz)) {
      g_strlcpy(tui_local_message, "Tune rejected; see local SDR log for receiver limits.", sizeof(tui_local_message));

      return false;
   }
   g_strlcpy(tui_local_message, "Frequency change requested.", sizeof(tui_local_message));

   return true;
}

static void tui_local_start_frequency(void) {
   uint64_t hz = 0;

   if (!rrclient_local_bridge_state_vfo(tui_local_vfo, &hz, NULL)) {
      return;
   }
   g_snprintf(tui_local_freq, sizeof(tui_local_freq), "%.6f", (double)hz / 1000000.0);
   tui_local_freq_edit = true;
}

static void tui_local_cycle_mode(void) {
   uint64_t hz = 0;
   const char *mode = NULL;

   if (!rrclient_local_bridge_state_vfo(tui_local_vfo, &hz, &mode)) {
      return;
   }
   static const char *modes[] = {
      "USB", "LSB", "AM", "CW", "FM"
   };
   unsigned i = 0;
   while (i < G_N_ELEMENTS(modes) && g_ascii_strcasecmp(modes[i], mode ? mode : "")) {
      i++;
   }
   const char *next = modes[(i + 1) % G_N_ELEMENTS(modes)];

   if (!rrclient_local_bridge_set_mode(tui_local_vfo, next)) {
      g_snprintf(tui_local_message, sizeof(tui_local_message), "%s unsupported; check bridge capabilities.", next);
   } else {
      g_snprintf(tui_local_message, sizeof(tui_local_message), "Requested mode %s.", next);
   }
}

static void tui_local_action(int action) {
   switch (action) {
      case LOCAL_DEC:
      case LOCAL_INC: {
         if (!tui_local_tune_delta(action == LOCAL_DEC ? -1000 : 1000)) {
            g_strlcpy(tui_local_message, "Unable to step frequency; see local SDR log.", sizeof(tui_local_message));
         }
         break;
      }
      case LOCAL_MODE: {
         tui_local_cycle_mode();
         break;
      }
      case LOCAL_FREQ: {
         tui_local_start_frequency();
         break;
      }
      case LOCAL_VFO: {
         if (tui_local_count) {
            size_t i = 0;

            for ( ; i < tui_local_count ; i++) {
               if (tui_local_vfos[i] == tui_local_vfo) {
                  break;
               }
            }

            tui_local_select_vfo(tui_local_vfos[(i + 1) % tui_local_count]);
         }
         break;
      }
      case LOCAL_BACK: {
         tui_window_focus("SDR Directory");
         break;
      }
   }
}

static void tui_local_sdr_draw(tui_window_t *win,int rows,int cols,void *unused){
   (void)win;(void)unused;
   if(rows<10||cols<24)return;
   tui_local_update_vfos();
   int height=rows-2,width=cols-1;
   rrui_canvas_t *c=rrui_canvas_new(width,height);if(!c)return;
   /* Reserve roughly one quarter for controls, but keep all controls
    * accessible in shorter terminals. */
   int footer=MAX(7,height/4);
   int plot=MAX(1,height-footer-2);
   tui_local_plot_rows=plot;tui_local_plot_cols=width;
   rr_local_bridge_ui_state_t state={0};
   rrclient_local_bridge_ui_state_vfo(tui_local_vfo,&state);
   uint64_t hz=state.frequency_hz;
   char label[256];
   g_snprintf(label,sizeof(label),"VFO %c | %s | %s",tui_local_vfo,state.receiver_name[0]?state.receiver_name:"Local SDR",state.receiver_location);
   rrui_canvas_text(c,0,0,width,label,0x8baab8);
   bool fresh=state.connected&&tui_local_history_count&&g_get_monotonic_time()-tui_local_last_row<5000000;
   if(fresh){
      const rr_bridge_spectrum_row *latest=&tui_local_history[(tui_local_head+255)%256];
      for(int y=0;y<plot&&y<(int)tui_local_history_count;y++){
         const rr_bridge_spectrum_row *row=&tui_local_history[(tui_local_head+255-(unsigned)y)%256];
         for(int x=0;x<width;x++){
            unsigned first=(unsigned)((uint64_t)x*RR_BRIDGE_SPECTRUM_BINS/width);
            unsigned last=MIN(RR_BRIDGE_SPECTRUM_BINS,(unsigned)((uint64_t)(x+1)*RR_BRIDGE_SPECTRUM_BINS/width));
            unsigned power=row->power[MIN(first,RR_BRIDGE_SPECTRUM_BINS-1)];
            for(unsigned j=first+1;j<last;j++)power=MAX(power,row->power[j]);
            rrui_canvas_put(c,x,y+2,0x2588,tui_local_palette(power));
         }
      }
      for(int x=0;x<width;x++)rrui_canvas_put(c,x,1,0x2500,0x8baab8);
      for(int tick=0;tick<=4;tick++){
         uint64_t f=latest->low_hz+(latest->high_hz-latest->low_hz)*tick/4;
         g_snprintf(label,sizeof(label),latest->high_hz-latest->low_hz<10000?"%.6f":"%.3f",(double)f/1000000.);
         int x=CLAMP((width-1)*tick/4-(tick==4?(int)strlen(label)-1:0),0,width-1);
         rrui_canvas_text(c,x,1,width-x,label,0xc2d1da);
      }
      if(hz>=latest->low_hz&&hz<latest->high_hz){
         int x=(int)((hz-latest->low_hz)*width/(latest->high_hz-latest->low_hz));
         rrui_canvas_put(c,x,1,0x25bc,0xffff70);
      }
   }else rrui_canvas_text(c,2,2,width-2,state.connected?"Waiting for RF waterfall...":"Waiting for receiver...",0xc2d1da);
   int y=plot+2;
   for(int x=0;x<width;x++)rrui_canvas_put(c,x,y,0x2500,0x8baab8);
   g_snprintf(label,sizeof(label),"VFO %c   %s   %s   Zoom %u",tui_local_vfo,state.mode[0]?state.mode:"---",state.connected?"Online (RX only)":"Connecting",tui_local_zoom);
   rrui_canvas_text(c,1,y+1,width-1,label,0xc2d1da);
   char digits[16];g_snprintf(digits,sizeof(digits),"%03llu.%06llu",(unsigned long long)(hz/1000000),(unsigned long long)(hz%1000000));
   int fx=MAX(1,(width-14)/2);
   tui_local_freq_row=y+4;tui_local_freq_col=fx+1;
   if(tui_local_freq_edit){
      g_snprintf(label,sizeof(label),"MHz: %s_",tui_local_freq);
      rrui_canvas_text(c,1,y+2,width-1,label,0xffff70);
   }else{
      for(unsigned d=0;d<9;d++){
         int x=fx+d+(d>=3);
         uint32_t color=tui_local_focus==-1&&d==tui_local_digit?0xffff70:0x53cbd5;
         rrui_canvas_put(c,x,y+2,0x25b2,color);
         rrui_canvas_put(c,x,y+3,digits[d+(d>=3)],color);
         rrui_canvas_put(c,x,y+4,0x25bc,color);
      }
      rrui_canvas_put(c,fx+3,y+3,'.',0xc2d1da);
      rrui_canvas_text(c,fx+11,y+3,3,"MHz",0xc2d1da);
      tui_local_freq_row=y+5;
   }
   const char *buttons[]={"-1k","+1k","Mode","Freq","VFO","Directory"};
   int bx=0,by=height-2;tui_local_buttons_row=by+2;
   for(int i=0;i<LOCAL_BUTTONS;i++){
      g_snprintf(label,sizeof(label),"[%s]",buttons[i]);int n=strlen(label);
      tui_local_button_start[i]=tui_local_button_end[i]=0;
      if(bx+n<=width){
         rrui_canvas_text(c,bx,by,n,label,i==tui_local_focus?0xffff70:0x8baab8);
         tui_local_button_start[i]=bx+1;tui_local_button_end[i]=bx+n;
      }
      bx+=n+1;
   }
   rrui_canvas_text(c,0,height-1,width,tui_local_message[0]?tui_local_message:"Left/Right digit | Up/Down tune | Tab controls | +/- zoom (waterfall wheel) | Esc back",0xc2d1da);
   rrui_canvas_draw(c,2,1);rrui_canvas_free(c);
}

static void tui_local_change_zoom(int delta,int col){
   rr_local_bridge_ui_state_t state={0};
   if(!rrclient_local_bridge_ui_state_vfo(tui_local_vfo,&state)||!state.connected||!state.waterfall){
      return;
   }
   if(!tui_local_zoom_known||!tui_local_history_count){
      return;
   }
   unsigned zoom=(unsigned)CLAMP((int)tui_local_zoom+delta,(int)state.min_zoom,(int)state.max_zoom);
   if(zoom==tui_local_zoom){
      return;
   }
   const rr_bridge_spectrum_row *row=&tui_local_history[(tui_local_head+255)%256];
   uint64_t span=row->high_hz-row->low_hz;
   uint64_t next=zoom>tui_local_zoom?MAX(UINT64_C(1),span>>(zoom-tui_local_zoom)):span<<(tui_local_zoom-zoom);
   uint64_t center=state.frequency_hz;
   if(col>0&&tui_local_plot_cols>0){
      /* Keep the RF frequency under the pointer stationary as the span
       * changes. The bridge clamps the resulting window at band edges. */
      double fraction=(CLAMP(col-1,0,tui_local_plot_cols-1)+0.5)/tui_local_plot_cols;
      double anchor=row->low_hz+fraction*span;
      double desired=anchor+(0.5-fraction)*next;
      center=(uint64_t)MAX(1.0,desired);
   }
   if(rrclient_local_bridge_request_waterfall_vfo(tui_local_vfo,true,zoom,center)){
      tui_local_zoom=zoom;
      tui_local_zoom_span=next;
      tui_local_zoom_requested=g_get_monotonic_time();
      tui_local_clear_rf();
      tui_redraw_request();
   }
}
static bool tui_local_sdr_key(tui_window_t *win, unsigned key, unsigned mods, void *unused) {
   (void)win;
   (void)unused;

   if (tui_local_freq_edit) {
      size_t len = strlen(tui_local_freq);

      if (key == TERMKEY_SYM_ESCAPE || key == 27) {
         tui_local_freq_edit = false;
      } else if (key == TERMKEY_SYM_ENTER || key == 10) {
         if (tui_local_set_frequency()) {
            tui_local_freq_edit = false;
         }
      } else if (key == TERMKEY_SYM_BACKSPACE || key == 8 || key == 127) {
         if (len) {
            tui_local_freq[len - 1] = 0;
         }
      } else if (((key >= '0' && key <= '9') || key == '.') && len + 1 < sizeof(tui_local_freq)) {
         tui_local_freq[len] = (char)key;
         tui_local_freq[len + 1] = 0;
      }

      return true;
   }

   if (key == TERMKEY_SYM_ESCAPE || key == 27) {
      tui_window_focus("SDR Directory");

      return true;
   }

   if (key == TERMKEY_SYM_TAB || key == '\t') {
      tui_local_focus += mods & TERMKEY_KEYMOD_SHIFT ? -1 : 1;

      if (tui_local_focus >= LOCAL_BUTTONS) {
         tui_local_focus = -1;
      }

      if (tui_local_focus < -1) {
         tui_local_focus = LOCAL_BUTTONS - 1;
      }

      return true;
   }

   if (mods) {
      return false;
   }

   if(tui_local_focus==-1&&(key==TERMKEY_SYM_LEFT||key==TERMKEY_SYM_RIGHT)){
      tui_local_digit=(unsigned)CLAMP((int)tui_local_digit+(key==TERMKEY_SYM_RIGHT?1:-1),0,8);return true;
   }
   if(tui_local_focus==-1&&(key==TERMKEY_SYM_UP||key==TERMKEY_SYM_DOWN)){
      int64_t step=(int64_t)tui_local_steps[tui_local_digit];
      tui_local_tune_delta(key==TERMKEY_SYM_UP?step:-step);return true;
   }
   if (key == TERMKEY_SYM_UP || key == TERMKEY_SYM_DOWN) {
      if (tui_local_count) {
         size_t i = 0;

         for ( ; i < tui_local_count ; i++) {
            if (tui_local_vfos[i] == tui_local_vfo) {
               break;
            }
         }

         i = key == TERMKEY_SYM_DOWN ? (i + 1) % tui_local_count : (i + tui_local_count - 1) % tui_local_count;
         tui_local_select_vfo(tui_local_vfos[i]);
      }
   } else if (key == TERMKEY_SYM_ENTER || key == 10 || key == ' ') {
      if (tui_local_focus >= 0) {
         tui_local_action(tui_local_focus);
      }
   } else if (key == '-' || key == '+') {
      tui_local_change_zoom(key=='+'?1:-1,0);
   } else if (key == 'f' || key == 'F') {
      tui_local_action(LOCAL_FREQ);
   } else if (key == 'm' || key == 'M') {
      tui_local_action(LOCAL_MODE);
   } else if (key == 'v' || key == 'V') {
      tui_local_action(LOCAL_VFO);
   } else {
      return false;
   }

   return true;
}

static bool tui_local_sdr_mouse(tui_window_t *win,int row,int col,int button,int action,unsigned mods,void *unused){
   (void)win;(void)mods;(void)unused;
   if(action!=TERMKEY_MOUSE_PRESS)return false;
   if(row>=3&&row<4+tui_local_plot_rows){
      if(button==4||button==5){
         tui_local_change_zoom(button==4?1:-1,col);
         return true;
      }
      if(button==1&&tui_local_history_count&&g_get_monotonic_time()-tui_local_last_row<5000000){
         const rr_bridge_spectrum_row *latest=&tui_local_history[(tui_local_head+255)%256];
         int x=CLAMP(col-1,0,tui_local_plot_cols-1);
         uint64_t hz=latest->low_hz+(latest->high_hz-latest->low_hz)*(2*x+1)/(2*tui_local_plot_cols);
         rrclient_local_bridge_set_frequency(tui_local_vfo,hz);return true;
      }
   }
   if(!tui_local_freq_edit&&row>=tui_local_freq_row-1&&row<=tui_local_freq_row+1){
      int d=col-tui_local_freq_col;
      if(d==3)return true;
      if(d>3)d--;
      if(d>=0&&d<9){
         tui_local_digit=(unsigned)d;tui_local_focus=-1;
         int64_t step=(int64_t)tui_local_steps[d];
         if(button==4||(button==1&&row<tui_local_freq_row))tui_local_tune_delta(step);
         else if(button==5||(button==1&&row>tui_local_freq_row))tui_local_tune_delta(-step);
         return true;
      }
   }
   if(button==1&&row==tui_local_buttons_row){
      for(int i=0;i<LOCAL_BUTTONS;i++)if(tui_local_button_start[i]&&col>=tui_local_button_start[i]&&col<=tui_local_button_end[i]){
         tui_local_focus=i;tui_local_action(i);return true;
      }
   }
   return false;
}

void rrclient_tui_local_sdr_open(void) {
   if (ui_mode != UI_MODE_TUI) {
      return;
   }
   tui_window_t *window = tui_window_create("&local-sdr");

   if (!window) {
      return;
   }
   if(!tui_local_rf_token){
      tui_local_rf_token=event_on_message_domain_token(RR_BRIDGE_SPECTRUM_EVENT,tui_local_rf_row,NULL,"core");
   }
   tui_local_clear_rf();
   tui_local_zoom=0;tui_local_zoom_known=false;tui_local_zoom_requested=0;
   window->view_draw = tui_local_sdr_draw;
   window->view_key = tui_local_sdr_key;
   window->view_mouse = tui_local_sdr_mouse;
   window->view_full_height = true;
   tui_local_freq_edit = false;
   tui_local_focus = -1;
   tui_local_message[0] = 0;
   tui_window_focus("&local-sdr");
   tui_redraw_request();
}

static bool rrclient_sdr_hotkey(tui_window_t *win, unsigned key, unsigned mods, void *user_data) {
   (void)win;
   (void)key;
   (void)mods;
   (void)user_data;

   if (tui_window_focus("SDR Directory")) {
      sdr_rebuild(false);
      tui_redraw_request();

      return true;
   }

   return false;
}

static bool rrclient_server_hotkey(tui_window_t *win, unsigned key, unsigned modifiers, void *user_data) {
   (void)key;
   (void)user_data;

   if (modifiers & TERMKEY_KEYMOD_SHIFT) {
      return false;
   }

   if ((modifiers & TERMKEY_KEYMOD_ALT) && key != 's' && key != 'S') {
      return false;
   }

   /* Directory browsing is a shared view too: its selected server controls proxy receiver connections, just as Status/Client Log/Host Log do. */
   return rrclient_connection_cycle_status(win && (rrclient_window_is_shared(win->title) || !strcasecmp(win->title, "SDR Directory") || !strcasecmp(win->title, "Admin")));
}

static void *tui_nick_completion;
static bool rrclient_tui_chat_completion(tui_window_t *win) {
   (void)win;
   extern char input_buf[TUI_INPUTLEN];
   extern int tui_cursor_pos, tui_input_len;
   size_t cursor = (size_t)tui_cursor_pos;
   char *result = client_chat_complete(input_buf, &cursor, &tui_nick_completion);

   if (!result) {
      return false;
   }
   bool fits = strlen(result) < TUI_INPUTLEN;

   if (fits) {
      strcpy(input_buf, result);
      tui_input_len = (int)strlen(result);
      tui_cursor_pos = (int)cursor;
   }
   free(result);

   return fits;
}

static bool rrclient_ptt_hotkey(tui_window_t *win, unsigned key, unsigned modifiers, void *user_data) {
   (void)ui_active_window_name();
   (void)win;
   (void)key;
   (void)modifiers;
   (void)user_data;

   if (frontend_ops() && frontend_ops()->ptt_hotkey_toggle) {
      return frontend_ops()->ptt_hotkey_toggle();
   }

   if (!rrclient_connection_context() || ws_connected != 1) {
      return true;
   }
   char vfo[2] = {
      vfo_state_get_active(), '\0'
   };
   bool active = vfo_state_get_bool(vfo, "cat.state.ptt", false);
   rrclient_send_ptt_command(rrclient_connection_context(), vfo, !active, ui_active_window_name());

   return true;
}

void shutdown_app(int signum) {
   if (signum > 0) {
      Log(LOG_INFO, "core", "Shutting down due to signal %d", signum);
   } else {
      Log(LOG_INFO, "core", "Shutting down by user request");
   }
   // Signal the main loop that we are dying
   dying = true;
}

#ifdef USE_MONGOOSE
// GLib owns the wait between sweeps. Blocking in Mongoose here also blocks
// GTK rendering and input; TUI uses this same nonblocking transport callback.
#define MG_POLL_INTERVAL_MS 10

void poll_mongoose_init(void) {
   rr_core_timeout(MG_POLL_INTERVAL_MS, G_PRIORITY_HIGH, ws_poll_cb);
}

#endif // USE_MONGOOSE

// TUI 1hz clock: updates now, refreshes statusbar/clock, handles shutdown.
// Over SSH (SSH_TTY set) the clock is HH:MM, so we only repaint when the
// minute actually changes instead of once a second.
static gboolean tui_clock_cb_real(gpointer user_data) {
   rrclient_media_room_selected(ui_active_window_name());
   now = time(NULL);

   if (dying) {
      rrclient_cleanup();
      rrclient_database_shutdown();

      return G_SOURCE_REMOVE;
   }

   sdr_tick();
   rrclient_tui_workspace_refresh();
   // Over SSH: repaint only when the minute turns over (or on first tick)
   static time_t last_repaint = 0;

   /* SIGWINCH queues a full redraw because terminal I/O is not signal-safe. Consume it before the SSH clock throttle so resized remote terminals do not keep a
    * partially rendered screen until the next minute. */
   if (tui_redraw_if_pending()) {
      last_repaint = now;

      return G_SOURCE_CONTINUE;
   }

   if (tui_over_ssh && last_repaint != 0 && (now / 60) == (last_repaint / 60)) {
      last_repaint = now;

      return G_SOURCE_CONTINUE;
   }
   last_repaint = now;

   tui_window_t *tw = tui_active_window();
   tui_refresh_sb_window();
   tui_refresh_sb_vfo();
   tui_update_status(tw, "%s %s", sb_online, sb_window);
   tui_redraw_clock();

   /* Local receiver state is updated asynchronously by rrbridge IPC. */
   if (tw && tw->view_draw == tui_local_sdr_draw) {
      tui_redraw_request();
   }

   return G_SOURCE_CONTINUE;
}

static void rrclient_tui_host_log_frame(const char *event, const void *data, size_t len, rrconn_t *cptr, void *user) {
   (void)event;
   (void)cptr;
   (void)user;

   if (dying || !data || len < RR_LOGFRAME_HDR_LEN) {
      return;
   }

   const uint8_t *payload = (const uint8_t *)data;
   char subsys[sizeof(((struct rr_logframe *) 0)->subsys) + 1];
   memcpy(subsys, payload + 1, sizeof(subsys) - 1);
   subsys[sizeof(subsys) - 1] = '\0';

   size_t message_len = strnlen((const char *)payload + RR_LOGFRAME_HDR_LEN, len - RR_LOGFRAME_HDR_LEN);

   if (message_len == 0) {
      return;
   }

   char message[4096];
   size_t src_pos = 0;
   size_t out = 0;
   const unsigned char *src = payload + RR_LOGFRAME_HDR_LEN;
   while (out + 1 < sizeof(message) && src_pos < message_len) {
      unsigned char ch = src[src_pos++];

      if (ch == '\033' && src_pos < message_len && src[src_pos] == '[') {
         // Drop terminal CSI sequences from remote log text.  The TUI adds
         // its own SGR sequences and remote logs must never move the cursor.
         while (src_pos < message_len) {
            unsigned char end = src[src_pos++];

            if (end >= '@' && end <= '~') {
               break;
            }
         }
         continue;
      }

      if (ch < 32 || ch == 127) {
         ch = ' ';
      }
      message[out++] = (char)ch;
   }
   message[out] = '\0';

   ui_print("host log", "%s<%s.%s> %s", get_chat_ts(now), subsys, log_priority_to_str((logpriority_t) payload[0]), message);
}

/* The three TUI system tabs share all servers; ui_print labels each source. GTK has its own log callbacks. */
static bool rrclient_tui_log_print_va(logpriority_t priority, const char *subsys, const char *fmt, va_list ap) {
   static bool log_printing = false;

   /* tui_vprint redraws the top line.  The top-line renderer reads config, and cfg_get() logs at verbose levels, so allowing that callback back into this
    * function recurses until the stack is exhausted. */
   if (log_printing) {
      return true;
   }

   if (!fmt || dying || debug_filter(subsys, priority)) {
      return true;
   }

   char message[4096];
   va_list copy;
   va_copy(copy, ap);
   vsnprintf(message, sizeof(message), fmt, copy);
   va_end(copy);

   // Log messages can originate in subprocesses and may contain carriage
   // returns or terminal escapes.  Keep them as one safe TUI line.
   for (size_t i = 0 ; message[i] ; i++) {
      unsigned char ch = (unsigned char)message[i];

      if (ch < 32 || ch == 127) {
         message[i] = ' ';
      }
   }

   log_printing = true;
   ui_print("client log", "%s<%s.%s> %s", get_chat_ts(now), subsys ? subsys : "core", log_priority_to_str(priority), message);
   log_printing = false;

   return false;
}

struct talk_msg_event_data {
   char from[128];
   char data[4096];
   char target[128];
   char msg_type[32];
   time_t ts;
};

static void rrclient_handle_talk_msg_event(const char *event, void *data, rrconn_t *cptr, void *user) {
   struct talk_msg_event_data *tmed = (struct talk_msg_event_data *)data;

   if (!tmed || !tmed->from[0] || !tmed->data[0]) {
      return;
   }

   if (strcasecmp(tmed->msg_type, "action") == 0) {
      ui_print(tmed->target[0] ? tmed->target : NULL, "%s\00309* \00311%s\017 %s", get_chat_ts(tmed->ts), tmed->from, tmed->data);
   } else {
      ui_print(tmed->target[0] ? tmed->target : NULL, "%s\00314<\00310%s\00314>\017 %s\017", get_chat_ts(tmed->ts), tmed->from, tmed->data);
   }
}

bool rrclient_cleanup(void) {
   // Idempotent: this is reachable from the timeouts, the signal handler, and
   // after gtk_main()/g_main_loop_run() return.  Guard so GTK is not torn down
   // twice (which triggers gtk_main_quit "main_loops != NULL").
   static bool cleaned_up = false;

   if (cleaned_up) {
      return true;
   }
   cleaned_up = true;
   rrclient_identity_shutdown();
   rrclient_exec_shutdown();
   rrclient_quit_servers("Client exiting");
   rrclient_flush_quit();
   tui_set_completion_handler(NULL);
   client_chat_completion_free(tui_nick_completion);
   tui_nick_completion = NULL;

   tui_over_ssh = tui_is_over_ssh();

   // Let the frontend module tear down first: it must unregister its event
   // tokens and idle/timer sources before the event bus goes away.
   if (frontend_module) {
      if (rr_unload_module(frontend_module->mod_name)) {
         frontend_module = NULL;
      } else {
         Log(LOG_CRIT, "module", "Frontend module %s remains mapped because shutdown was incomplete", frontend_module->mod_name);
      }
   }

   rrclient_receiver_connect_shutdown();
   receiver_directory_shutdown();

   /* Stop/join the RX mixer and close its duplicated playback descriptor before destroying the fwdsp processes it writes to. */
   ws_audio_shutdown();

   // Stop all fwdsp children while their Mongoose wrappers and logging are
   // still alive. This also releases encoders retained by fwdsp.hangtime.
   fwdsp_fini();

   // Shut down sockets
#ifdef USE_MONGOOSE
   // NOTE: this fires the "disconnected" event, whose handlers still read
   // cfg (e.g. ui.auto-show-userlist), so cfg must be freed AFTER this.
   ws_fini(&mgr);
#endif // defined(USE_MONGOOSE)

   connman_shutdown();
   cat_pty_shutdown();
#ifdef RR_HAVE_RUSTYSCRIPT
   rrclient_script_shutdown();
#endif
   event_shutdown();

   // Persist the running config (including window placements learned while
   // we ran) if ui.save-on-exit says so. Backed up via cfg_save's .old logic.
   if (cfg && cfg_get_bool("ui.save-on-exit", false) && !rrclient_database_restart_required()) {
      Log(LOG_INFO, "config", "ui.save-on-exit: saving config overrides to client.db");

      if (!rrclient_config_save()) {
         Log(LOG_WARN, "config", "Unable to save configuration overrides to client.db");
      }
   }

   free((void *)cfg_log_audio);
   cfg_log_audio = NULL;
   cfg_fini();

   if (ui_mode == UI_MODE_TUI) {
      sdr_shutdown();
      rrclient_tui_workspace_shutdown();
      rrclient_tui_script_picker_shutdown();
      tui_fini();
   }

   logger_end();
   exit(0);

   return false;
}

static int rrclient_reset_local(const char *option,bool factory) {
   char *identity=g_build_filename(g_get_user_config_dir(),"rustyrig","identity",NULL);
   char *config=g_build_filename(g_get_user_config_dir(),"rustyrig",NULL);
   char *state=g_build_filename(g_get_user_state_dir(),"rustyrig",NULL);
   char *cache=g_build_filename(g_get_user_cache_dir(),"rustyrig",NULL);
   char *data=g_build_filename(g_get_user_data_dir(),"rustyrig",NULL);
   char *legacy_config=g_build_filename(g_get_home_dir(),".config","rrclient.cfg",NULL);
   char *legacy_data=g_build_filename(g_get_home_dir(),".local","share","rustyrig",NULL);
   char *legacy_app_config=g_build_filename(g_get_home_dir(),".config","rustyrig",NULL);
   char *recordings=g_build_filename(g_get_home_dir(),"rrclient-recordings",NULL);
   rr_reset_t reset={.preview=true};
   bool ok=true;
   for (unsigned pass=0;pass<2 && ok;pass++) {
      reset.preview=pass==0;
      if (factory) {
         const char *trees[]={config,state,cache,data,legacy_data,legacy_app_config,recordings};
         for (unsigned i=0;i<G_N_ELEMENTS(trees) && ok;i++) {
            ok=rr_reset_target(&reset,trees[i],true);
         }
         if (ok) { ok=rr_reset_target(&reset,legacy_config,false); }
      } else {
         ok=rr_reset_target(&reset,identity,true);
      }
      if (pass==0 && ok) { ok=rr_reset_confirm(option,"rrclient"); }
   }
   fprintf(stderr,ok?"Reset complete: %u files removed.\n":"Reset incomplete or cancelled: %u files removed.\n",reset.removed);
   g_free(identity);g_free(config);g_free(state);g_free(cache);g_free(data);
   g_free(legacy_config);g_free(legacy_data);g_free(legacy_app_config);g_free(recordings);
   return ok?EXIT_SUCCESS:EXIT_FAILURE;
}

void show_arg_help(int argc, char **argv) {
   printf("%s [-T] [-f config] [-s server] [-S] [--ignore-db-config] [--reset-db|--reset-certs|--factory-reset] [-h]\n", argv[0]);
   printf("\t-S\t\tssh mode (implies -T; throttle screen updates)\n");
   printf("\t-T\t\tTUI only mode (no X11)\n");
   printf("\t-f config\tChose an alternative configuration file\n");
   printf("\t-s server\tServer profile to autoconnect to on start\n");
   printf("\t--ignore-db-config\tSkip saved configuration overrides (still load profiles and themes)\n");
   printf("\t--reset-db\tReset client SQLite database (interactive confirmation) and exit\n");
   printf("\t--reset-certs / --reset-identity\tDelete local client identity keys and exit\n");
   printf("\t--factory-reset\tDelete local configuration, databases, identities, caches and recordings and exit\n");
   printf("\t-h\t\tHelp\n");
}

////////////////////////////
/* GTK runs on its own thread while core polling stays on the main thread. */
typedef struct {
   const rr_frontend_ops_t *ops;
   int argc;
   char **argv;
   GMutex mutex;
   GCond cond;
   bool ready, failed;
   bool core_stopped;
   GMainLoop *core_loop;
} rr_gtk_frontend_state_t;
static gpointer rr_gtk_frontend_thread(gpointer data) {
   rr_gtk_frontend_state_t *e = data;
   Log(LOG_INFO, "core.thread", "GTK worker starting tid=%p", (void *)g_thread_self());
   bool failed = e->ops->init(&e->argc, &e->argv);
   g_mutex_lock(&e->mutex);
   e->failed = failed;
   e->ready = true;
   g_cond_signal(&e->cond);
   g_mutex_unlock(&e->mutex);

   if (!failed) {
      e->ops->run();
      g_main_loop_quit(e->core_loop);
      g_mutex_lock(&e->mutex);
      /* Do not tear down GTK while a core event callback can still queue UI work. */
      while (!e->core_stopped) {
         g_cond_wait(&e->cond, &e->mutex);
      }
      g_mutex_unlock(&e->mutex);
   }

   if (e->ops->stop) {
      e->ops->stop();
   }

   return NULL;
}

int main(int argc, char *argv[]) {
   char *display = getenv("DISPLAY");
   char *autoconnect_server = NULL; // -s: server profile to connect to on start
   const char *reset_option=NULL;

   // Apply the hard-coded defaults from defconfig.c FIRST, so keys missing
   // from the user's config fall back to them.
   if (!default_cfg) {
      default_cfg = dict_new();
   }
   cfg_set_defaults(default_cfg, defcfg);


   int c;
   int digit_optind = 0;

   cfg = default_cfg;

#ifdef USE_COREDUMPS_CLIENT
   struct rlimit rl = {
      .rlim_cur = RLIM_INFINITY,
      .rlim_max = RLIM_INFINITY
   };
   setrlimit(RLIMIT_CORE, &rl);
#else
   struct rlimit rl = {
      0, 0
   };
   setrlimit(RLIMIT_CORE, &rl);
#endif // USE_COREDUMPS_CLIENT

   // Set a time stamp so logging will work
   now = time(NULL);
   update_timestamp();

   // The [modules] config section (after cfg_load) decides which frontend
   // modules to load; until then assume TUI.
   ui_mode = UI_MODE_TUI;

   // Let's do commandline parsing here
   // -T: Always force TUI (no X11)
   while (1) {
      int this_option_optind = optind ? optind : 1;
      int option_index = 0;
      static struct option long_options[] = {
         {
            "config", required_argument, 0, 'f'
         },
         {
            "tui", no_argument, 0, 'T'
         },
         {
            "server", required_argument, 0, 's'
         },
         {
            "ssh", no_argument, 0, 'S'
         },
         {
            "ignore-db-config", no_argument, 0, 1000
         },
         {
            "reset-db", no_argument, 0, 1001
         },
         {"reset-certs", no_argument, 0, 1002},
         {"reset-identity", no_argument, 0, 1003},
         {"factory-reset", no_argument, 0, 1004},
         {
            "help", no_argument, 0, 'h'
         },
         {
            0, 0, 0, 0
         }
      };

      c = getopt_long(argc, argv, "ThSf:s:", long_options, &option_index);

      if (c == -1) {
         break;
      }

      switch (c) {
         case 1001: case 1002: case 1003: case 1004: {
            const char *option=c==1001?"--reset-db":c==1002?"--reset-certs":
               c==1003?"--reset-identity":"--factory-reset";
            if (reset_option || !this_option_optind || strcmp(argv[this_option_optind],option)) {
               fprintf(stderr,"Specify exactly one full reset option.\n");
               return EXIT_FAILURE;
            }
            reset_option=option;
            break;
         }
         case 1000: {
            rrclient_config_set_ignore_db(true);
            break;
         }
         case 'f': {
            printf("Using config file: %s\n", optarg);
            config_file = strdup(optarg);
            break;
         }

         case 'S': {
            printf("Setting ssh mode!\n");
            tui_over_ssh = true;
            ui_mode = UI_MODE_TUI;
            break;
         }

         case 'h': {
            show_arg_help(argc, argv);
            exit(0);
            break;
         }

         case 'T': {
            ui_mode = UI_MODE_TUI;
            force_tui = true;
            break;
         }

         case 's': {
            printf("Autoconnect server: %s\n", optarg);
            autoconnect_server = strdup(optarg);
            break;
         }

         case '?': {
            break;
         }

         default: {
            printf("?? getopt returned character code 0%o ??\n", c);
         }
      }
   }

   if (optind < argc) {
      printf("non-option ARGV-elements: ");
      while (optind < argc) {
         printf("%s ", argv[optind++]);
      }
      printf("\n");
   }

   /* Maintenance exits without config, UI, transport or database startup. */
   if (reset_option) {
      if (config_file || autoconnect_server) {
         fprintf(stderr,"Reset uses standard local paths; do not combine with -f or -s.\n");
         return EXIT_FAILURE;
      }
      if (!strcmp(reset_option,"--reset-db")) { return rrclient_database_reset(); }
      return rrclient_reset_local(reset_option,!strcmp(reset_option,"--factory-reset"));
   }
   event_init();
   host_init();

   // add our configuration callbacks
   cfg_add_callback(NULL, "network:*", config_network_cb);
   config_fwdsp_init();

   if (!rr_serial_config_register()) {
      return EXIT_FAILURE;
   }
   cfg_modules_init();

   // Register config save callbacks so module-owned sections get saved.
   // cfg_servers_init() is weak: it lives in cfg.servers.c which is part of
   // the old IRC transport path and may be disabled in rules.mk.
   if (cfg_servers_init) {
      cfg_servers_init();
   }
   cfg_network_save_init();

   if (!rrclient_config_load()) {
      Log(LOG_CRIT, "config", "Unable to load client configuration");
      exit(1);
   }

   /* Check even when an existing database does not need the template yet. */
   const char *schema_path = "/var/lib/rustyrig/sql/sqlite.client.sql";
   const bool schema_missing = !g_file_test(schema_path, G_FILE_TEST_IS_REGULAR) || access(schema_path, R_OK) != 0;
   const char *schema_warning = NULL;

   if (schema_missing) {
      const char *source_schema = "sql/sqlite.client.sql";
      schema_warning = g_file_test(source_schema, G_FILE_TEST_IS_REGULAR) && access(source_schema, R_OK) == 0
         ? "Installed client SQL template /var/lib/rustyrig/sql/sqlite.client.sql is missing or unreadable; using source checkout fallback."
         : "Client SQL template /var/lib/rustyrig/sql/sqlite.client.sql is missing or unreadable (no source fallback). Reinstall rustyrig-client; new databases cannot be initialized.";
      fprintf(stderr, "WARNING: %s\n", schema_warning);
   }

   /* Load persistent overrides before selecting GTK/TUI, initializing the logger, or creating frontend modules. --ignore-db-config is a recovery boot: no DB
    * config overrides are applied; profiles/themes still load. */
   if (!rrclient_database_init()) {
      Log(LOG_WARN, "db", "Client database unavailable; using file configuration (saved profiles and themes unavailable)");
   } else {
      /* Config overrides are optional; they must not prevent startup. Profiles and themes remain independent of --ignore-db-config. */
      if (!rrclient_config_ignore_db() && !rrclient_database_config_overlay()) {
         Log(LOG_WARN, "db", "Saved config overrides could not be read; using rrclient.cfg defaults. Try --ignore-db-config for recovery");
      }

      if (!rrclient_database_load_profiles()) {
         Log(LOG_WARN, "db", "Unable to load saved server profiles");
      }

      if (!rrclient_database_load_themes()) {
         Log(LOG_WARN, "db", "Unable to load saved themes");
      }
   }
   rrclient_theme_apply();

   /* Choose the execution topology once, after loading configuration and before installing the core event domain, reactor and timer sources. */
   const bool threaded_gtk = !force_tui && cfg_modules_get(0, NULL) != NULL;

   if (threaded_gtk) {
      rr_core_context = g_main_context_new();
      fwdsp_set_reactor_context(rr_core_context);
      logger_set_drain_context(rr_core_context);
   }

   if (!event_domain_register("core", rrclient_core_dispatch_event, rr_core_context ? rr_core_context : g_main_context_default())) {
      Log(LOG_WARN, "core.event", "Unable to register core event delivery domain");
   }
   Log(LOG_INFO, "core.thread", "Frontend threading model: %s", threaded_gtk ? "dedicated GTK thread" : "TUI");
#ifndef _WIN32
   const char *module_dir = cfg_get("path.modules");
   struct stat module_stat;

   if (!module_dir || module_dir[0] != '/' || stat(module_dir, &module_stat) != 0 || !S_ISDIR(module_stat.st_mode)) {
      Log(LOG_CRIT, "modules", "Invalid installed module directory '%s'. Install rrclient modules and set path.modules to an existing absolute directory.",
         module_dir ? module_dir : "(unset)");

      return EXIT_FAILURE;
   }
#endif

   printf("Loading config %s\n", config_file);

   // -s overrides server.auto-connect so we connect to the given profile on start
   if (autoconnect_server) {
      dict_add(cfg, "server.auto-connect", autoconnect_server);
      free((void *)autoconnect_server);
      autoconnect_server = NULL;
   }

   // apply some global configuration
   char *logfile = cfg_get_path("log.file");
   /* Keep trace traffic on disk rather than stdout/the GTK console. An explicitly configured file path continues to take precedence. */
   char *default_logfile = NULL;
   const char *log_target = logfile;

   if (!log_target || !*log_target || !strcmp(log_target, "-")) {
      default_logfile = g_build_filename(g_get_home_dir(), "rrclient.log", NULL);
      log_target = default_logfile;
   }
   logger_init(log_target, (ui_mode == UI_MODE_TUI));
   g_free(default_logfile);

   if (logfile) {
      free(logfile);
      logfile = NULL;
   }

   /////////////////////////////////////////
// Store some oft used config settings //
/////////////////////////////////////////
   reload_event_add(NULL, rrclient_config_refresh, "refresh cached client settings after config reload");
   rrclient_config_refresh(NULL);
   Log(LOG_DEBUG, "main", "CAT poll blocking delay: %d second(s)", (int)poll_block_delay);

   /* Warm the disk-only directory before starting any frontend, audio worker
    * or network polling. Lazy first access must not stall an active reactor. */
   (void)receiver_directory_count();

   // CAT parsers and configured PTY/serial service bindings
   rr_cat_init();

//////////////////////////////

#ifdef USE_MONGOOSE
   mg_mgr_init(&mgr);
#endif


   // Setup the tab complete and hotkeys
   /* Shared completion provider is used by both GTK and TUI. Keep its registration, but defer all TUI-only callbacks until TUI is selected. */
   tui_register_completion_provider(client_cmd_completions);

   // Reap exited children and expire warm encoders after fwdsp.hangtime.
   // This is independent of the UI and Mongoose polling mechanisms.
   rr_core_timeout(1000, G_PRIORITY_DEFAULT, fwdsp_maintenance_cb);

   // Setup stdio & clock: the frontend module must be chosen first, so
   // the TUI setup below runs only when no graphical frontend loaded.
   // The [modules] config section decides what to load, e.g.:
   //    [modules]
   //    rrclient-gtk.so=
   rr_gtk_frontend_state_t gtk_frontend = {
      0
   };
   GThread *gtk_worker = NULL;
   const char *module_options = NULL;
   const char *requested = force_tui ? NULL : cfg_modules_get(0, &module_options);
   const char *loaded_name = NULL;

   if (requested) {
      Log(LOG_INFO, "core", "Requested module %s (path.modules=%s)", requested, cfg_get("path.modules"));

      if (rr_load_module(requested)) {
         frontend_module = rr_find_loaded_module(requested);
         loaded_name = requested;

         if (frontend_module) {
            // init (e.g. gtk_init) needs the real argc/argv; a failed init
            // (no display, theme problems) unloads the module and we fall
            // back to TUI.

            gtk_frontend.ops = frontend_ops();
            gtk_frontend.argc = argc;
            gtk_frontend.argv = argv;
            gtk_frontend.core_loop = g_main_loop_new(rr_core_context, FALSE);
            g_mutex_init(&gtk_frontend.mutex);
            g_cond_init(&gtk_frontend.cond);
            gtk_worker = g_thread_new("rrclient-gtk", rr_gtk_frontend_thread, &gtk_frontend);
            g_mutex_lock(&gtk_frontend.mutex);
            while (!gtk_frontend.ready) {
               g_cond_wait(&gtk_frontend.cond, &gtk_frontend.mutex);
            }
            g_mutex_unlock(&gtk_frontend.mutex);

            if (gtk_frontend.failed) {
               Log(LOG_CRIT, "core", "Frontend module init failed; falling back to TUI");

               /* Failed init worker must exit before we dlclose its code. */
               g_thread_join(gtk_worker);
               gtk_worker = NULL;
               g_main_loop_unref(gtk_frontend.core_loop);
               g_cond_clear(&gtk_frontend.cond);
               g_mutex_clear(&gtk_frontend.mutex);
               rr_unload_module(requested);
               frontend_module = NULL;
               loaded_name = NULL;
            } else {
               ui_mode = UI_MODE_GTK;

            }
         }
      } else {
         Log(LOG_INFO, "core", "Requested module %s not found (path.modules=%s); TUI mode", requested, cfg_get("path.modules"));
      }
   } else {
      Log(LOG_INFO, "core", "No modules configured ([modules] section); TUI mode");
   }
   (void)module_options;
   (void)loaded_name;

   if (ui_mode == UI_MODE_TUI) {
      tui_set_completion_handler(rrclient_tui_chat_completion);
      tui_set_completion_describer(client_cmd_completion_describe);
      tui_hotkey_register(TERMKEY_SYM_TAB, TERMKEY_KEYMOD_CTRL, rrclient_server_hotkey, NULL);
      /* Ctrl-Tab is indistinguishable from Tab in many terminals. */
      tui_hotkey_register('s', TERMKEY_KEYMOD_ALT, rrclient_server_hotkey, NULL);
      tui_hotkey_register('S', TERMKEY_KEYMOD_ALT, rrclient_server_hotkey, NULL);
      tui_hotkey_register('v', TERMKEY_KEYMOD_ALT, rrclient_tui_audio_hotkey, NULL);
      tui_hotkey_register('V', TERMKEY_KEYMOD_ALT, rrclient_tui_audio_hotkey, NULL);
      tui_hotkey_register('x', TERMKEY_KEYMOD_ALT, rrclient_tui_server_picker_hotkey, NULL);
      tui_hotkey_register('X', TERMKEY_KEYMOD_ALT, rrclient_tui_server_picker_hotkey, NULL);
      tui_hotkey_register('z', TERMKEY_KEYMOD_ALT, rrclient_sdr_hotkey, NULL);
      tui_hotkey_register('Z', TERMKEY_KEYMOD_ALT, rrclient_sdr_hotkey, NULL);
      tui_hotkey_register(TERMKEY_SYM_ENTER, TERMKEY_KEYMOD_ALT, rrclient_ptt_hotkey, NULL);
      tui_hotkey_register(' ', TERMKEY_KEYMOD_CTRL, rrclient_ptt_hotkey, NULL);
      tui_hotkey_register(0, TERMKEY_KEYMOD_CTRL, rrclient_ptt_hotkey, NULL);
      tui_input_blocked_cb = rrclient_chat_input_blocked;
      tui_readline_cb = parse_chat_input_real;
      tui_set_topline_renderer(rrclient_tui_topline);
      tui_set_prompt_renderer(rrclient_tui_prompt);
      tui_init();
      // One shared tab per system output type, independent of server count.
      tui_window_create("host log");
      tui_window_create("client log");
      tui_window_t *sdr_tab = tui_window_create("SDR Directory");

      if (sdr_tab) {
         sdr_init(sdr_tab);
      }
      event_on_binary("media.frame.log", rrclient_tui_host_log_frame, NULL);
      log_add_callback(rrclient_tui_log_print_va, LOG_CALLBACK_FILTERED);
      rrclient_tui_workspace_init();
      tui_redraw_request();

      // 1hz TUI clock (statusbar/clock refresh, shutdown check)
      g_timeout_add(1000, tui_clock_cb_real, NULL);
      // 20hz reconnect/poll sweep
      g_timeout_add(rr_rtp_supported(false) ? 10 : 50, ws_poll_cb, NULL);
   }

   /* Repeat after frontend/log initialization so the warning is visible in Client Log. */
   if (schema_warning) {
      Log(LOG_WARN, "db.schema", "%s", schema_warning);
   }

   rrclient_database_log_startup();

   // Register all of our core event handlers
   rrclient_register_events();
#ifdef RR_HAVE_RUSTYSCRIPT
   rrclient_script_init();
#endif

   if (!event_on_message_domain_token("client.supervision.reply", rrclient_supervision_reply_event, NULL, "core")) {
      Log(LOG_WARN, "supervision", "Unable to register supervision reply event");
   }

   rrclient_log_level_register();
   rrclient_config_reload_register();
   rrclient_backup_register();

   if (!event_on_message_domain_token("client.ptt.request", rrclient_ptt_request_event, NULL, "core")) {
      Log(LOG_WARN, "ptt", "Unable to register core PTT request handler");
   }
   rrclient_receiver_connect_init();
   rrclient_local_bridge_controls_init();
   webcam_client_register_events();
   connman_register_events();

   // GStreamer audio paths (RX pipeline feeds from media.frame.audio events;
   // TX pipeline pushes mic samples to the server)
   audio_init();

   // setup the client bits and autoconnect if configured
   ws_client_init();
   if(ui_mode==UI_MODE_TUI)rrclient_tui_security_init();
   if(ui_mode!=UI_MODE_TUI||!rrclient_tui_identity_first_run())connman_autoconnect();

   // start the main loop (both modes now run on GLib)
   if (ui_mode == UI_MODE_TUI) {
      // TUI: run the GLib main loop. The keyboard, clock, and reconnect
      // polling are all GLib sources now (stdin is watched via tui.keys.c)
      GMainLoop *tui_loop = g_main_loop_new(NULL, FALSE);
      g_main_loop_run(tui_loop);
   } else if (frontend_module) {
      if (gtk_worker) {
         /* A single high-priority transport sweep runs on the core context, regardless of backend. */
         rr_core_timeout(rr_rtp_supported(false) ? 10 : 50, G_PRIORITY_HIGH, ws_poll_cb);
         Log(LOG_INFO, "core.thread", "Core main loop starting tid=%p context=%p", (void *)g_thread_self(), (void *)rr_core_context);
         g_main_loop_run(gtk_frontend.core_loop);
         g_mutex_lock(&gtk_frontend.mutex);
         gtk_frontend.core_stopped = true;
         g_cond_signal(&gtk_frontend.cond);
         g_mutex_unlock(&gtk_frontend.mutex);
         g_thread_join(gtk_worker);
         gtk_worker = NULL;
         g_main_loop_unref(gtk_frontend.core_loop);
         g_cond_clear(&gtk_frontend.cond);
         g_mutex_clear(&gtk_frontend.mutex);
      }
   }
   rrclient_cleanup();

   return 0;
}
