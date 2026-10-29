#include <sys/stat.h>
#include <rrclient/config.layers.h>
#include <rrclient/database.h>
#include <rrclient/media.direct.h>
#include <rrclient/gtk/gtk.serverpick.h>
//
// rrclient/gtk/gtk.serveredit.c: Server editor
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/gtk/gtk.core.h>
#include "gtk.floating-panel.h"

extern GtkWidget *main_window, *main_notebook;
static RRFloatPanel *server_editor_panel;
static GtkWidget *server_editor_frame;

static void server_editor_frame_destroyed(GtkWidget *widget, gpointer unused) {
   (void)widget;
   (void)unused;
   server_editor_frame = NULL;
}

static void server_editor_close(gpointer unused) {
   (void)unused;
   RRFloatPanel *panel = server_editor_panel;
   server_editor_panel = NULL;
   server_editor_frame = NULL;

   if (panel) {
      rr_floating_panel_destroy(panel);
   }
}


extern dict *cfg;

typedef rr_url_transport_t server_proto_t;

typedef struct serverlist {
   char *name;                  // friendly name
   char *host;                  // host from URL
   int port;                    // parsed port
   char *user;                  // optional user
   char *pass;                  // optional pass
   server_proto_t proto;
   struct serverlist *next;
} serverlist_t;

bool server_parse_url(const char *url, char *host, int *port, char *user, char *pass, server_proto_t *proto) {
   rr_server_url_t parsed;

   if (!rr_server_url_parse(url, &parsed) || strlen(parsed.host) >= 256) {
      return false;
   }
   snprintf(host, 256, "%s", parsed.host);
   *port = parsed.port;
   *proto = parsed.transport;
   user[0] = pass[0] = '\0';

   return true;
}

serverlist_t *serverlist_add(const char *name, const char *url) {
   serverlist_t *sp = malloc(sizeof(serverlist_t));

   if (!sp) {
      return NULL;
   }
   memset(sp, 0, sizeof(*sp));

   char host[256];
   int port = -1;
   char user[HTTP_USER_LEN];
   char pass[HTTP_PASS_LEN];
   server_proto_t proto;

   if (!server_parse_url(url, host, &port, user, pass, &proto)) {
      free(sp);

      return NULL;
   }
   sp->name = strdup(name ? name : "");
   sp->host = strdup(host);
   sp->port = port;
   sp->user = strdup(user);
   sp->pass = strdup(pass);
   sp->proto = proto;
   sp->next = NULL;

   if (!sp->name || !sp->host || !sp->user || !sp->pass) {
      free(sp->name);
      free(sp->host);
      free(sp->user);
      free(sp->pass);
      free(sp);

      return NULL;
   }

   return sp;
}

void serverlist_free(serverlist_t *sp) {
   if (!sp) {
      return;
   }
   free(sp->name);
   free(sp->host);
   free(sp->user);
   free(sp->pass);
   free(sp);
}

// --- lookup helpers ---

serverlist_t *serverlist_find_by_name(serverlist_t *head, const char *name) {
   for (serverlist_t *sp = head ; sp ; sp = sp->next) {
      if (strcmp(sp->name, name) == 0) {
         return sp;
      }
   }

   return NULL;
}

serverlist_t *serverlist_find_by_url(serverlist_t *head, const char *url) {
   char host[256], user[HTTP_USER_LEN], pass[HTTP_PASS_LEN];
   int port = -1;
   server_proto_t proto;

   if (!server_parse_url(url, host, &port, user, pass, &proto)) {
      return NULL;
   }

   for (serverlist_t *sp = head ; sp ; sp = sp->next) {
      if (sp->port == port && sp->proto == proto && strcmp(sp->host, host) == 0 && strcmp(sp->user, user) == 0 && strcmp(sp->pass, pass) == 0) {
         return sp;
      }
   }

   return NULL;
}

typedef struct {
   char *profile;
   GtkWidget *fields[5];
   char *initial[5];
   GtkWidget *error, *direct;
   bool initial_direct;
} GtkServerEditor;

static const char *editor_keys[] = {
   "server.url", "server.user", "server.pass", "server.proxy", "autojoin"
};

