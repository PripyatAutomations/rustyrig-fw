// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// GTK3 serial terminal: VTE presentation over common sercom byte streams.
// This module is optional; rrclient/sercom.c has no GTK or VTE dependency.
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <string.h>
#include <math.h>
#include <librustyaxe/json.h>
#include <rrclient/sercom.h>
#include <rrclient/serial.h>
#include <rrclient/local.modem.h>
#include <rrclient/modem.callsign.h>
#include <rrclient/database.h>
#include "gtk.serial.h"
#include "gtk.decoders.h"
#include <rrclient/gtk/gtk.chat.h>
#ifdef RR_HAVE_VTE
#include <vte/vte.h>
#endif

extern GtkWidget *main_notebook;
typedef struct {
   char *name;
   GtkWidget *page, *view, *baud, *bits, *parity, *stops, *running, *config_status, *connection_status, *apply_button, *endpoint_label, *tab_label,
      *command_entry;
   rr_event_token_t config_token, connection_token;
   rr_sercom_observer_t *observer;
} terminal_session_t;
static GtkWidget *terminal_notebook;
static GtkWidget *terminal_root;
static GtkWidget *terminal_ports;
/* Decoded messages are not serial bytes. Keep this event-driven view separate from VTE so a noisy worker cannot overwrite or reflow an interactive PTY. */
typedef struct {
   GtkWidget *page, *list, *scroll;
   unsigned rows;
   gboolean follow;
   guint follow_idle;
} decode_tab_t;
static decode_tab_t decode_tabs[RR_LOCAL_MODEM_MAX];
static rr_event_token_t decode_token, decode_snapshot_token;
static char decode_highlight_call[32];

static gboolean decode_follow_idle(gpointer data) {
   decode_tab_t *tab = data;
   tab->follow_idle = 0;

   if (!tab->scroll || !tab->follow) {
      return G_SOURCE_REMOVE;
   }
   GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(tab->scroll));
   gtk_adjustment_set_value(adj, MAX(0., gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj)));

   return G_SOURCE_REMOVE;
}

static void decode_scroll_changed(GtkAdjustment *adj, gpointer data) {
   decode_tab_t *tab = data;

   /* Do not interrupt selection/scrollback: resume only when back at bottom. */
   if (!tab->follow_idle) {
      tab->follow = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj) - gtk_adjustment_get_value(adj) < 36.;
   }
}

static void decode_tab_closed(GtkWidget *page, gpointer user) {
   decode_tab_t *tab = user;
   (void)page;

   if (tab->follow_idle) {
      g_source_remove(tab->follow_idle);
   }
   memset(tab, 0, sizeof(*tab));
}

static void decode_close_clicked(GtkButton *button, gpointer user) {
   (void)button;
   decode_tab_t *tab = user;

   if (tab->page) {
      gtk_widget_destroy(tab->page);
   }
}

static decode_tab_t *decode_tab_for(const rr_local_modem_decode_t *d) {
   if (!terminal_notebook || d->slot < 1 || d->slot > RR_LOCAL_MODEM_MAX) {
      return NULL;
   }
   decode_tab_t *tab = &decode_tabs[d->slot - 1];

   if (tab->page) {
      return tab;
   }
   tab->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   tab->follow = TRUE;

   if (!g_ascii_strcasecmp(d->engine, "ft8")) {
      GtkWidget *header = gtk_label_new("UTC       DT       Audio Hz   Sync   Message");
      gtk_widget_set_halign(header, GTK_ALIGN_START);
      gtk_widget_set_margin_start(header, 8);
      gtk_widget_set_margin_top(header, 3);
      gtk_widget_set_margin_bottom(header, 3);
      PangoAttrList *attrs = pango_attr_list_new();
      PangoAttribute *mono = pango_attr_family_new("monospace");
      pango_attr_list_insert(attrs, mono);
      gtk_label_set_attributes(GTK_LABEL(header), attrs);
      pango_attr_list_unref(attrs);
      gtk_box_pack_start(GTK_BOX(tab->page), header, FALSE, FALSE, 0);
   }
   tab->scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tab->scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   tab->list = gtk_list_box_new();
   gtk_list_box_set_selection_mode(GTK_LIST_BOX(tab->list), GTK_SELECTION_NONE);
   gtk_container_add(GTK_CONTAINER(tab->scroll), tab->list);
   gtk_box_pack_start(GTK_BOX(tab->page), tab->scroll, TRUE, TRUE, 0);
   GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(tab->scroll));
   g_signal_connect(adj, "value-changed", G_CALLBACK(decode_scroll_changed), tab);
   GtkWidget *title = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   gchar *name = g_strdup_printf("%s-%u", d->engine, d->slot);
   gtk_box_pack_start(GTK_BOX(title), gtk_label_new(name), FALSE, FALSE, 0);
   g_free(name);
   GtkWidget *close = gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
   gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
   gtk_box_pack_start(GTK_BOX(title), close, FALSE, FALSE, 0);
   g_signal_connect(close, "clicked", G_CALLBACK(decode_close_clicked), tab);
   g_signal_connect(tab->page, "destroy", G_CALLBACK(decode_tab_closed), tab);
   gtk_notebook_append_page(GTK_NOTEBOOK(terminal_notebook), tab->page, title);
   gtk_widget_show_all(title);
   gtk_widget_show_all(tab->page);

   return tab;
}

