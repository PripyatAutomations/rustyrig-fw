// GTK3 serial terminal: VTE presentation over common sercom byte streams.
// This module is optional; rrclient/sercom.c has no GTK or VTE dependency.
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <string.h>
#include <rrclient/sercom.h>
#include <rrclient/serial.h>
#include "gtk.serial.h"
#ifdef RR_HAVE_VTE
#include <vte/vte.h>
#endif

extern GtkWidget *main_notebook;
typedef struct {
   char *name;
   GtkWidget *page, *view, *baud, *bits, *parity, *stops, *running, *config_status, *connection_status, *apply_button, *endpoint_label, *tab_label;
   rr_event_token_t config_token, connection_token;
   rr_sercom_observer_t *observer;
} terminal_session_t;
static GtkWidget *terminal_notebook;
static GtkWidget *terminal_root;
static GtkWidget *terminal_ports;

static void console_bytes(const char *name, const char *bytes, size_t length, bool incoming, void *user) {
   (void)name;
   terminal_session_t *session = user;

   if (!session->view || !incoming || !length) {
      return;
   }
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
   if (!terminal_notebook || !name || !*name || !rr_serial_find(name)) {
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
   terminal_toolbar(session);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(scroll), session->view);
   gtk_box_pack_start(GTK_BOX(session->page), scroll, TRUE, TRUE, 0);
   session->observer = rr_sercom_observe(name, console_bytes, session);
   session->config_token = event_on_token("serial.console.configuration", terminal_configuration, session);
   session->connection_token = event_on_token("serial.console.connection", terminal_connection, session);

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
   GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   terminal_ports = gtk_combo_box_text_new();
   gtk_box_pack_start(GTK_BOX(toolbar), gtk_label_new("Endpoint:"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar), terminal_ports, FALSE, FALSE, 0);
   GtkWidget *refresh = gtk_button_new_with_label("Refresh");
   GtkWidget *open = gtk_button_new_with_label("Open console");
   GtkWidget *configure = gtk_button_new_with_label("Configure…");
   gtk_box_pack_start(GTK_BOX(toolbar), refresh, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar), open, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar), configure, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 5);
   terminal_notebook = gtk_notebook_new();
   gtk_notebook_set_tab_pos(GTK_NOTEBOOK(terminal_notebook), GTK_POS_TOP);
   gtk_notebook_set_scrollable(GTK_NOTEBOOK(terminal_notebook), TRUE);
   gtk_box_pack_start(GTK_BOX(root), terminal_notebook, TRUE, TRUE, 0);
   g_signal_connect(refresh, "clicked", G_CALLBACK(terminal_refresh), NULL);
   g_signal_connect(open, "clicked", G_CALLBACK(terminal_open), NULL);
   g_signal_connect(configure, "clicked", G_CALLBACK(terminal_configure), NULL);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), root, gtk_label_new("terminal"));
   terminal_refresh(NULL, NULL);

   return root;
}