/* Existing profiles may still contain the old editor's separate rig list. */
static char *server_editor_rooms(const char *rooms, const char *rig_rooms) {
   gchar *combined = g_strjoin(" ", rooms ? rooms : "", rig_rooms ? rig_rooms : "", NULL);
   gchar **tokens = g_strsplit_set(combined, ", \t\r\n", -1);
   GString *result = g_string_new("");
   GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

   for (unsigned i = 0 ; tokens[i] ; i++) {
      if (!*tokens[i]) {
         continue;
      }
      char *key = g_ascii_strdown(tokens[i], -1);

      if (g_hash_table_contains(seen, key)) {
         g_free(key);
         continue;
      }
      g_hash_table_add(seen, key);

      if (result->len) {
         g_string_append_c(result, ' ');
      }
      g_string_append(result, tokens[i]);
   }

   g_hash_table_destroy(seen);
   g_strfreev(tokens);
   g_free(combined);

   return g_string_free(result, FALSE);
}

static void server_editor_free(gpointer data) {
   GtkServerEditor *editor = data;

   for (unsigned i = 0 ; i < G_N_ELEMENTS(editor_keys) ; i++) {
      g_free(editor->initial[i]);
   }

   g_free(editor->profile);
   g_free(editor);
}

static gboolean server_editor_escape(GtkWidget *widget, GdkEventKey *event, gpointer unused) {
   (void)widget; (void)unused;
   if (event->keyval != GDK_KEY_Escape) return FALSE;
   server_editor_close(NULL);
   return TRUE;
}

static void server_editor_save(GtkButton *button, gpointer data) {
   (void)button;
   GtkWidget *window = GTK_WIDGET(data);
   GtkServerEditor *editor = g_object_get_data(G_OBJECT(window), "rr-server-editor");
   char *normalized = rrclient_database_normalize_url(gtk_entry_get_text(GTK_ENTRY(editor->fields[0])));
   gtk_entry_set_text(GTK_ENTRY(editor->fields[0]), normalized);
   g_free(normalized);
   rr_server_url_t endpoint;
   if (!rr_server_url_parse(gtk_entry_get_text(GTK_ENTRY(editor->fields[0])), &endpoint)) {
      gtk_label_set_text(GTK_LABEL(editor->error), "Enter a valid WS, WSS, IRC or IRCS URL.");
      return;
   }
   if (!cfg) {
      gtk_label_set_text(GTK_LABEL(editor->error), "Client configuration is not loaded.");
      return;
   }
   char *rooms = server_editor_rooms(gtk_entry_get_text(GTK_ENTRY(editor->fields[4])), NULL);
   gtk_entry_set_text(GTK_ENTRY(editor->fields[4]), rooms);
   g_free(rooms);
   bool rooms_changed = strcmp(gtk_entry_get_text(GTK_ENTRY(editor->fields[4])), editor->initial[4]) != 0;
   for (unsigned i = 0; i < G_N_ELEMENTS(editor_keys); ++i) {
      const char *value = gtk_entry_get_text(GTK_ENTRY(editor->fields[i]));
      if (strcmp(value, editor->initial[i])) {
         char key[640];
         snprintf(key, sizeof(key), "server:%s.%s", editor->profile, editor_keys[i]);
         if (!rrclient_database_server_set(editor->profile, editor_keys[i], value)) {
            gtk_label_set_text(GTK_LABEL(editor->error), "Unable to save server profile to SQLite.");
            return;
         }
      }
   }
   bool direct = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(editor->direct));
   if (direct != editor->initial_direct) {
      char key[640];
      snprintf(key, sizeof(key), "server:%s.media.direct.prefer", editor->profile);
      if (!rrclient_database_server_set(editor->profile, "media.direct.prefer", direct ? "true" : "false")) return;
      rrclient_direct_apply_preferences();
   }
   if (rooms_changed) {
      char key[640];
      snprintf(key, sizeof(key), "server:%s.autojoin.rig", editor->profile);
      if (!rrclient_database_server_set(editor->profile, "autojoin.rig", "")) return;
   }
   gtk_server_picker_refresh_profile(editor->profile);
   server_editor_close(NULL);
}