static gboolean decode_has_call(const char *line, const char *call);
static void decode_set_call(GtkEditable *edit, gpointer unused) {
   (void)unused;
   const char *value = gtk_entry_get_text(GTK_ENTRY(edit));
   g_strlcpy(decode_highlight_call, value, sizeof(decode_highlight_call));
   rrclient_database_setting_set("modem.my_callsign", decode_highlight_call);

   for (unsigned i = 0 ; i < RR_LOCAL_MODEM_MAX ; i++) {
      decode_tab_t *tab = &decode_tabs[i];

      if (!tab->list) {
         continue;
      }
      GList *rows = gtk_container_get_children(GTK_CONTAINER(tab->list));

      for (GList *p = rows ; p ; p = p->next) {
         GtkWidget *row = p->data;
         const char *body = g_object_get_data(G_OBJECT(row), "rr-decoded-text");
         GtkStyleContext *style = gtk_widget_get_style_context(row);

         if (decode_has_call(body, decode_highlight_call)) {
            gtk_style_context_add_class(style, "rr-modem-highlight");
         } else {
            gtk_style_context_remove_class(style, "rr-modem-highlight");
         }
      }

      g_list_free(rows);
   }
}

static gboolean decode_has_call(const char *line, const char *call) {
   return rr_modem_callsign_in_text(line,call);
}

/* Match watched calls as complete call tokens, never substrings (K1AB must not match K1ABC). The RF dial determines band; audio offset is not part of the
 * amateur band selection. Unknown RF matches only "Any". */
typedef struct {
   const char *message, *mode, *band;
   gboolean matched;
} DecodeWatchMatch;
static void decode_watch_match_entry(const rrclient_watched_call_t *rule, void *user) {
   DecodeWatchMatch *match = user;

   if (match->matched || !decode_has_call(match->message, rule->callsign)) {
      return;
   }
   gboolean mode_ok = !g_ascii_strcasecmp(rule->mode, "Any") ||
      !g_ascii_strcasecmp(rule->mode, match->mode) ||
      (!g_ascii_strcasecmp(rule->mode, "PSK31") && (!g_ascii_strcasecmp(match->mode, "BPSK31") || !g_ascii_strcasecmp(match->mode, "QPSK31")));
   gboolean band_ok = !g_ascii_strcasecmp(rule->band, "Any") || (match->band && !g_ascii_strcasecmp(rule->band, match->band));

   if (mode_ok && band_ok) {
      match->matched = TRUE;
   }
}

static gboolean decode_watch_matches(const rr_local_modem_decode_t *d, const char *body) {
   if (!body || !*body) {
      return FALSE;
   }
   DecodeWatchMatch match = {
      .message = body, .mode = rr_modem_callsign_mode(d->engine,d->mode),
      .band = rr_modem_callsign_band(d->frequency_hz)
   };
   rrclient_database_watched_calls_foreach(decode_watch_match_entry, &match);

   return match.matched;
}

/* Format data using the actual decoder JSON schema; sync_score is not SNR. Other decoder families keep their own rendering path as they gain metadata. */
/* The FT8 worker emits a fixed flat JSON record. Read it directly instead of relying on generic dict flattening, which may rename dotted event fields. */
static gchar *decode_field(const char *json, const char *key) {
   if (!json || !key) {
      return NULL;
   }
   gchar *needle = g_strdup_printf("\"%s\"", key);
   const char *p = strstr(json, needle);
   size_t len = strlen(needle);
   g_free(needle);

   if (!p) {
      return NULL;
   }
   p += len;
   while (g_ascii_isspace(*p)) {
      p++;
   }

   if (*p++ != ':') {
      return NULL;
   }
   while (g_ascii_isspace(*p)) {
      p++;
   }

   if (*p == '"') {
      GString *value = g_string_new(NULL);

      for (p++ ; *p && *p != '"' ; p++) {
         if (*p == '\\' && p[1]) {
            p++;

            switch (*p) {
               case 'n': {
                  g_string_append_c(value, '\n');
                  break;
               }
               case 'r': {
                  g_string_append_c(value, '\r');
                  break;
               }
               case 't': {
                  g_string_append_c(value, '\t');
                  break;
               }
               default: {
                  g_string_append_c(value, *p);
                  break;
               }
            }
         } else {
            g_string_append_c(value, *p);
         }
      }

      return g_string_free(value, FALSE);
   }
   const char *end = p;
   while (*end && *end != ',' && *end != '}' && !g_ascii_isspace(*end)) {
      end++;
   }
   return end > p ? g_strndup(p, end - p) : NULL;
}

