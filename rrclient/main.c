//
// rrclient/main.c: Core of the client
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#include <rrclient/database.h>
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
#include <librustyaxe/termkey.h>
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
   ws_send_ptt_cmd_in_room(connection, vfo, on, room);
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

   if (!ws_conn || ws_connected != 1) {
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
   rrclient_send_ptt_command(ws_conn, vfo, request.on != 0, request.room);
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
   delivery->fn(delivery->arg);

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
   rrclient_poll_events();
   // GTK and TUI share the nonblocking transport sweep and reconnect engine.
   rrclient_poll_events_reconnect();

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

/* Text-mode receiver directory. All data comes from the local catalog. Grouping, filtering and user marks mirror gtk.receiver-directory.c. */
typedef struct {
   size_t index;
   const char *country;
   unsigned count;
   bool group, connected;
} tui_sdr_row_t;
static GArray *tui_sdr_rows;
/* Set of expanded groups; countries are collapsed by default. */
static GHashTable *tui_sdr_expanded;
static size_t tui_sdr_selected, tui_sdr_scroll;
static bool tui_sdr_availability, tui_sdr_connected, tui_sdr_favorites;
static bool tui_sdr_proxy_bridge;   /* Off = use local rrbridge, as in GTK. */
static bool tui_sdr_info, tui_sdr_mark_menu, tui_sdr_search_active;
static char tui_sdr_search[128];
static char tui_sdr_message[256];
static size_t tui_sdr_total, tui_sdr_matched;
/* ANSI 16-color foreground/background combinations for dark terminals. */
#define SDR_NORMAL "\033[0m"
#define SDR_TITLE "\033[1;36m"
#define SDR_GROUP "\033[1;34m"
#define SDR_HILITE "\033[1;37;44m"
#define SDR_BUTTON "\033[1;37;44m"
#define SDR_FOCUS "\033[1;30;46m"
#define SDR_ENABLED "\033[1;30;102m"
#define SDR_WARNING "\033[1;33m"
#define SDR_NAME "\033[1;96m"
#define SDR_LOCATION "\033[37m"
#define SDR_USERS "\033[1;93m"
#define SDR_GOOD "\033[1;92m"
#define SDR_BAD "\033[1;91m"
#define SDR_MUTED "\033[2;37m"
static int tui_sdr_filter_start[4], tui_sdr_filter_end[4];
static void tui_sdr_filters_changed(void) {
   /* Never retain a scroll offset into the old filtered row set. */
   tui_sdr_selected = 0;
   tui_sdr_scroll = 0;
   tui_sdr_info = false;
   tui_sdr_message[0] = 0;
}
static void tui_sdr_clear_filters(void) {
   tui_sdr_availability = tui_sdr_connected = tui_sdr_favorites = false;
   tui_sdr_search[0] = 0;
   tui_sdr_search_active = false;
   tui_sdr_filters_changed();
}


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
static void tui_sdr_rebuild(void) {
   if (!tui_sdr_rows) {
      tui_sdr_rows = g_array_new(FALSE, FALSE, sizeof(tui_sdr_row_t));
   }

   if (!tui_sdr_expanded) {
      tui_sdr_expanded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
      g_hash_table_add(tui_sdr_expanded, g_strdup("Connected SDRs"));
   }
   g_array_set_size(tui_sdr_rows, 0);
   tui_sdr_total = receiver_directory_count();
   tui_sdr_matched = 0;
   GHashTable *groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_array_unref);
   GArray *connected = g_array_new(FALSE, FALSE, sizeof(size_t));
   GList *countries = NULL;

   for (size_t i = 0, n = receiver_directory_count() ; i < n ; i++) {
      const receiver_directory_entry_t *r = receiver_directory_receiver(i);

      if (!tui_sdr_matches(r)) {
         continue;
      }
      tui_sdr_matched++;
      const char *country = tui_sdr_country(r);
      GArray *indices = g_hash_table_lookup(groups, country);

      if (!indices) {
         indices = g_array_new(FALSE, FALSE, sizeof(size_t));
         g_hash_table_insert(groups, g_strdup(country), indices);
         countries = g_list_prepend(countries, g_strdup(country));
      }
      g_array_append_val(indices, i);

      if (tui_sdr_is_connected(r)) {
         g_array_append_val(connected, i);
      }
   }

   if (connected->len) {
      g_array_sort(connected, tui_sdr_entry_compare);
      tui_sdr_row_t header = {
         .group = true, .connected = true, .country = "Connected SDRs", .count = connected->len
      };
      g_array_append_val(tui_sdr_rows, header);

      if (g_hash_table_contains(tui_sdr_expanded, "Connected SDRs")) {
         for (unsigned j = 0 ; j < connected->len ; j++) {
            tui_sdr_row_t row = {
               .index = g_array_index(connected, size_t, j), .connected = true
            };
            g_array_append_val(tui_sdr_rows, row);
         }
      }
   }
   countries = g_list_sort(countries, tui_sdr_country_compare);

   for (GList *c = countries ; c ; c = c->next) {
      const char *country = c->data;
      GArray *indices = g_hash_table_lookup(groups, country);

      if (!indices) {
         continue;
      }
      g_array_sort(indices, tui_sdr_entry_compare);
      tui_sdr_row_t header = {
         .group = true, .country = country, .count = indices->len
      };
      /* Store country strings in the persistent collapse table's separate key namespace before releasing temporary country names. */
      header.country = g_intern_string(country);
      g_array_append_val(tui_sdr_rows, header);

      if (!g_hash_table_contains(tui_sdr_expanded, country)) {
         continue;
      }

      for (unsigned j = 0 ; j < indices->len ; j++) {
         tui_sdr_row_t row = {
            .index = g_array_index(indices, size_t, j)
         };
         g_array_append_val(tui_sdr_rows, row);
      }
   }

   g_list_free_full(countries, g_free);
   g_array_unref(connected);
   g_hash_table_unref(groups);

   if (!tui_sdr_rows->len) {
      tui_sdr_selected = tui_sdr_scroll = 0;
   } else if (tui_sdr_selected >= tui_sdr_rows->len) {
      tui_sdr_selected = tui_sdr_rows->len - 1;
   }
   int height = tui_rows() - 6;

   if (height < 1) {
      height = 1;
   }

   if (tui_sdr_scroll >= tui_sdr_rows->len) {
      tui_sdr_scroll = 0;
   }

   if (tui_sdr_selected < tui_sdr_scroll) {
      tui_sdr_scroll = tui_sdr_selected;
   }

   if (tui_sdr_selected >= tui_sdr_scroll + (size_t)height) {
      tui_sdr_scroll = tui_sdr_selected - (size_t)height + 1;
   }
}
static tui_sdr_row_t *tui_sdr_current_row(void) {
   if (!tui_sdr_rows || tui_sdr_selected >= tui_sdr_rows->len) {
      return NULL;
   }

   return &g_array_index(tui_sdr_rows, tui_sdr_row_t, tui_sdr_selected);
}
static const receiver_directory_entry_t *tui_sdr_current(void) {
   tui_sdr_row_t *row = tui_sdr_current_row();

   return row && !row->group ? receiver_directory_receiver(row->index) : NULL;
}
static void tui_sdr_safe_text(const char *s, int width) {
   for (int i = 0 ; i < width && s && s[i] ; i++) {
      unsigned char c = (unsigned char)s[i];
      putchar(c >= 32 && c < 127 ? c : ' ');
   }
}
static void tui_sdr_line(int row, const char *label, const char *value) {
   printf("\033[%d;1H%-18s ", row, label);
   tui_sdr_safe_text(value, tui_cols() - 21);
}