GtkWidget *gtk_show_server_editor(const char *profile) {
   if (server_editor_panel) {
      rr_floating_panel_restore(server_editor_panel);
      rr_floating_panel_show(server_editor_panel);

      return server_editor_frame;
   }
   GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;

   if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) {
      return NULL;
   }
   server_editor_panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook), "serveredit", "Edit server");

   if (!server_editor_panel) {
      return NULL;
   }
   rr_floating_panel_set_close_callback(server_editor_panel, server_editor_close, NULL);
   GtkWidget *window = rr_floating_panel_widget(server_editor_panel);
   server_editor_frame = window;
   g_signal_connect(window, "key-press-event", G_CALLBACK(server_editor_escape), NULL);
   g_signal_connect(window, "destroy", G_CALLBACK(server_editor_frame_destroyed), NULL);
   GtkServerEditor *editor = g_new0(GtkServerEditor, 1);
   editor->profile = g_strdup(profile);
   g_object_set_data_full(G_OBJECT(window), "rr-server-editor", editor, server_editor_free);
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
   gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(server_editor_panel)), grid, TRUE, TRUE, 0);
   const char *labels[] = {
      "URL", "Username", "Password", "Proxy", "Rooms"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(editor_keys) ; i++) {
      const char *value = get_server_property(profile, editor_keys[i]);
      editor->initial[i] = i == 4 ? server_editor_rooms(value, get_server_property(profile, "autojoin.rig")) : g_strdup(value ? value : "");
      editor->fields[i] = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(editor->fields[i]), editor->initial[i]);
      gtk_entry_set_width_chars(GTK_ENTRY(editor->fields[i]), 48);
      gtk_entry_set_visibility(GTK_ENTRY(editor->fields[i]), i != 2);

      if (i == 4) {
         gtk_entry_set_placeholder_text(GTK_ENTRY(editor->fields[i]), "#lobby #station-rig0 #hamradio");
         gtk_widget_set_tooltip_text(editor->fields[i], "Rooms to join, separated by spaces.");
      }
      gtk_grid_attach(GTK_GRID(grid), gtk_label_new(labels[i]), 0, i, 1, 1);
      if (i == 3) {
         const char *effective = editor->initial[i][0] ? editor->initial[i] : cfg_get("server.proxy");
         GtkWidget *proxy_status = gtk_check_button_new_with_label("Proxy configured");
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(proxy_status), effective && *effective);
         gtk_widget_set_sensitive(proxy_status, FALSE);
         gtk_widget_set_tooltip_text(proxy_status, "Read-only: checked if this server uses a proxy, including an inherited global proxy. Proxy settings cannot be changed here.");
         gtk_grid_attach(GTK_GRID(grid), proxy_status, 1, i, 1, 1);
      } else gtk_grid_attach(GTK_GRID(grid), editor->fields[i], 1, i, 1, 1);
   }

   editor->initial_direct = rrclient_direct_preferred(profile);
   editor->direct = gtk_check_button_new_with_label("Prefer direct RTP audio");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(editor->direct), editor->initial_direct);
   gtk_widget_set_tooltip_text(editor->direct, "If checked, try direct RTP audio, then RTP to the nearest server (proxy-rtp), before trying proxy-media. Unchecked tries proxy-media first. This is less desirable but can avoid firewall issues.");
   gtk_grid_attach(GTK_GRID(grid), editor->direct, 0, 5, 2, 1);
   editor->error = gtk_label_new("");
   gtk_grid_attach(GTK_GRID(grid), editor->error, 0, 6, 2, 1);
   GtkWidget *save = gtk_button_new_with_label("Save");
   GtkWidget *cancel = gtk_button_new_with_label("Cancel");
   gtk_grid_attach(GTK_GRID(grid), cancel, 0, 7, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), save, 1, 7, 1, 1);
   g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(server_editor_close), NULL);
   g_signal_connect(save, "clicked", G_CALLBACK(server_editor_save), window);
   rr_floating_panel_show(server_editor_panel);

   return window;
}

GtkWidget *server_edit_create(serverlist_t *sp) {
   return sp ? gtk_show_server_editor(sp->name) : NULL;
}