static gchar *decode_format_line(const rr_local_modem_decode_t *d, char **plain) {
   const char *raw = d->line;
   *plain = NULL;
   gchar *prefix = NULL;
   gchar *type = decode_field(raw, "type");

   if (!g_ascii_strcasecmp(d->engine, "ft8") && type && !strcmp(type, "ft8.decode")) {
      gchar *message = decode_field(raw, "text");
      gchar *slot_text = decode_field(raw, "slot");
      gchar *hz_text = decode_field(raw, "audio_hz");
      gchar *dt_text = decode_field(raw, "dt");
      gchar *sync_text = decode_field(raw, "sync_score");
      gint64 slot = slot_text ? g_ascii_strtoll(slot_text, NULL, 10) : 0;
      GDateTime *utc = slot > 0 ? g_date_time_new_from_unix_utc(slot) : NULL;
      gchar *clock = utc ? g_date_time_format(utc, "%H:%M:%S") : g_strdup("--:--:--");

      if (utc) {
         g_date_time_unref(utc);
      }
      char hz[24] = "--", dt[24] = "--", sync[24] = "--";

      if (hz_text) {
         double value = g_ascii_strtod(hz_text, NULL);

         if (isfinite(value)) {
            g_snprintf(hz, sizeof(hz), "%.0f", value);
         }
      }

      if (dt_text) {
         double value = g_ascii_strtod(dt_text, NULL);

         if (isfinite(value)) {
            g_snprintf(dt, sizeof(dt), "%+.2f", value);
         }
      }

      if (sync_text) {
         g_strlcpy(sync, sync_text, sizeof(sync));
      }
      prefix = g_strdup_printf("%-8s %7s %10s %6s  ", clock, dt, hz, sync);
      *plain = message ? message : g_strdup("");
      g_free(clock);
      g_free(slot_text);
      g_free(hz_text);
      g_free(dt_text);
      g_free(sync_text);
   } else {
      gchar *message = decode_field(raw, "text");
      *plain = message ? message : g_strdup(raw);
   }
   g_free(type);

   if (!prefix) {
      GDateTime *utc = g_date_time_new_now_utc();
      gchar *clock = g_date_time_format(utc, "%H:%M:%S");
      prefix = g_strdup_printf("%-8s  ", clock);
      g_free(clock);
      g_date_time_unref(utc);
   }

   return prefix;
}

static void decode_received(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message || !message->payload || message->length != sizeof(rr_local_modem_decode_t)) {
      return;
   }
   const rr_local_modem_decode_t *d = message->payload;
   decode_tab_t *tab = decode_tab_for(d);

   if (!tab) {
      return;
   }
   char *body = NULL;
   gchar *prefix = decode_format_line(d, &body);
   gboolean cq = g_ascii_strncasecmp(body, "CQ ", 3) == 0;
   gboolean highlight = decode_has_call(body, decode_highlight_call);
   gboolean watched = decode_watch_matches(d, body);
   GtkWidget *row = gtk_list_box_row_new();
   g_object_set_data_full(G_OBJECT(row), "rr-decoded-text", g_strdup(body), g_free);
   GtkStyleContext *style = gtk_widget_get_style_context(row);

   /* The row counter continues across pruning; 1000 retained rows means zebra parity remains stable while the oldest entries are dropped. */
   if (tab->rows & 1) {
      gtk_style_context_add_class(style, "rr-modem-odd");
   }

   if (highlight) {
      gtk_style_context_add_class(style, "rr-modem-highlight");
   }

   if (watched) {
      gtk_style_context_add_class(style, "rr-modem-watched");
   }

   if (cq) {
      gtk_style_context_add_class(style, "rr-modem-cq");
   }
   GtkWidget *label = gtk_label_new(NULL);
   gchar *escaped_prefix = g_markup_escape_text(prefix, -1);
   gchar *escaped_body = g_markup_escape_text(body, -1);
   gboolean reply = strstr(body, "RR73") != NULL || strstr(body, " 73") != NULL;
   gboolean report = !reply && (strstr(body, " -") != NULL || strstr(body, " R-") != NULL);
   const char *message_style = watched ? "foreground='#241e08' weight='bold'" :
      highlight ? "foreground='#241e08'" : cq ? "foreground='#159c72' weight='bold'" : reply ? "foreground='#ae8200'" : report ? "foreground='#3686d0'" : "";
   gchar *markup = g_strdup_printf("<span alpha='75%%'>%s</span><span %s>%s</span>", escaped_prefix, message_style, escaped_body);
   gtk_label_set_markup(GTK_LABEL(label), markup);
   gtk_label_set_xalign(GTK_LABEL(label), 0);
   gtk_label_set_selectable(GTK_LABEL(label), TRUE);
   gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
   gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
   gtk_widget_set_halign(label, GTK_ALIGN_FILL);
   gtk_widget_set_margin_start(label, 8);
   gtk_widget_set_margin_end(label, 8);
   gtk_widget_set_margin_top(label, 2);
   gtk_widget_set_margin_bottom(label, 2);
   gtk_style_context_add_class(gtk_widget_get_style_context(label), "rr-modem-decode-label");
   gtk_widget_set_tooltip_text(label, body);
   gtk_container_add(GTK_CONTAINER(row), label);
   gtk_list_box_insert(GTK_LIST_BOX(tab->list), row, -1);
   gtk_widget_show_all(row);

   if (++tab->rows > 1000) {
      GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(tab->list), 0);

      if (first) {
         gtk_widget_destroy(GTK_WIDGET(first));
      }
      /* Keep total count for stable alternating parity. */
   }

   if (tab->follow && !tab->follow_idle) {
      tab->follow_idle = g_idle_add(decode_follow_idle, tab);
   }
   g_free(markup);
   g_free(escaped_body);
   g_free(escaped_prefix);
   g_free(prefix);
   g_free(body);
}

