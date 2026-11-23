// SPDX-License-Identifier: GPL-2.0-only
// GTK decode output and callsign highlighting (separate from serial terminal).
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <math.h>
#include <string.h>
#include <rrclient/local.modem.h>
#include <rrclient/modem.callsign.h>
#include <rrclient/database.h>
#include <librustyaxe/json.h>
#include "gtk.decoders.h"
#include "gtk.audio-routing.h"

static GtkWidget *decode_notebook;
/* Decoded messages are not serial bytes. Keep this event-driven view separate from VTE so a noisy worker cannot overwrite or reflow an interactive PTY. */
typedef struct {
   GtkWidget *page, *list, *scroll, *compose, *send, *tx_status, *tx_box;
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

/* Compose and send over the same core-owned request path as Routing's
 * Send Modem Text dialog. No direct worker write or GTK-side PTT request. */
static void decode_send_clicked(GtkWidget *widget, gpointer user) {
   (void)widget;
   decode_tab_t *tab=user;
   if (!tab || !tab->compose || !tab->send ||
       !gtk_widget_get_sensitive(tab->send)) return;
   const char *value=gtk_entry_get_text(GTK_ENTRY(tab->compose));
   if (!value || !*value || strchr(value,'\n') || strchr(value,'\r') ||
       strlen(value)>=sizeof(((rr_local_modem_request_t *)0)->text)) {
      if (tab->tx_status)
         gtk_label_set_text(GTK_LABEL(tab->tx_status),"Enter 1–255 bytes of text (no newline)");
      return;
   }
   unsigned slot=(unsigned)(tab-decode_tabs)+1u;
   if (slot<1 || slot>RR_LOCAL_MODEM_MAX) return;
   rr_local_modem_request_t req={.action=RR_LOCAL_MODEM_SEND_TEXT,.slot=slot};
   g_strlcpy(req.text,value,sizeof(req.text));
   rr_local_modem_request(&req);
   /* Sending is asynchronous. Do not say it transmitted: the core may
    * reject missing TX audio/PTT wires or unavailable TX authorization. */
   if (tab->tx_status)
      gtk_label_set_text(GTK_LABEL(tab->tx_status),"TX request queued; awaiting modem/PTT result");
   gtk_entry_set_text(GTK_ENTRY(tab->compose),"");
}
static void decode_close_clicked(GtkButton *button, gpointer user) {
   (void)button;
   decode_tab_t *tab = user;

   if (tab->page) {
      gtk_widget_destroy(tab->page);
   }
}

static decode_tab_t *decode_tab_for(const rr_local_modem_decode_t *d) {
   if (!decode_notebook || d->slot < 1 || d->slot > RR_LOCAL_MODEM_MAX) {
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
   /* A modem's advertised TX-enabled state controls the composer, not
    * the decoder engine name. This leaves room for other text-capable TX
    * workers without adding engine-name conditions to GTK. */
   {
      GtkWidget *tx_box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
      tab->tx_box=tx_box;
      tab->compose=gtk_entry_new();
      gtk_entry_set_max_length(GTK_ENTRY(tab->compose),255);
      gtk_entry_set_placeholder_text(GTK_ENTRY(tab->compose),
         "Type text to send via this modem…");
      gtk_widget_set_tooltip_text(tab->compose,
         "Send one text message. TX requires enabled modem, TX audio and PTT routes, and radio permission.");
      gtk_widget_set_hexpand(tab->compose,TRUE);
      tab->send=gtk_button_new_with_label("Send");
      gtk_widget_set_sensitive(tab->send,FALSE);
      gtk_widget_set_sensitive(tab->compose,FALSE);
      g_signal_connect(tab->send,"clicked",G_CALLBACK(decode_send_clicked),tab);
      g_signal_connect(tab->compose,"activate",G_CALLBACK(decode_send_clicked),tab);
      gtk_box_pack_start(GTK_BOX(tx_box),tab->compose,TRUE,TRUE,0);
      gtk_box_pack_start(GTK_BOX(tx_box),tab->send,FALSE,FALSE,0);
      gtk_box_pack_start(GTK_BOX(tab->page),tx_box,FALSE,FALSE,2);
      tab->tx_status=gtk_label_new("Enable TX for this modem in Routing before composing");
      gtk_label_set_xalign(GTK_LABEL(tab->tx_status),0.f);
      gtk_label_set_ellipsize(GTK_LABEL(tab->tx_status),PANGO_ELLIPSIZE_END);
      gtk_box_pack_start(GTK_BOX(tab->page),tab->tx_status,FALSE,FALSE,2);
   }
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
   gtk_notebook_append_page(GTK_NOTEBOOK(decode_notebook), tab->page, title);
   gtk_widget_show_all(title);
   gtk_widget_show_all(tab->page);
   if (tab->tx_box) {
      gtk_widget_hide(tab->tx_box);
      gtk_widget_hide(tab->tx_status);
   }

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
      decode_tab_t *tab=decode_tab_for(&sample);
      if (tab && tab->compose) {
         /* Hide the whole composer when TX is disabled; a later snapshot
          * can reveal it without recreating the decoder tab or losing RX. */
         gtk_widget_set_visible(tab->tx_box,entry->tx_enabled);
         gtk_widget_set_visible(tab->tx_status,entry->tx_enabled);
         gtk_widget_set_sensitive(tab->compose,entry->tx_enabled);
         gtk_widget_set_sensitive(tab->send,entry->tx_enabled);
         if (tab->tx_status) {
            if (!entry->tx_enabled) {
               gtk_label_set_text(GTK_LABEL(tab->tx_status),
                  "TX disabled; enable in Routing and connect TX audio + PTT");
            } else {
               char prefix[32];
               g_snprintf(prefix,sizeof(prefix),"Modem %u ",entry->slot);
               if (g_str_has_prefix(snapshot->status,prefix))
                  gtk_label_set_text(GTK_LABEL(tab->tx_status),snapshot->status);
               else if (g_str_has_prefix(gtk_label_get_text(GTK_LABEL(tab->tx_status)),
                                         "TX disabled"))
                  gtk_label_set_text(GTK_LABEL(tab->tx_status),
                     "Ready to request TX (requires audio/PTT routes and authorization)");
            }
         }
      }
   }
}


void gtk_decoder_view_init(GtkWidget *notebook,GtkWidget *toolbar) {
   decode_notebook=notebook;
   GtkWidget *highlight_call = gtk_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(highlight_call), "My callsign");
   gtk_entry_set_width_chars(GTK_ENTRY(highlight_call), 13);
   char *stored_call = rrclient_database_setting_get("modem.my_callsign");
   if (stored_call) {
      g_strlcpy(decode_highlight_call, stored_call, sizeof(decode_highlight_call));
      gtk_entry_set_text(GTK_ENTRY(highlight_call), decode_highlight_call);
      g_free(stored_call);
   }
   gtk_widget_set_tooltip_text(highlight_call, "Highlight this callsign and emit watch.callsign events for scripts; saved across restarts");
   g_signal_connect(highlight_call, "changed", G_CALLBACK(decode_set_call), NULL);
   gtk_box_pack_start(GTK_BOX(toolbar), highlight_call, FALSE, FALSE, 0);
   /* Each decoder creates its own decode subtab on its first event. */
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

}

void gtk_decoder_view_shutdown(void) {
   if (decode_token) {
      event_off_token(decode_token);
   }

   if (decode_snapshot_token) {
      event_off_token(decode_snapshot_token);
   }
   decode_token = decode_snapshot_token = 0;
   /* The terminal notebook still owns decoder pages: destroy them before
    * clearing the state, so row follow-idles are cancelled by page teardown. */
   for (unsigned i = 0; i < RR_LOCAL_MODEM_MAX; i++) {
      if (decode_tabs[i].page)
         gtk_widget_destroy(decode_tabs[i].page);
   }
   memset(decode_tabs, 0, sizeof(decode_tabs));
   decode_notebook = NULL;

}

void gtk_decoders_show(void) {
   gtk_audio_graph_present();
}
void gtk_decoders_shutdown(void) {
   gtk_decoder_view_shutdown();
}