enum {
   SDR_BTN_BACK, SDR_BTN_CONNECT, SDR_BTN_INFO, SDR_BTN_MARK, SDR_BTN_FAVORITE, SDR_BTN_REFRESH, SDR_BTN_RESET, SDR_BTN_COUNT
};
static int tui_sdr_button_focus = -1;  /* -1 = receiver tree */
static int tui_sdr_button_start[SDR_BTN_COUNT], tui_sdr_button_end[SDR_BTN_COUNT];
static int tui_sdr_mark_focus;
static int tui_sdr_mark_start[5], tui_sdr_mark_end[5];
static const char *tui_sdr_mark_labels[] = {
   "Unreachable", "Restricted", "Good", "Clear", "Cancel"
};

static const char *tui_sdr_mark_values[] = {
   "unreachable", "restricted", "good", "unknown", NULL
};
static void tui_sdr_draw_mark_buttons(int row, int cols) {
   int col = 1;
   printf("\033[%d;1H\033[K", row);

   for (unsigned i = 0 ; i < G_N_ELEMENTS(tui_sdr_mark_labels) ; i++) {
      int width = (int)strlen(tui_sdr_mark_labels[i]) + 2;
      tui_sdr_mark_start[i] = tui_sdr_mark_end[i] = 0;

      if (col + width - 1 <= cols) {
         tui_sdr_mark_start[i] = col;
         tui_sdr_mark_end[i] = col + width - 1;
         printf("\033[%d;%dH", row, col);
         fputs((int)i == tui_sdr_mark_focus ? SDR_FOCUS : SDR_BUTTON, stdout);
         printf("[%s]", tui_sdr_mark_labels[i]);
         fputs(SDR_NORMAL, stdout);
      }
      col += width + 1;
   }
}