static void decode_snapshot_received(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message || !message->payload || message->length != sizeof(rr_local_modem_snapshot_t)) {
      return;
   }
   const rr_local_modem_snapshot_t *snapshot = message->payload;

   for (unsigned i = 0 ; i < RR_LOCAL_MODEM_MAX ; i++) {
      const rr_local_modem_entry_t *entry = &snapshot->entries[i];

      if (!entry->running || !entry->engine[0]) {
         if (decode_tabs[i].page) {
            gtk_widget_destroy(decode_tabs[i].page);
         }
         continue;
      }
      rr_local_modem_decode_t sample = {
         .slot = entry->slot
      };
      g_strlcpy(sample.engine, entry->engine, sizeof(sample.engine));
      g_strlcpy(sample.source, entry->source, sizeof(sample.source));
      decode_tab_for(&sample);
   }
}

static void terminal_copy(GtkMenuItem *item, gpointer data) {
   (void)item;
   GtkWidget *view = GTK_WIDGET(data);
#ifdef RR_HAVE_VTE
   vte_terminal_copy_clipboard_format(VTE_TERMINAL(view), VTE_FORMAT_TEXT);
#else
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
   gtk_text_buffer_copy_clipboard(buffer, gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
#endif
}

static void terminal_paste(GtkMenuItem *item, gpointer data) {
   (void)item;
   GtkWidget *view = GTK_WIDGET(data);
#ifdef RR_HAVE_VTE
   vte_terminal_paste_clipboard(VTE_TERMINAL(view));
#else
   (void)view;
#endif
}

static gboolean terminal_key_press(GtkWidget *view, GdkEventKey *event, gpointer unused) {
   (void)unused;

   if (!event || event->type != GDK_KEY_PRESS) {
      return FALSE;
   }
   GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask();

   if (modifiers != (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
      return FALSE;
   }

   if (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C) {
      if (gtk_chat_copy_selection()) {
         return TRUE;
      }
#ifdef RR_HAVE_VTE
      terminal_copy(NULL, view);

      return TRUE;
#else

      return FALSE;
#endif
   }

   if (event->keyval == GDK_KEY_v || event->keyval == GDK_KEY_V) {
#ifdef RR_HAVE_VTE
      terminal_paste(NULL, view);

      return TRUE;
#else

      return FALSE;
#endif
   }

   return FALSE;
}

static void terminal_populate_menu(GtkWidget *view, GtkWidget *menu) {
   GtkWidget *copy = gtk_menu_item_new_with_label("Copy");
   gtk_widget_set_sensitive(copy,
#ifdef RR_HAVE_VTE
      vte_terminal_get_has_selection(VTE_TERMINAL(view))
#else
      gtk_text_buffer_get_has_selection(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)))
#endif
      );
   g_signal_connect(copy, "activate", G_CALLBACK(terminal_copy), view);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy);
#ifdef RR_HAVE_VTE
   GtkWidget *paste = gtk_menu_item_new_with_label("Paste");
   g_signal_connect(paste, "activate", G_CALLBACK(terminal_paste), view);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), paste);
#endif
   gtk_widget_show_all(menu);
}

static gboolean terminal_button_press(GtkWidget *view, GdkEventButton *event, gpointer unused) {
   (void)unused;

   if (!event || event->button != GDK_BUTTON_SECONDARY) {
      return FALSE;
   }
   gtk_widget_grab_focus(view);
   GtkWidget *menu = gtk_menu_new();
   terminal_populate_menu(view, menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);

   return TRUE;
}

static void console_bytes(const char *name, const char *bytes, size_t length, bool incoming, void *user) {
   (void)name;
   (void)incoming;
   terminal_session_t *session = user;

   if (!session->view || !length) {
      return;
   }

   /* Serial terminal is a raw byte monitor, irrespective of endpoint type. Preserve framing, escape sequences, CR/LF and binary payload unchanged. The serial
    * observer sees both PTY input and output; do not filter one direction or insert CAT-specific labels/newlines into the stream. */
#ifdef RR_HAVE_VTE
   vte_terminal_feed(VTE_TERMINAL(session->view), bytes, (gssize) length);
#else
   GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(session->view));
   GtkTextIter end;
   gtk_text_buffer_get_end_iter(buf, &end);
   gtk_text_buffer_insert(buf, &end, bytes, (gint) length);
#endif
}

#ifdef RR_HAVE_VTE
static void terminal_commit(VteTerminal *terminal, gchar *text, guint size, gpointer user) {
   (void)terminal;
   terminal_session_t *session = user;
   /* GPS outputs never accept input. CAT/modem commands use common sercom. */
   const char *service = rr_sercom_binding(session->name);

   if (service && (g_str_has_suffix(service, ".gps-out") || g_str_has_suffix(service, ".nmea-out"))) {
      return;
   }

   if (size && !rr_sercom_console_send(session->name, text, size)) {
      gdk_display_beep(gdk_display_get_default());
   }
}

#endif

/* Optional command input does not modify the monitored stream. For CAT, send exactly what the operator typed, including its ';' terminator. */
static void terminal_send_command(GtkEntry *entry, gpointer user) {
   terminal_session_t *session = user;
   const char *command = gtk_entry_get_text(entry);
   size_t length = strlen(command);

   if (!length) {
      return;
   }

   if (!rr_sercom_console_send(session->name, command, length)) {
      gdk_display_beep(gdk_display_get_default());

      return;
   }
   gtk_entry_set_text(entry, "");
}

static void terminal_settings_apply(GtkButton *button, gpointer user) {
   (void)button;
   terminal_session_t *session = user;
   rr_serial_settings_t settings;

   if (!rr_sercom_line_settings(session->name, &settings)) {
      return;
   }
   const char *baud = gtk_combo_box_get_active_id(GTK_COMBO_BOX(session->baud));
   const char *bits = gtk_combo_box_get_active_id(GTK_COMBO_BOX(session->bits));
   const char *parity = gtk_combo_box_get_active_id(GTK_COMBO_BOX(session->parity));
   const char *stops = gtk_combo_box_get_active_id(GTK_COMBO_BOX(session->stops));

   if (!baud || !bits || !parity || !stops) {
      return;
   }
   settings.baud = (unsigned)g_ascii_strtoull(baud, NULL, 10);
   settings.bits = (unsigned)g_ascii_strtoull(bits, NULL, 10);
   settings.parity = parity[0];
   settings.stops = (unsigned)g_ascii_strtoull(stops, NULL, 10);

   if (!rr_sercom_set_line_settings(session->name, &settings)) {
      gtk_label_set_text(GTK_LABEL(session->config_status), "Apply failed / disconnected");
      gdk_display_beep(gdk_display_get_default());
   } else if (!rr_sercom_binding(session->name) || !g_str_has_prefix(rr_sercom_binding(session->name), "host:")) {
      gtk_label_set_text(GTK_LABEL(session->config_status), "Applied locally");
   }
}

static void terminal_rx_toggle(GtkToggleButton *button, gpointer user) {
   terminal_session_t *session = user;
   rr_serial_t *port = rr_serial_find(session->name);

   if (!port) {
      return;
   }
   gboolean running = gtk_toggle_button_get_active(button);
   rr_serial_read_enabled(port, running);
   gtk_button_set_label(GTK_BUTTON(button), running ? "Stop RX" : "Start RX");
}

static GtkWidget *terminal_setting_combo(const char *const *values, guint n, unsigned current, bool numeric) {
   GtkWidget *combo = gtk_combo_box_text_new();

   for (guint i = 0 ; i < n ; i++) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), values[i], values[i]);
   }

   gchar *id = numeric ? g_strdup_printf("%u", current) : NULL;
   gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), numeric ? id : values[current]);
   g_free(id);

   return combo;
}

static void terminal_toolbar(terminal_session_t *session) {
   GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(session->page), bar, FALSE, FALSE, 3);
   rr_serial_settings_t settings;
   const char *service = rr_sercom_binding(session->name);
   bool configured = rr_sercom_line_settings(session->name, &settings);
   /* CAT/GPS PTYs aren't physical line settings; avoid misleading controls. */
   bool physical = service && (!strcmp(service, "local.raw") || g_str_has_prefix(service, "host:"));

   if (configured && physical) {
      static const char *const rates[] = {
         "300", "1200", "2400", "4800", "9600",
         "19200", "38400", "57600", "115200", "230400", "460800", "921600"
      };
      static const char *const bits[] = {
         "5", "6", "7", "8"
      };
      static const char *const parity[] = {
         "n", "e", "o"
      };
      static const char *const stops[] = {
         "1", "2"
      };
      session->baud = terminal_setting_combo(rates, G_N_ELEMENTS(rates), settings.baud, true);
      session->bits = terminal_setting_combo(bits, G_N_ELEMENTS(bits), settings.bits, true);
      session->parity = terminal_setting_combo(parity, G_N_ELEMENTS(parity), settings.parity == 'e' ? 1 : settings.parity == 'o' ? 2 : 0, false);
      session->stops = terminal_setting_combo(stops, G_N_ELEMENTS(stops), settings.stops, true);
      GtkWidget *entries[] = {
         session->baud, session->bits, session->parity, session->stops
      };
      const char *names[] = {
         "Baud", "Bits", "Parity", "Stop"
      };

      for (guint i = 0 ; i < G_N_ELEMENTS(entries) ; i++) {
         gtk_box_pack_start(GTK_BOX(bar), gtk_label_new(names[i]), FALSE, FALSE, 0);
         gtk_box_pack_start(GTK_BOX(bar), entries[i], FALSE, FALSE, 0);
      }

      GtkWidget *apply = gtk_button_new_with_label("Apply");
      session->apply_button = apply;
      g_signal_connect(apply, "clicked", G_CALLBACK(terminal_settings_apply), session);
      gtk_box_pack_start(GTK_BOX(bar), apply, FALSE, FALSE, 0);
      session->config_status = gtk_label_new("Ready");
      gtk_box_pack_start(GTK_BOX(bar), session->config_status, FALSE, FALSE, 0);

      if (!strcmp(service, "local.raw")) {
         session->running = gtk_toggle_button_new_with_label("Stop RX");
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(session->running), TRUE);
         g_signal_connect(session->running, "toggled", G_CALLBACK(terminal_rx_toggle), session);
         gtk_box_pack_start(GTK_BOX(bar), session->running, FALSE, FALSE, 0);
      }
   } else {
      session->endpoint_label = gtk_label_new("Serial endpoint");
      gtk_box_pack_start(GTK_BOX(bar), session->endpoint_label, FALSE, FALSE, 0);
   }
   session->connection_status = gtk_label_new("CONNECTED");
   gtk_box_pack_end(GTK_BOX(bar), session->connection_status, FALSE, FALSE, 8);
   rr_sercom_info_t info;

   if (!rr_sercom_info(session->name, &info) || (service && g_str_has_prefix(service, "host:") && g_strcmp0(info.state, "Connected"))) {
      gtk_label_set_text(GTK_LABEL(session->connection_status), "UNAVAILABLE");

      if (session->apply_button) {
         gtk_widget_set_sensitive(session->apply_button, FALSE);
      }

      if (service && g_str_has_prefix(service, "host:")) {
         gtk_widget_set_sensitive(session->view, FALSE);
      }
   }
}