static const char *tui_sdr_button_label(int button) {
   const receiver_directory_entry_t *r = tui_sdr_current();

   switch (button) {
      case SDR_BTN_BACK: {
         return "Back";
      }
      case SDR_BTN_CONNECT: {
         return r && tui_sdr_is_connected(r) ? "Disconnect" : "Connect";
      }
      case SDR_BTN_INFO: {
         return "Info";
      }
      case SDR_BTN_MARK: {
         return "Mark";
      }
      case SDR_BTN_FAVORITE: {
         return r && r->favorite ? "Unfavorite" : "Favorite";
      }
      case SDR_BTN_REFRESH: {
         return "Refresh";
      }
      case SDR_BTN_RESET: {
         return "Reset filters";
      }
      default: {
         return "";
      }
   }
}
static void tui_sdr_draw_buttons(int row, int cols) {
   int col = 1;
   printf("\033[%d;1H\033[K", row);

   for (int i = 0 ; i < SDR_BTN_COUNT ; i++) {
      tui_sdr_button_start[i] = tui_sdr_button_end[i] = 0;

      if ((i == SDR_BTN_BACK && !tui_sdr_info) || (i == SDR_BTN_INFO && tui_sdr_info)) {
         continue;
      }
      const char *label = tui_sdr_button_label(i);
      int width = (int)strlen(label) + 2;
      tui_sdr_button_start[i] = col;
      tui_sdr_button_end[i] = col + width - 1;

      if (col + width - 1 > cols) {
         tui_sdr_button_start[i] = tui_sdr_button_end[i] = 0;
      } else {
         printf("\033[%d;%dH", row, col);
         fputs(i == tui_sdr_button_focus ? SDR_FOCUS : SDR_BUTTON, stdout);
         printf("[%s]", label);
         fputs(SDR_NORMAL, stdout);
      }
      col += width + 1;
   }
}
static void tui_sdr_draw(tui_window_t *w, int rows, int cols, void *data) {
   (void)w;
   (void)data;
   tui_sdr_rebuild();
   printf("\033[2;1H" SDR_TITLE "Search:" SDR_NORMAL " %s%s\033[K", tui_sdr_search, tui_sdr_search_active ? "_" : "  (/ to edit)");
   printf("\033[3;1H\033[K");
   int filter_col = 1;
   const char *filters[] = {
      "Availability", "Connected", "Favorites", "Proxy rrbridge"
   };
   const bool enabled[] = {
      tui_sdr_availability, tui_sdr_connected, tui_sdr_favorites,
      tui_sdr_proxy_bridge
   };

   for (int i = 0 ; i < 4 ; i++) {
      int width = (int)strlen(filters[i]) + 4;
      tui_sdr_filter_start[i] = tui_sdr_filter_end[i] = 0;

      if (filter_col + width - 1 <= cols) {
         tui_sdr_filter_start[i] = filter_col;
         tui_sdr_filter_end[i] = filter_col + width - 1;
         printf("\033[3;%dH%s[%c] %s" SDR_NORMAL, filter_col, enabled[i] ? SDR_ENABLED : SDR_BUTTON, enabled[i] ? 'x' : ' ', filters[i]);
      }
      filter_col += width + 2;
   }

   if (filter_col + 19 <= cols) {
      printf("\033[3;%dH" SDR_TITLE "%zu / %zu receivers" SDR_NORMAL, filter_col, tui_sdr_matched, tui_sdr_total);
   }

   if (tui_sdr_info) {
      const receiver_directory_entry_t *r = tui_sdr_current();

      if (r) {
         tui_sdr_line(4, "Name:", r->name);
         tui_sdr_line(5, "URL:", r->url);
         tui_sdr_line(6, "Location:", r->location);
         tui_sdr_line(7, "Country / grid:", r->country);
         tui_sdr_line(8, "Contact:", r->contact);
         tui_sdr_line(9, "Antenna:", r->antenna);
         tui_sdr_line(10, "Hardware:", r->hardware);
         tui_sdr_line(11, "Description:", r->description);
         tui_sdr_line(12, "Local report:", r->local_state);
         tui_sdr_line(13, "Reason:", r->local_reason);
         printf("\033[14;1H Users: %d/%d   API channels: %d   Range: %llu-%llu Hz", r->users, r->users_max, r->ext_api, (unsigned long long)r->low_hz, (unsigned
            long long)r->high_hz);
      } else {
         tui_sdr_line(4, "Info:", "Select a receiver first.");
      }
   } else {
      printf("\033[4;1H" SDR_TITLE "    %-26s %-26s %-8s %-12s" SDR_NORMAL, "Receiver / Country", "Location", "Users", "Status");

      if (!tui_sdr_rows->len) {
         printf("\033[5;1H" SDR_WARNING);

         if (tui_sdr_total == 0) {
            fputs("Directory cache is empty. Try Refresh.", stdout);
         } else {
            printf("No receivers match these filters (%zu cached). Press 0 or Reset filters.", tui_sdr_total);
         }
         fputs(SDR_NORMAL, stdout);
      }

      for (int y = 5 ; y < rows - 1 ; y++) {
         size_t i = tui_sdr_scroll + (size_t)(y - 5);

         if (i >= tui_sdr_rows->len) {
            break;
         }
         tui_sdr_row_t *item = &g_array_index(tui_sdr_rows, tui_sdr_row_t, i);
         printf("\033[%d;1H", y);

         if (i == tui_sdr_selected) {
            fputs(SDR_HILITE, stdout);
         } else if (item->group) {
            fputs(SDR_GROUP, stdout);
         }

         if (item->group) {
            bool collapsed = !g_hash_table_contains(tui_sdr_expanded, item->country);
            printf(" %c  ", collapsed ? '+' : '-');
            tui_sdr_safe_text(item->country, 27);
            printf(" (%u)", item->count);
         } else {
            const receiver_directory_entry_t *r = receiver_directory_receiver(item->index);

            if (r) {
               bool selected = i == tui_sdr_selected;
               const char *state = tui_sdr_is_connected(r) ? "Connected" :
                  r->local_state && strcmp(r->local_state, "unknown") ? r->local_state : r->flagged ? "Flagged" : r->offline ? "Offline" : "Not tested";
               bool good = tui_sdr_is_connected(r) || !g_ascii_strcasecmp(state, "good");
               bool bad = r->flagged || r->offline || !g_ascii_strcasecmp(state, "restricted") || !g_ascii_strcasecmp(state, "unreachable");
               printf("    ");

               if (!selected) {
                  fputs(SDR_NAME, stdout);
               }
               tui_sdr_safe_text(r->name && *r->name ? r->name : r->url, 26);

               if (!selected) {
                  fputs(SDR_NORMAL, stdout);
               }
               printf(" ");

               if (!selected) {
                  fputs(SDR_LOCATION, stdout);
               }
               tui_sdr_safe_text(r->location, 26);

               if (!selected) {
                  fputs(SDR_NORMAL SDR_USERS, stdout);
               }
               printf(" %2d/%-3d  ", r->users, r->users_max);

               if (!selected) {
                  fputs(good ? SDR_GOOD : bad ? SDR_BAD : SDR_MUTED, stdout);
               }
               printf("%-12.12s", state);
            }
         }
         fputs(SDR_NORMAL, stdout);
      }
   }
   int action_row = rows - 1;

   if (tui_sdr_mark_menu) {
      tui_sdr_draw_mark_buttons(action_row, cols);
   } else {
      tui_sdr_draw_buttons(action_row, cols);
   }
   const char *message = tui_sdr_message[0] ? tui_sdr_message : receiver_directory_status();
   printf("\033[%d;1H", rows);
   tui_sdr_safe_text(message, cols - 1);
   printf("\033[K");
}
static void tui_sdr_connect(void) {
   const receiver_directory_entry_t *r = tui_sdr_current();

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
      if (!rrclient_receiver_connect(r->url, false)) {
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
static void tui_sdr_mark(const char *state) {
   const receiver_directory_entry_t *r = tui_sdr_current();

   if (r && r->url) {
      receiver_directory_set_state(r->url, state, !strcmp(state, "unknown") ? "" : "User report; receiver was not probed");
   }
   tui_sdr_mark_menu = false;
}
static void tui_sdr_mark_activate(int option) {
   if (option >= 0 && option < 4) {
      tui_sdr_mark(tui_sdr_mark_values[option]);
   } else {
      tui_sdr_mark_menu = false;
   }
}
static void tui_sdr_button_activate(int button) {
   const receiver_directory_entry_t *r = tui_sdr_current();

   switch (button) {
      case SDR_BTN_BACK: {
         tui_sdr_info = false;
         tui_sdr_button_focus = -1;
         break;
      }
      case SDR_BTN_CONNECT: {
         tui_sdr_connect();
         break;
      }
      case SDR_BTN_INFO: {
         if (r) {
            tui_sdr_info = true;
            tui_sdr_button_focus = SDR_BTN_BACK;
         }
         break;
      }
      case SDR_BTN_MARK: {
         if (r) {
            tui_sdr_mark_menu = true;
            tui_sdr_mark_focus = 0;
         }
         break;
      }
      case SDR_BTN_FAVORITE: {
         if (r && r->url) {
            receiver_directory_set_favorite(r->url, !r->favorite);
         }
         break;
      }
      case SDR_BTN_REFRESH: {
         receiver_directory_refresh();
         break;
      }
      case SDR_BTN_RESET: {
         tui_sdr_clear_filters();
         break;
      }
   }
}
static void tui_sdr_activate(void) {
   tui_sdr_row_t *row = tui_sdr_current_row();

   if (!row) {
      return;
   }

   if (row->group) {
      if (g_hash_table_contains(tui_sdr_expanded, row->country)) {
         g_hash_table_remove(tui_sdr_expanded, row->country);
      } else {
         g_hash_table_add(tui_sdr_expanded, g_strdup(row->country));
      }
      tui_sdr_rebuild();
   } else {
      tui_sdr_connect();
   }
}
static bool tui_sdr_key(tui_window_t *w, unsigned key, unsigned mods, void *data) {
   (void)w;
   (void)data;

   /* Escape returns from receiver details without leaving the directory. */
   if (tui_sdr_info && !tui_sdr_mark_menu && (key == TERMKEY_SYM_ESCAPE || key == 27)) {
      tui_sdr_button_activate(SDR_BTN_BACK);

      return true;
   }

   if (tui_sdr_search_active && (key == TERMKEY_SYM_TAB || key == '\t')) {
      tui_sdr_search_active = false;
   } else if (tui_sdr_search_active) {
      size_t len = strlen(tui_sdr_search);

      if (key == TERMKEY_SYM_ENTER || key == 10 || key == 27) {
         tui_sdr_search_active = false;
      } else if (key == TERMKEY_SYM_BACKSPACE || key == 127 || key == 8) {
         if (len) {
            tui_sdr_search[len - 1] = 0;
         }
      } else if (key == '/' && len == 0) {
         /* '/' opens search; repeated slashes must not become a prefix. */
      } else if (key >= 32 && key < 127 && len + 1 < sizeof(tui_sdr_search)) {
         tui_sdr_search[len] = (char)key;
         tui_sdr_search[len + 1] = 0;
      } else {
         return true;
      }
      tui_sdr_filters_changed();
      tui_sdr_rebuild();

      return true;
   }

   if (tui_sdr_mark_menu) {
      if (key == TERMKEY_SYM_TAB || key == '\t') {
         int direction = mods & TERMKEY_KEYMOD_SHIFT ? -1 : 1;
         tui_sdr_mark_focus = (tui_sdr_mark_focus + 5 + direction) % 5;
      } else if (key == TERMKEY_SYM_ENTER || key == 10 || key == ' ') {
         tui_sdr_mark_activate(tui_sdr_mark_focus);
      } else if (key >= '1' && key <= '4') {
         tui_sdr_mark_activate((int)(key - '1'));
      } else if (key == 27 || key == TERMKEY_SYM_ESCAPE) {
         tui_sdr_mark_menu = false;
      }

      return true;
   }

   if (key == TERMKEY_SYM_TAB || key == '\t') {
      int step = (mods & TERMKEY_KEYMOD_SHIFT) ? -1 : 1;
      int next = tui_sdr_button_focus;
      do {
         next += step;

         if (next >= SDR_BTN_COUNT) {
            next = -1;
         }

         if (next < -1) {
            next = SDR_BTN_COUNT - 1;
         }
      } while ((next == SDR_BTN_BACK && !tui_sdr_info) || (next == SDR_BTN_INFO && tui_sdr_info));
      tui_sdr_button_focus = next;

      return true;
   }

   if (mods) {
      return false;
   }
   size_t count = tui_sdr_rows ? tui_sdr_rows->len : 0;

   if (key == TERMKEY_SYM_UP) {
      if (tui_sdr_selected) {
         tui_sdr_selected--;
      }
   } else if (key == TERMKEY_SYM_DOWN) {
      if (count && tui_sdr_selected + 1 < count) {
         tui_sdr_selected++;
      }
   } else if (key == TERMKEY_SYM_PAGEUP) {
      size_t page = tui_rows() > 6 ? tui_rows() - 6 : 1;
      tui_sdr_selected = tui_sdr_selected > page ? tui_sdr_selected - page : 0;
   } else if (key == TERMKEY_SYM_PAGEDOWN) {
      size_t page = tui_rows() > 6 ? tui_rows() - 6 : 1;

      if (count) {
         tui_sdr_selected = page < count - 1 - tui_sdr_selected ? tui_sdr_selected + page : count - 1;
      }
   } else if (key == TERMKEY_SYM_ENTER || key == 10 || key == ' ') {
      if (tui_sdr_button_focus >= 0) {
         tui_sdr_button_activate(tui_sdr_button_focus);
      } else if (tui_sdr_info) {
         tui_sdr_info = false;
      } else {
         tui_sdr_activate();
      }
   } else if (key == '/') {
      tui_sdr_search_active = true;
   } else if (key == 'i' || key == 'I') {
      if (!tui_sdr_info) {
         tui_sdr_button_activate(SDR_BTN_INFO);
      }
   } else if (key == 'm' || key == 'M') {
      if (tui_sdr_current()) {
         tui_sdr_button_activate(SDR_BTN_MARK);
      }
   } else if (key == 'f' || key == 'F') {
      const receiver_directory_entry_t *r = tui_sdr_current();

      if (r) {
         receiver_directory_set_favorite(r->url, !r->favorite);
      }
   } else if (key == 'r' || key == 'R') {
      receiver_directory_refresh();
   } else if (key == 'a' || key == 'A') {
      tui_sdr_availability = !tui_sdr_availability;
      tui_sdr_filters_changed();
   } else if (key == 'c' || key == 'C') {
      tui_sdr_connected = !tui_sdr_connected;
      tui_sdr_filters_changed();
   } else if (key == 'v' || key == 'V') {
      tui_sdr_favorites = !tui_sdr_favorites;
      tui_sdr_filters_changed();
   } else if (key == 'p' || key == 'P') {
      tui_sdr_proxy_bridge = !tui_sdr_proxy_bridge;
   } else if (key == '0') {
      tui_sdr_clear_filters();
   } else {
      return false;
   }
   tui_sdr_rebuild();

   return true;
}
static bool tui_sdr_mouse(tui_window_t *w, int row, int col, int button, int action, unsigned mods, void *data) {
   (void)w;
   (void)mods;
   (void)data;

   if (action != TERMKEY_MOUSE_PRESS) {
      return false;
   }

   if (button == 4 || button == 5) {
      if (button == 4 && tui_sdr_selected) {
         tui_sdr_selected--;
      }

      if (button == 5 && tui_sdr_rows && tui_sdr_selected + 1 < tui_sdr_rows->len) {
         tui_sdr_selected++;
      }
   } else if (button != 1) {
      return false;
   } else if (row == 2) {
      tui_sdr_search_active = true;
   } else if (row == 3) {
      int filter = -1;

      for (int i = 0 ; i < 4 ; i++) {
         if (tui_sdr_filter_start[i] && col >= tui_sdr_filter_start[i] && col <= tui_sdr_filter_end[i]) {
            filter = i;
            break;
         }
      }

      if (filter == 0) {
         tui_sdr_availability = !tui_sdr_availability;
      } else if (filter == 1) {
         tui_sdr_connected = !tui_sdr_connected;
      } else if (filter == 2) {
         tui_sdr_favorites = !tui_sdr_favorites;
      } else if (filter == 3) {
         tui_sdr_proxy_bridge = !tui_sdr_proxy_bridge;
      } else {
         return false;
      }

      if (filter < 3) {
         tui_sdr_filters_changed();
      }
   } else if (row == tui_rows() - 1) {
      if (tui_sdr_mark_menu) {
         for (int i = 0 ; i < 5 ; i++) {
            if (tui_sdr_mark_start[i] && col >= tui_sdr_mark_start[i] && col <= tui_sdr_mark_end[i]) {
               tui_sdr_mark_focus = i;
               tui_sdr_mark_activate(i);
               break;
            }
         }
      } else {
         for (int i = 0 ; i < SDR_BTN_COUNT ; i++) {
            if (tui_sdr_button_start[i] && col >= tui_sdr_button_start[i] && col <= tui_sdr_button_end[i]) {
               tui_sdr_button_focus = i;
               tui_sdr_button_activate(i);
               break;
            }
         }
      }
   } else if (!tui_sdr_info && row >= 5 && row < tui_rows() - 1) {
      size_t i = tui_sdr_scroll + (size_t)(row - 5);

      if (tui_sdr_rows && i < tui_sdr_rows->len) {
         tui_sdr_selected = i;
         tui_sdr_button_focus = -1;
         tui_sdr_row_t *item = tui_sdr_current_row();

         if (item && item->group && col <= 4) {
            tui_sdr_activate();
         }
      }
   } else {
      return false;
   }
   tui_sdr_rebuild();

   return true;
}

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
void rrclient_tui_server_picker_toggle(void) {
   if (ui_mode != UI_MODE_TUI) {
      return;
   }

   if (tui_window_find(RR_TUI_SERVER_PICKER)) {
      tui_server_picker_close();

      return;
   }
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
   bool found = false;

   for (size_t i = 0 ; i < tui_local_count ; i++) {
      if (tui_local_vfos[i] == tui_local_vfo) {
         found = true;
      }
   }

   if (!found) {
      tui_local_vfo = tui_local_vfos[0];
   }
}
static void tui_local_select_vfo(char vfo) {
   if (!rrclient_local_bridge_state_vfo(vfo, NULL, NULL)) {
      return;
   }
   tui_local_vfo = vfo;
   rrclient_room_set_active_vfo("&local-sdr", vfo);
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
static void tui_local_sdr_draw(tui_window_t *win, int rows, int cols, void *unused) {
   (void)win;
   (void)unused;
   tui_local_update_vfos();
   char active = rrclient_room_active_vfo("&local-sdr");
   printf("\033[2;1H" SDR_TITLE " Local SDR / rrbridge VFO controls (RX only)" SDR_NORMAL "\033[K");
   printf("\033[3;1H" SDR_TITLE "   VFO       Frequency           Mode      Status" SDR_NORMAL "\033[K");

   if (!tui_local_count) {
      printf("\033[4;1H" SDR_WARNING "  Waiting for local rrbridge receiver..." SDR_NORMAL "\033[K");
   }

   for (size_t i = 0 ; i < tui_local_count && (int)i + 4 < rows - 5 ; i++) {
      char vfo = tui_local_vfos[i];
      uint64_t hz = 0;
      const char *mode = NULL;
      bool online = rrclient_local_bridge_state_vfo(vfo, &hz, &mode);
      printf("\033[%zu;1H%s  %c VFO %c  " SDR_NORMAL, i + 4, vfo == tui_local_vfo ? SDR_HILITE : SDR_NAME, vfo == active ? '*' : ' ', vfo);

      if (online) {
         printf(SDR_USERS "%11.6f MHz" SDR_NORMAL "    " SDR_GOOD "%-8s" SDR_NORMAL "  Online", (double)hz / 1000000.0, mode ? mode : "---");
      } else {
         fputs(SDR_WARNING "Not ready" SDR_NORMAL, stdout);
      }
      fputs("\033[K", stdout);
   }

   printf("\033[%d;1H" SDR_TITLE "Selected VFO %c" SDR_NORMAL, rows - 5, tui_local_vfo);
   printf("\033[%d;1H", rows - 4);

   if (tui_local_freq_edit) {
      printf(SDR_FOCUS " Frequency (MHz): %s_  [Enter apply / Esc cancel] " SDR_NORMAL, tui_local_freq);
   } else {
      printf(" F: enter MHz frequency    +/-: 1 kHz    M: change mode    V: next VFO");
   }
   const char *buttons[] = {
      "-1 kHz", "+1 kHz", "Mode", "Frequency", "Next VFO", "Directory"
   };
   int x = 1;

   for (int i = 0 ; i < LOCAL_BUTTONS ; i++) {
      int width = (int)strlen(buttons[i]) + 2;
      tui_local_button_start[i] = tui_local_button_end[i] = 0;

      if (x + width - 1 <= cols) {
         tui_local_button_start[i] = x;
         tui_local_button_end[i] = x + width - 1;
         printf("\033[%d;%dH%s[%s]" SDR_NORMAL, rows - 2, x, i == tui_local_focus ? SDR_FOCUS : SDR_BUTTON, buttons[i]);
      }
      x += width + 1;
   }

   printf("\033[%d;1H" SDR_MUTED "Tab: buttons / list   Space/Enter: activate   Esc: directory" SDR_NORMAL "\033[K", rows - 1);
   printf("\033[%d;1H", rows);
   tui_sdr_safe_text(tui_local_message[0] ? tui_local_message : "Use arrow keys to choose a VFO; tuning is RX-only.", cols - 1);
   fputs("\033[K", stdout);
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
      tui_local_action(key == '-' ? LOCAL_DEC : LOCAL_INC);
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
static bool tui_local_sdr_mouse(tui_window_t *win, int row, int col, int button, int action, unsigned mods, void *unused) {
   (void)win;
   (void)mods;
   (void)unused;

   if (action != TERMKEY_MOUSE_PRESS || button != 1) {
      return false;
   }

   if (row >= 4 && row < 4 + (int)tui_local_count) {
      tui_local_select_vfo(tui_local_vfos[row - 4]);
      tui_local_focus = -1;

      return true;
   }

   if (row == tui_rows() - 2) {
      for (int i = 0 ; i < LOCAL_BUTTONS ; i++) {
         if (tui_local_button_start[i] && col >= tui_local_button_start[i] && col <= tui_local_button_end[i]) {
            tui_local_focus = i;
            tui_local_action(i);

            return true;
         }
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
      tui_redraw_screen();

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
   return rrclient_connection_cycle_status(win && (rrclient_window_is_shared(win->title) || !strcasecmp(win->title, "SDR Directory")));
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

   if (!ws_conn || ws_connected != 1) {
      return true;
   }
   char vfo[2] = {
      vfo_state_get_active(), '\0'
   };
   bool active = vfo_state_get_bool(vfo, "cat.state.ptt", false);
   rrclient_send_ptt_command(ws_conn, vfo, !active, ui_active_window_name());

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
   event_shutdown();

   // Persist the running config (including window placements learned while
   // we ran) if ui.save-on-exit says so. Backed up via cfg_save's .old logic.
   if (cfg && cfg_get_bool("ui.save-on-exit", false)) {
      Log(LOG_INFO, "config", "ui.save-on-exit: saving config overrides to client.db");

      if (!rrclient_config_save()) {
         Log(LOG_WARN, "config", "Unable to save configuration overrides to client.db");
      }
   }

   free((void *)cfg_log_audio);
   cfg_log_audio = NULL;
   cfg_fini();

   if (ui_mode == UI_MODE_TUI) {
      tui_fini();
   }

   logger_end();
   exit(0);

   return false;
}

void show_arg_help(int argc, char **argv) {
   printf("%s [-T] [-f config] [-s server] [-S] [--ignore-db-config] [--reset-db] [-h]\n", argv[0]);
   printf("\t-S\t\tssh mode (implies -T; throttle screen updates)\n");
   printf("\t-T\t\tTUI only mode (no X11)\n");
   printf("\t-f config\tChose an alternative configuration file\n");
   printf("\t-s server\tServer profile to autoconnect to on start\n");
   printf("\t--ignore-db-config\tSkip saved configuration overrides (still load profiles and themes)\n");
   printf("\t--reset-db\tReset client SQLite database (interactive confirmation) and exit\n");
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
   bool reset_db = false;

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
         case 1001: {
            reset_db = true;
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
   if (reset_db) return rrclient_database_reset();
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
         sdr_tab->view_draw = tui_sdr_draw;
         sdr_tab->view_full_height = true;
         sdr_tab->view_key = tui_sdr_key;
         sdr_tab->view_mouse = tui_sdr_mouse;
      }
      event_on_binary("media.frame.log", rrclient_tui_host_log_frame, NULL);
      log_add_callback(rrclient_tui_log_print_va, LOG_CALLBACK_FILTERED);

      // 1hz TUI clock (statusbar/clock refresh, shutdown check)
      g_timeout_add(1000, tui_clock_cb_real, NULL);
      // 20hz reconnect/poll sweep
      g_timeout_add(rr_rtp_supported(false) ? 10 : 50, ws_poll_cb, NULL);
   }

   // Register all of our core event handlers
   rrclient_register_events();

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
   connman_autoconnect();

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