static void terminal_configuration(const char *event, const char *data, rrconn_t *connection, void *user) {
   (void)event;
   (void)connection;
   terminal_session_t *session = user;

   if (!session->config_status || !data) {
      return;
   }
   size_t n = strlen(session->name);

   if (strncmp(data, session->name, n) || data[n] != ':') {
      return;
   }
   const char *status = data + n + 1;
   gtk_label_set_text(GTK_LABEL(session->config_status), !strcmp(status, "confirmed") ? "Confirmed by server" :
      !strcmp(status, "pending") ? "Awaiting server…" : !strcmp(status, "rejected") ? "Server rejected settings" : status);
}

static void terminal_connection(const char *event, const char *data, rrconn_t *connection, void *user) {
   (void)event;
   (void)connection;
   terminal_session_t *session = user;

   if (!data || !session->connection_status) {
      return;
   }
   size_t n = strlen(session->name);

   if (strncmp(data, session->name, n) || data[n] != ':') {
      return;
   }
   const char *state = data + n + 1;
   gtk_label_set_text(GTK_LABEL(session->connection_status), state);
   bool ready = !strcmp(state, "CONNECTED");

   if (session->view) {
      gtk_widget_set_sensitive(session->view, ready);
   }

   if (session->apply_button) {
      gtk_widget_set_sensitive(session->apply_button, ready);
   }
}

static void terminal_session_destroy(GtkWidget *widget, gpointer data) {
   (void)widget;
   terminal_session_t *session = data;

   if (session->config_token) {
      event_off_token(session->config_token);
   }

   if (session->connection_token) {
      event_off_token(session->connection_token);
   }
   rr_sercom_unobserve(session->observer);
   session->observer = NULL;
   g_free(session->name);
   g_free(session);
}

/* Release per-page event listeners before ws_fini() emits disconnects. */
void gtk_terminal_shutdown(void) {
   if (terminal_notebook && GTK_IS_NOTEBOOK(terminal_notebook)) {
      while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(terminal_notebook)) > 0) {
         GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(terminal_notebook), 0);
         gtk_widget_destroy(page);  /* Runs terminal_session_destroy synchronously. */
      }
   }
   terminal_notebook = NULL;
   terminal_root = NULL;
   terminal_ports = NULL;

   if (decode_token) {
      event_off_token(decode_token);
   }

   if (decode_snapshot_token) {
      event_off_token(decode_snapshot_token);
   }
   decode_token = decode_snapshot_token = 0;
   memset(decode_tabs, 0, sizeof(decode_tabs));
}

static void terminal_close(GtkButton *button, gpointer data) {
   (void)button;
   GtkWidget *page = data;
   int n = gtk_notebook_page_num(GTK_NOTEBOOK(terminal_notebook), page);

   if (n >= 0) {
      gtk_notebook_remove_page(GTK_NOTEBOOK(terminal_notebook), n);
   }
}

void gtk_terminal_open_endpoint_labeled(const char *name, const char *display_name) {
   /* Console observers can be created before a port is attached. The old rr_serial_find guard silently discarded Open clicks for configured and permanent
    * endpoints that have not connected yet. */
   if (!terminal_notebook || !name || !*name) {
      return;
   }

   if (main_notebook && terminal_root) {
      int page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), terminal_root);

      if (page >= 0) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), page);
      }
   }

   for (int i = 0 ; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(terminal_notebook)) ; i++) {
      GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(terminal_notebook), i);

      if (!g_strcmp0(g_object_get_data(G_OBJECT(page), "serial-name"), name)) {
         if (display_name && *display_name) {
            terminal_session_t *existing = g_object_get_data(G_OBJECT(page), "terminal-session");

            if (existing) {
               if (existing->endpoint_label) {
                  gtk_label_set_text(GTK_LABEL(existing->endpoint_label), display_name);
               }

               if (existing->tab_label) {
                  char *lower = g_utf8_strdown(display_name, -1);
                  gtk_label_set_text(GTK_LABEL(existing->tab_label), lower);
                  g_free(lower);
               }
            }
         }
         gtk_notebook_set_current_page(GTK_NOTEBOOK(terminal_notebook), i);

         return;
      }
   }

   terminal_session_t *session = g_new0(terminal_session_t, 1);
   session->name = g_strdup(name);
   session->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   g_object_set_data(G_OBJECT(session->page), "terminal-session", session);
   g_object_set_data_full(G_OBJECT(session->page), "serial-name", g_strdup(name), g_free);
#ifdef RR_HAVE_VTE
   session->view = vte_terminal_new();
   vte_terminal_set_scrollback_lines(VTE_TERMINAL(session->view), 10000);
   g_signal_connect(session->view, "commit", G_CALLBACK(terminal_commit), session);
#else
   session->view = gtk_text_view_new();
   gtk_text_view_set_editable(GTK_TEXT_VIEW(session->view), FALSE);
   gtk_text_view_set_monospace(GTK_TEXT_VIEW(session->view), TRUE);
#endif
   g_signal_connect(session->view, "key-press-event", G_CALLBACK(terminal_key_press), NULL);
   g_signal_connect(session->view, "button-press-event", G_CALLBACK(terminal_button_press), NULL);
   terminal_toolbar(session);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(scroll), session->view);
   gtk_box_pack_start(GTK_BOX(session->page), scroll, TRUE, TRUE, 0);
   /* Send an exact byte sequence on Enter. No automatic CR/LF or CAT ';':
    * this is useful for both protocol commands and serial line debugging. */
   const char *service = rr_sercom_binding(name);
   gboolean writable = !(service && (g_str_has_suffix(service, ".gps-out") || g_str_has_suffix(service, ".nmea-out")));

   if (writable) {
      session->command_entry = gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(session->command_entry), "Send bytes (e.g. FA; or FB;) — Enter to send exactly");
      gtk_widget_set_tooltip_text(session->command_entry, "Sends exactly the entered bytes; append ';' for CAT commands");
      g_signal_connect(session->command_entry, "activate", G_CALLBACK(terminal_send_command), session);
      gtk_box_pack_end(GTK_BOX(session->page), session->command_entry, FALSE, FALSE, 0);
   }
   session->observer = rr_sercom_observe(name, console_bytes, session);
   session->config_token = event_on_domain_token("serial.console.configuration", terminal_configuration, session, "gtk");
   session->connection_token = event_on_domain_token("serial.console.connection", terminal_connection, session, "gtk");

   if (session->endpoint_label) {
      gtk_label_set_text(GTK_LABEL(session->endpoint_label), display_name && *display_name ? display_name : name);
   }

   if (!session->observer) {
      event_off_token(session->config_token);
      event_off_token(session->connection_token);
      gtk_widget_destroy(session->page);
      g_free(session->name);
      g_free(session);

      return;
   }
   g_signal_connect(session->page, "destroy", G_CALLBACK(terminal_session_destroy), session);
   GtkWidget *tab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   char *lower_title = g_utf8_strdown(display_name && *display_name ? display_name : name, -1);
   session->tab_label = gtk_label_new(lower_title);
   g_free(lower_title);
   gtk_box_pack_start(GTK_BOX(tab), session->tab_label, FALSE, FALSE, 0);
   GtkWidget *close = gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
   gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
   gtk_box_pack_start(GTK_BOX(tab), close, FALSE, FALSE, 0);
   g_signal_connect(close, "clicked", G_CALLBACK(terminal_close), session->page);
   gtk_widget_show_all(tab);
   gtk_notebook_append_page(GTK_NOTEBOOK(terminal_notebook), session->page, tab);
   gtk_widget_show_all(session->page);
   gtk_notebook_set_current_page(GTK_NOTEBOOK(terminal_notebook), gtk_notebook_page_num(GTK_NOTEBOOK(terminal_notebook), session->page));
}

void gtk_terminal_open_endpoint(const char *name) {
   gtk_terminal_open_endpoint_labeled(name, NULL);
}

static void terminal_port_visit(rr_serial_t *port, void *user) {
   GHashTable *seen = user;
   const char *name = rr_serial_name(port);

   if (name && *name && !g_hash_table_contains(seen, name)) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(terminal_ports), name);
      g_hash_table_add(seen, g_strdup(name));
   }
}

static void terminal_refresh(GtkButton *button, gpointer user) {
   (void)button;
   (void)user;
   gchar *previous = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(terminal_ports));
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(terminal_ports));
   GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   /* Permanent auto-follow endpoints are visible regardless of attach timing. */
   const char *permanent[] = {
      "ttyCAT", "ttyGPS", "ttyMODEM"
   };

   for (guint i = 0 ; i < G_N_ELEMENTS(permanent) ; i++) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(terminal_ports), permanent[i]);
      g_hash_table_add(seen, g_strdup(permanent[i]));
   }

   GList *configured = gtk_serial_configured_names();

   for (GList *item = configured ; item ; item = item->next) {
      const char *name = item->data;

      if (!g_hash_table_contains(seen, name)) {
         gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(terminal_ports), name);
         g_hash_table_add(seen, g_strdup(name));
      }
   }

   g_list_free_full(configured, g_free);
   rr_serial_foreach(terminal_port_visit, seen);
   g_hash_table_destroy(seen);
   /* Preserve the selected endpoint when refreshing. */
   GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(terminal_ports));
   GtkTreeIter iter;
   gboolean restored = FALSE;

   for (gboolean valid = gtk_tree_model_get_iter_first(model, &iter) ; valid ; valid = gtk_tree_model_iter_next(model, &iter)) {
      char *name = NULL;
      gtk_tree_model_get(model, &iter, 0, &name, -1);

      if (!g_strcmp0(name, previous)) {
         gtk_combo_box_set_active_iter(GTK_COMBO_BOX(terminal_ports), &iter);
         restored = TRUE;
         g_free(name);
         break;
      }
      g_free(name);
   }

   if (!restored) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(terminal_ports), 0);
   }
   g_free(previous);
}

static void terminal_configure(GtkButton *button, gpointer user) {
   (void)user;
   gchar *name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(terminal_ports));
   gtk_serial_configure(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))), name);
   g_free(name);
   terminal_refresh(NULL, NULL);
}

static void terminal_open(GtkButton *button, gpointer user) {
   (void)button;
   (void)user;
   gchar *name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(terminal_ports));

   if (name) {
      gtk_terminal_open_endpoint(name);
      g_free(name);
   }
}

GtkWidget *init_terminal_tab(void) {
   GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
   terminal_root = root;
   /* Working page, unlike fullscreen Status/Logs: retain side panels. */
   g_object_set_data(G_OBJECT(root), "rr-terminal-page", GINT_TO_POINTER(1));
   GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   terminal_ports = gtk_combo_box_text_new();
   gtk_box_pack_start(GTK_BOX(toolbar), gtk_label_new("Endpoint:"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar), terminal_ports, FALSE, FALSE, 0);
   GtkWidget *refresh = gtk_button_new_with_label("Refresh");
   GtkWidget *open = gtk_button_new_with_label("Open Terminal");
   GtkWidget *configure = gtk_button_new_with_label("Configure…");
   GtkWidget *decoders = gtk_button_new_with_label("Routing…");
   gtk_box_pack_start(GTK_BOX(toolbar), refresh, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar), open, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar), configure, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar), decoders, FALSE, FALSE, 0);
   GtkWidget *highlight_call = gtk_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(highlight_call), "My callsign");
   gtk_entry_set_width_chars(GTK_ENTRY(highlight_call), 13);
   char *stored_call = rrclient_database_setting_get("modem.my_callsign");
   if (stored_call) {
      g_strlcpy(decode_highlight_call, stored_call, sizeof(decode_highlight_call));
      gtk_entry_set_text(GTK_ENTRY(highlight_call), decode_highlight_call);
      g_free(stored_call);
   }
   gtk_widget_set_tooltip_text(highlight_call, "Highlight this callsign and emit modem.callsign events for scripts; saved across restarts");
   g_signal_connect(highlight_call, "changed", G_CALLBACK(decode_set_call), NULL);
   gtk_box_pack_start(GTK_BOX(toolbar), highlight_call, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 5);
   terminal_notebook = gtk_notebook_new();
   gtk_notebook_set_tab_pos(GTK_NOTEBOOK(terminal_notebook), GTK_POS_TOP);
   gtk_notebook_set_scrollable(GTK_NOTEBOOK(terminal_notebook), TRUE);
   gtk_box_pack_start(GTK_BOX(root), terminal_notebook, TRUE, TRUE, 0);
   /* Each decoder creates its own terminal subtab on its first event. */
   decode_token = event_on_message_domain_token(RR_LOCAL_MODEM_DECODE, decode_received, NULL, "gtk");
   decode_snapshot_token = event_on_message_domain_token(RR_LOCAL_MODEM_RESULT, decode_snapshot_received, NULL, "gtk");
   rr_local_modem_request_t request = {
      .action = RR_LOCAL_MODEM_SNAPSHOT
   };
   rr_local_modem_request(&request);
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css, "row.rr-modem-odd { background-image: none; background-color: alpha(@theme_fg_color, 0.18); }"
      "label.rr-modem-decode-label { font-family: monospace; }"
      "row.rr-modem-highlight, row.rr-modem-highlight.rr-modem-odd {"
      " background-image: none; background-color: #ffe073; }"
      "row.rr-modem-highlight label { color: #241e08; }"
      "row.rr-modem-watched, row.rr-modem-watched.rr-modem-odd,"
      "row.rr-modem-watched.rr-modem-highlight {"
      " background-image: none; background-color: #ffac36; }" "row.rr-modem-watched label { color: #241e08; }", -1, NULL);
   gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(css);

   g_signal_connect(refresh, "clicked", G_CALLBACK(terminal_refresh), NULL);
   g_signal_connect(open, "clicked", G_CALLBACK(terminal_open), NULL);
   g_signal_connect(configure, "clicked", G_CALLBACK(terminal_configure), NULL);
   g_signal_connect_swapped(decoders, "clicked", G_CALLBACK(gtk_decoders_show), NULL);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), root, gtk_label_new("terminal"));
   terminal_refresh(NULL, NULL);

   return root;
}
