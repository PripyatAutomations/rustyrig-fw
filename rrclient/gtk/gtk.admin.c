// Database-backed administration. Server permissions remain authoritative.
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <librustyaxe/util.time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.privileges.h>

extern GtkWidget *main_notebook;
GtkWidget *admin_view;
GtkWidget *admin_tab;

typedef struct {
   const char *name;
   const char *fields[10];
   GtkListStore *store;
   GtkWidget *tree, *older, *status;
   char profile[128], next[32];
} admin_table_t;

static admin_table_t tables[] = {
   {
      .name = "users", .fields = {
         "name", "enabled", "permissions", "email", "maxsessions", "password_set", "password_expires", "password_change_required", "uid",
         "full_rig_access"
      }
   },
   {
      .name = "rooms", .fields = {
         "name", "uuid", "deleted", "topic", "has_vfos", "vfo_mask"
      }
   },
   /* PARITY: rustyrig-www/js/webui.admin.js adminTables. Fields come from rrserver/events.c. */
   {
      .name = "quotas", .fields = {
         "name", "tx_remaining", "tx_used", "bandwidth_remaining", "bandwidth_tx_used", "bandwidth_rx_used"
      }
   },
   {
      .name = "audit", .fields = {
         "id", "timestamp", "username", "event_type", "details"
      }
   },
   /* PARITY: rustyrig-www/js/webui.admin.js adminTables.ptt */
   {
      .name = "ptt", .fields = {
         "id", "start_time", "username", "vfo", "frequency", "mode", "duration", "stop_reason", "recording_id", "server"
      }
   },
   {
      .name = "sessions", .fields = {
         "id", "username", "ip", "client", "server", "connected", "kickable", "serial_ports"
      }
   },
   {
      .name = "privsets", .fields = {
         "name", "description", "created_by", "created_at"
      }
   },
   {
      .name = "setentries", .fields = {
         "set_name", "privilege", "effect"
      }
   },
   {
      .name = "assignments", .fields = {
         "set_name", "subject_user", "subject_host", "resource_kind", "resource_uuid", "expires_at"
      }
   },
   {
      .name = "grants", .fields = {
         "subject_user", "subject_host", "resource_kind", "resource_uuid", "privilege", "effect", "expires_at"
      }
   },
};

/* Server-exported node controls. The UI knows only typed descriptors, not individual hardware providers or platform-specific paths. */
static GtkWidget *control_combo, *control_scale, *control_status, *control_apply;
static GtkWidget *group_tree;
static GtkTreeStore *group_store;
static GtkWidget *control_rows_box, *control_filter, *control_scroll, *control_hosts;
static gulong control_host_handler;
static guint control_pending_lists;
static GHashTable *control_row_buttons;
static char *control_pending_name;
static char *control_server_profile;
static bool admin_control_request(const char *name, int value);
static bool admin_control_request_profile(const char *profile, const char *name, int value);
static void admin_control_rows_clear(void);
static void admin_control_update_revert(void);
static void admin_control_refresh(GtkButton *button, gpointer user);
static void admin_control_row_add(dict *data, const char *profile);
/* The client owns local controls; they need not be exported to a server. Do not expose sysfs paths or allow names supplied by a remote peer. */
#ifdef __linux__
static bool local_backlight_path(char *out, size_t capacity) {
   DIR *dir = opendir("/sys/class/backlight");

   if (!dir) {
      return false;
   }
   struct dirent *item;
   char device[80] = "";
   unsigned found = 0;
   while ((item = readdir(dir))) {
      if (item->d_name[0] == '.') {
         continue;
      }

      if (++found != 1 || strlen(item->d_name) >= sizeof(device)) {
         break;
      }
      snprintf(device, sizeof(device), "%s", item->d_name);
   }
   closedir(dir);

   if (found != 1) {
      return false;
   }

   for (const char *p = device ; *p ; p++) {
      if (!g_ascii_isalnum(*p) && *p != '_' && *p != '-' && *p != '.') {
         return false;
      }
   }

   return snprintf(out, capacity, "/sys/class/backlight/%s", device) > 0;
}
static bool local_backlight_read(int *current, int *maximum) {
   char path[192], attribute[240];

   if (!local_backlight_path(path, sizeof(path))) {
      return false;
   }
   snprintf(attribute, sizeof(attribute), "%s/max_brightness", path);
   FILE *f = fopen(attribute, "r");

   if (!f) {
      return false;
   }
   bool ok = fscanf(f, "%d", maximum) == 1 && *maximum > 0;
   fclose(f);

   if (!ok) {
      return false;
   }
   snprintf(attribute, sizeof(attribute), "%s/brightness", path);
   f = fopen(attribute, "r");

   if (!f) {
      return false;
   }
   ok = fscanf(f, "%d", current) == 1 && *current >= 0 && *current <= *maximum;
   fclose(f);

   return ok;
}
static bool local_backlight_writable(void) {
   char path[192], attribute[240];

   if (!local_backlight_path(path, sizeof(path))) {
      return false;
   }
   snprintf(attribute, sizeof(attribute), "%s/brightness", path);

   return access(attribute, W_OK) == 0;
}
static bool local_backlight_write(int value) {
   int current = 0, maximum = 0;
   char path[192], attribute[240];

   if (!local_backlight_read(&current, &maximum) || value < 1 || value > maximum || !local_backlight_path(path, sizeof(path))) {
      return false;
   }
   snprintf(attribute, sizeof(attribute), "%s/brightness", path);
   int fd = open(attribute, O_WRONLY | O_CLOEXEC);

   if (fd < 0) {
      return false;
   }
   char text[32];
   int n = snprintf(text, sizeof(text), "%d\n", value);
   bool ok = n > 0 && write(fd, text, (size_t)n) == n;
   close(fd);

   return ok;
}
#endif
static void admin_control_local_discover(void) {
#ifdef __linux__
   int current = 0, maximum = 0;

   if (local_backlight_read(&current, &maximum)) {
      dict *state = dict_new();

      if (!state) {
         return;
      }
      char str[32];
      dict_add(state, "name", "display.brightness");
      dict_add(state, "label", "LCD Brightness");
      dict_add(state, "unit", "level");
      dict_add(state, "privilege", "local");
      dict_add(state, "minimum", "1");
      snprintf(str, sizeof(str), "%d", maximum);
      dict_add(state, "maximum", str);
      snprintf(str, sizeof(str), "%d", current);
      dict_add(state, "current", str);
      dict_add(state, "step", "1");
      dict_add_bool(state, "writable", local_backlight_writable());
      admin_control_row_add(state, "local");
      dict_free(state);

      return;
   }
#endif
   gtk_label_set_text(GTK_LABEL(control_status), "No local adjustable controls detected");
}
static void admin_control_filter_apply(void) {
   if (!control_filter || !control_rows_box) {
      return;
   }
   const char *query = gtk_entry_get_text(GTK_ENTRY(control_filter));
   char mode = (*query == '=' || *query == '^') ? *query++ : 0;
   GList *rows = gtk_container_get_children(GTK_CONTAINER(control_rows_box));

   for (GList *l = rows ; l ; l = l->next) {
      GtkWidget *row = l->data;
      const char *name = g_object_get_data(G_OBJECT(row), "control-path");
      const char *label = g_object_get_data(G_OBJECT(row), "control-label");
      gboolean visible = !*query ||
         (mode == '^' ? (name && !g_ascii_strncasecmp(name, query, strlen(query))) :
            ((name && strcasestr(name, query)) || (!mode && label && strcasestr(label, query))));
      gtk_widget_set_no_show_all(row, !visible);
      gtk_widget_set_visible(row, visible);
   }

   g_list_free(rows);
}
static void admin_control_hosts_refresh(void) {
   if (!control_hosts) {
      return;
   }
   const char *chosen = gtk_combo_box_get_active_id(GTK_COMBO_BOX(control_hosts));
   char *remember = g_strdup(chosen ? chosen : "local");

   if (control_host_handler) {
      g_signal_handler_block(control_hosts, control_host_handler);
   }
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(control_hosts));
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(control_hosts), "local", "This Client");

   for (unsigned i = 0 ;; ++i) {
      const char *profile = rrclient_connection_iter(i);

      if (!profile) {
         break;
      }
      rrconn_t *connection = rrclient_connection_find(profile);

      if (!connection || !connection->is_ws || !connection->authenticated) {
         continue;
      }
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(control_hosts), profile, profile);
   }

   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(control_hosts), "all", "All Nodes");

   if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(control_hosts), remember)) {
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(control_hosts), "local");
   }
   g_free(remember);

   if (control_host_handler) {
      g_signal_handler_unblock(control_hosts, control_host_handler);
   }
}
static void admin_control_host_changed(GtkComboBox *box, gpointer unused) {
   (void)unused;
   const char *profile = gtk_combo_box_get_active_id(box);
   g_free(control_server_profile);
   control_server_profile = (profile && strcmp(profile, "local") && strcmp(profile, "all")) ? g_strdup(profile) : NULL;
   admin_control_rows_clear();
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(control_combo));
   g_clear_pointer(&control_pending_name, g_free);
   admin_control_refresh(NULL, NULL);
}
static gboolean admin_controls_star(GtkWidget *widget, GdkEventKey *event, gpointer unused) {
   (void)widget;
   (void)unused;

   if (!control_hosts || !admin_view || !main_window) {
      return FALSE;
   }

   if (event->keyval != GDK_KEY_asterisk && event->keyval != GDK_KEY_KP_Multiply) {
      return FALSE;
   }
   GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(main_window));

   if (focus && GTK_IS_EDITABLE(focus)) {
      return FALSE;
   }
   GtkWidget *page = control_combo ? g_object_get_data(G_OBJECT(control_combo), "control-page") : NULL;

   if (!page || gtk_notebook_get_nth_page(GTK_NOTEBOOK(admin_view), gtk_notebook_get_current_page(GTK_NOTEBOOK(admin_view))) != page) {
      return FALSE;
   }
   gtk_combo_box_set_active_id(GTK_COMBO_BOX(control_hosts), "all");

   return TRUE;
}
static void admin_control_filter_changed(GtkEditable *entry, gpointer unused) {
   (void)entry;
   (void)unused;
   admin_control_filter_apply();
}
static gboolean admin_control_scroll_to_selected(gpointer unused) {
   (void)unused;

   if (!control_rows_box || !control_scroll || !control_combo || !control_row_buttons) {
      return G_SOURCE_REMOVE;
   }
   const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(control_combo));
   char *identity = g_strdup_printf("%s/%s", control_server_profile ? control_server_profile : "", id ? id : "");
   GtkWidget *button = control_row_buttons ? g_hash_table_lookup(control_row_buttons, identity) : NULL;
   g_free(identity);

   if (!button || !gtk_widget_get_visible(button)) {
      return G_SOURCE_REMOVE;
   }
   GtkAllocation area;
   gtk_widget_get_allocation(button, &area);
   GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(control_scroll));
   double bottom = MAX(gtk_adjustment_get_lower(adj), gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
   gtk_adjustment_set_value(adj, CLAMP((double)area.y - 24, gtk_adjustment_get_lower(adj), bottom));

   return G_SOURCE_REMOVE;
}
static void admin_control_choose(const char *name) {
   if (!name || !*name || !control_combo) {
      return;
   }

   if (gtk_combo_box_set_active_id(GTK_COMBO_BOX(control_combo), name)) {
      g_clear_pointer(&control_pending_name, g_free);

      if (control_filter) {
         gtk_entry_set_text(GTK_ENTRY(control_filter), "");
      }
      g_idle_add(admin_control_scroll_to_selected, NULL);

      if (admin_view && main_notebook) {
         int outer = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), admin_view);

         if (outer >= 0) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), outer);
         }
      }
   } else {
      g_free(control_pending_name);
      control_pending_name = g_strdup(name);
   }
}
void gtk_admin_open_control_on_server(const char *profile, const char *name) {
   if (!profile || !*profile) {
      return;
   }
   gboolean server_changed = g_strcmp0(control_server_profile, profile) != 0;
   g_free(control_server_profile);
   control_server_profile = g_strdup(profile);
   admin_control_hosts_refresh();

   if (control_hosts) {
      if (control_host_handler) {
         g_signal_handler_block(control_hosts, control_host_handler);
      }

      if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(control_hosts), profile)) {
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(control_hosts), profile, profile);
         gtk_combo_box_set_active_id(GTK_COMBO_BOX(control_hosts), profile);
      }

      if (control_host_handler) {
         g_signal_handler_unblock(control_hosts, control_host_handler);
      }
   }

   if (server_changed && control_combo) {
      gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(control_combo));
      admin_control_rows_clear();
   }

   if (!admin_view || !main_notebook) {
      return;
   }
   int outer = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), admin_view);

   if (outer >= 0) {
      gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), outer);
   }
   GtkWidget *page = control_combo ? g_object_get_data(G_OBJECT(control_combo), "control-page") : NULL;

   if (page) {
      int inner = gtk_notebook_page_num(GTK_NOTEBOOK(admin_view), page);

      if (inner >= 0) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(admin_view), inner);
      }
   }

   if (name && *name) {
      admin_control_choose(name);

      if (server_changed || control_pending_name) {
         admin_control_request(NULL, -1);
      }
   } else if (server_changed) {
      admin_control_request(NULL, -1);
   }
}
void gtk_admin_open_control(const char *name) {
   gtk_admin_open_control_on_server(rrclient_selected_server(), name);
}

/* Sort current row widgets in place; preserves each live slider and owner. */
static int control_sort_field = 0;
static gboolean control_sort_descending = FALSE;
static gint admin_control_row_compare(gconstpointer left, gconstpointer right) {
   GtkWidget *a = (GtkWidget *) left, *b = (GtkWidget *) right;
   const char *key = control_sort_field == 0 ? "control-path" : control_sort_field == 1 ? "control-label" : "control-description";
   const char *av = g_object_get_data(G_OBJECT(a), key);
   const char *bv = g_object_get_data(G_OBJECT(b), key);
   gint cmp = g_utf8_collate(av ? av : "", bv ? bv : "");

   if (!cmp) {
      cmp = g_strcmp0(g_object_get_data(G_OBJECT(a), "control-path"), g_object_get_data(G_OBJECT(b), "control-path"));
   }

   return control_sort_descending ? -cmp : cmp;
}
static void admin_control_sort_rows(void) {
   if (!control_rows_box) {
      return;
   }
   GList *children = gtk_container_get_children(GTK_CONTAINER(control_rows_box));
   children = g_list_sort(children, admin_control_row_compare);
   int position = 0;

   for (GList *l = children ; l ; l = l->next) {
      gtk_box_reorder_child(GTK_BOX(control_rows_box), GTK_WIDGET(l->data), position++);
   }

   g_list_free(children);
}
static void admin_control_sort_clicked(GtkButton *button, gpointer user) {
   (void)user;
   int field = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "sort-field"));

   if (field == control_sort_field) {
      control_sort_descending = !control_sort_descending;
   } else {
      control_sort_field = field;
      control_sort_descending = FALSE;
   }
   admin_control_sort_rows();
}
static void admin_control_rows_clear(void) {
   if (!control_rows_box) {
      return;
   }
   GList *children = gtk_container_get_children(GTK_CONTAINER(control_rows_box));

   for (GList *item = children ; item ; item = item->next) {
      gtk_widget_destroy(GTK_WIDGET(item->data));
   }

   g_list_free(children);

   if (control_row_buttons) {
      g_hash_table_remove_all(control_row_buttons);
   }
   admin_control_update_revert();
}

/* A control is committed once the operator releases its slider. Keep the pre-edit value for Revert; never issue network writes during drag. */
static bool admin_control_write(GtkWidget *row, int value) {
   const char *name = g_object_get_data(G_OBJECT(row), "control-name");
   const char *profile = g_object_get_data(G_OBJECT(row), "control-profile");

   if (!name || !profile) {
      return false;
   }

   if (!strcmp(profile, "local")) {
#ifdef __linux__

      if (!strcmp(name, "display.brightness")) {
         return local_backlight_write(value);
      }
#endif

      return false;
   }

   return admin_control_request_profile(profile, name, value);
}
static void admin_control_update_revert(void) {
   gboolean can_revert = FALSE;

   if (control_rows_box) {
      GList *rows = gtk_container_get_children(GTK_CONTAINER(control_rows_box));

      for (GList *l = rows ; l ; l = l->next) {
         if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(l->data), "control-dirty"))) {
            can_revert = TRUE;
            break;
         }
      }

      g_list_free(rows);
   }

   if (control_apply) {
      gtk_widget_set_sensitive(control_apply, can_revert);
   }
}
static gboolean admin_control_slider_released(GtkWidget *scale, GdkEventButton *event, gpointer data) {
   if (event->button != 1) {
      return FALSE;
   }
   GtkWidget *row = GTK_WIDGET(data);

   if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "control-writable"))) {
      return FALSE;
   }
   int value = (int)gtk_range_get_value(GTK_RANGE(scale));
   int current = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "control-original"));

   if (value == current) {
      return FALSE;
   }

   if (!admin_control_write(row, value)) {
      gtk_range_set_value(GTK_RANGE(scale), current);
      gtk_label_set_text(GTK_LABEL(control_status), "Unable to submit control change");

      return FALSE;
   }

   if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "control-dirty"))) {
      g_object_set_data(G_OBJECT(row), "control-previous", GINT_TO_POINTER(current));
      g_object_set_data(G_OBJECT(row), "control-dirty", GINT_TO_POINTER(TRUE));
   }
   /* Server acknowledgement may update the authoritative value later. */
   g_object_set_data(G_OBJECT(row), "control-original", GINT_TO_POINTER(value));
   admin_control_update_revert();
   gtk_label_set_text(GTK_LABEL(control_status), "Control change submitted");

   return FALSE;
}
static void admin_control_row_state(dict *data, const char *profile) {
   const char *name = dict_get(data, "name", "");
   char *identity = g_strdup_printf("%s/%s", profile, name);
   GtkWidget *row = control_row_buttons ? g_hash_table_lookup(control_row_buttons, identity) : NULL;
   g_free(identity);

   if (!row) {
      return;
   }
   GtkWidget *scale = g_object_get_data(G_OBJECT(row), "control-scale");
   GtkWidget *minimum_label = g_object_get_data(G_OBJECT(row), "control-min-label");
   GtkWidget *maximum_label = g_object_get_data(G_OBJECT(row), "control-max-label");
   GtkWidget *detail = g_object_get_data(G_OBJECT(row), "control-detail");
   int minimum = atoi(dict_get(data, "minimum", "0"));
   int maximum = atoi(dict_get(data, "maximum", "0"));
   int current = atoi(dict_get(data, "current", "0"));
   int step = atoi(dict_get(data, "step", "1"));
   gboolean writable = dict_get_bool(data, "writable", false);

   if (maximum >= minimum && step > 0 && current >= minimum && current <= maximum) {
      gtk_range_set_range(GTK_RANGE(scale), minimum, maximum);
      gtk_range_set_increments(GTK_RANGE(scale), step, step);
      gtk_range_set_value(GTK_RANGE(scale), current);
   }
   gtk_widget_set_sensitive(scale, writable);
   char str[40];
   snprintf(str, sizeof(str), "%d", minimum);
   gtk_label_set_text(GTK_LABEL(minimum_label), str);
   snprintf(str, sizeof(str), "%d", maximum);
   gtk_label_set_text(GTK_LABEL(maximum_label), str);
   g_object_set_data(G_OBJECT(row), "control-original", GINT_TO_POINTER(current));
   g_object_set_data(G_OBJECT(row), "control-writable", GINT_TO_POINTER(writable));
   char *summary = g_strdup_printf("%s — %s%s%s (%s)", g_object_get_data(G_OBJECT(row), "control-label") ? (const char *)g_object_get_data(G_OBJECT(row),
      "control-label") : "Control", writable ? "Writable" : "Read-only", *dict_get(data, "unit", "") ? "; unit: " : "", dict_get(data, "unit", ""), dict_get(
      data, "privilege", "owner|admin"));
   gtk_label_set_text(GTK_LABEL(detail), summary);
   g_free(summary);
}
static void admin_control_row_add(dict *data, const char *profile) {
   const char *name = dict_get(data, "name", "");
   char *identity = g_strdup_printf("%s/%s", profile, name);

   if (!*name || !control_rows_box) {
      g_free(identity);

      return;
   }

   if (control_row_buttons && g_hash_table_contains(control_row_buttons, identity)) {
      admin_control_row_state(data, profile);
      g_free(identity);

      return;
   }
   const char *label = dict_get(data, "label", name);
   GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
   GtkWidget *title = gtk_label_new(!strcmp(profile, "local") ? "This Client/display.brightness" : identity);
   gtk_label_set_xalign(GTK_LABEL(title), 0);
   gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
   gtk_widget_set_size_request(title, 110, -1);
   gtk_widget_set_hexpand(title, TRUE);
   gtk_box_pack_start(GTK_BOX(row), title, TRUE, TRUE, 0);
   GtkWidget *editor = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_widget_set_size_request(editor, 125, -1);
   gtk_widget_set_hexpand(editor, TRUE);
   GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_scale_set_digits(GTK_SCALE(scale), 0);
   gtk_widget_set_hexpand(scale, TRUE);
   GtkWidget *minimum_label = gtk_label_new("0"), *maximum_label = gtk_label_new("100");
   gtk_box_pack_start(GTK_BOX(editor), minimum_label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(editor), scale, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(editor), maximum_label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(row), editor, TRUE, TRUE, 0);
   GtkWidget *detail = gtk_label_new("");
   gtk_label_set_xalign(GTK_LABEL(detail), 0);
   gtk_label_set_line_wrap(GTK_LABEL(detail), TRUE);
   gtk_box_pack_start(GTK_BOX(row), detail, TRUE, TRUE, 0);
   g_object_set_data_full(G_OBJECT(row), "control-name", g_strdup(name), g_free);
   g_object_set_data_full(G_OBJECT(row), "control-profile", g_strdup(profile), g_free);
   g_object_set_data_full(G_OBJECT(row), "control-path", g_strdup(identity), g_free);
   g_object_set_data(G_OBJECT(row), "control-min-label", minimum_label);
   g_object_set_data(G_OBJECT(row), "control-max-label", maximum_label);
   g_object_set_data_full(G_OBJECT(row), "control-label", g_strdup(label), g_free);
   g_object_set_data_full(G_OBJECT(row), "control-description", g_strdup(dict_get(data, "description", label)), g_free);
   g_object_set_data(G_OBJECT(row), "control-scale", scale);
   g_object_set_data(G_OBJECT(row), "control-detail", detail);
   g_signal_connect(scale, "button-release-event", G_CALLBACK(admin_control_slider_released), row);
   gtk_box_pack_start(GTK_BOX(control_rows_box), row, FALSE, FALSE, 3);

   if (control_row_buttons) {
      g_hash_table_insert(control_row_buttons, g_strdup(identity), row);
   }
   admin_control_row_state(data, profile);
   g_free(identity);
   gtk_widget_show_all(row);
   admin_control_sort_rows();
   admin_control_filter_apply();
}

static bool admin_control_request_profile(const char *profile, const char *name, int value) {
   rrconn_t *connection = profile ? rrclient_connection_find(profile) : NULL;

   if (!connection || !connection->is_ws || !connection->authenticated) {
      return false;
   }
   dict *d = dict_new();

   if (!d) {
      return false;
   }
   char data[160];

   if (!name) {
      snprintf(data, sizeof(data), "controls");
   } else if (value < 0) {
      snprintf(data, sizeof(data), "control %s", name);
   } else {
      snprintf(data, sizeof(data), "control %s %d", name, value);
   }
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "admin");
   dict_add(d, "talk.data", data);
   bool ok = ws_send_dict(NULL, connection, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return ok;
}
static bool admin_control_request(const char *name, int value) {
   return admin_control_request_profile(control_server_profile, name, value);
}
static void admin_control_refresh(GtkButton *button, gpointer user) {
   (void)button;
   (void)user;
   admin_control_hosts_refresh();

   if (control_scale) {
      gtk_widget_set_sensitive(control_scale, FALSE);
   }
   admin_control_rows_clear();
   const char *host = control_hosts ? gtk_combo_box_get_active_id(GTK_COMBO_BOX(control_hosts)) : NULL;
   control_pending_lists = 0;

   if (host && !strcmp(host, "all")) {
      for (unsigned i = 0 ;; ++i) {
         const char *profile = rrclient_connection_iter(i);

         if (!profile) {
            break;
         }

         if (admin_control_request_profile(profile, NULL, -1)) {
            ++control_pending_lists;
         }
      }

      admin_control_local_discover();
      gtk_label_set_text(GTK_LABEL(control_status), control_pending_lists ?
         "Discovering controls across connected nodes…" : "Local controls shown; no connected nodes.");
   } else if (!host || !strcmp(host, "local")) {
      admin_control_local_discover();
   } else {
      gtk_label_set_text(GTK_LABEL(control_status), "Discovering server controls…");

      if (!admin_control_request_profile(host, NULL, -1)) {
         gtk_label_set_text(GTK_LABEL(control_status), "Cannot query selected server");
      }
   }
}
static void admin_control_selection(GtkComboBox *combo, gpointer user) {
   (void)user;
   const char *name = gtk_combo_box_get_active_id(combo);

   if (!name) {
      return;
   }

   if (control_scale) {
      gtk_widget_set_sensitive(control_scale, FALSE);
   }

   if (!admin_control_request(name, -1) && control_status) {
      gtk_label_set_text(GTK_LABEL(control_status), "Unable to read selected control");
   }
}
static void admin_control_apply(GtkButton *button, gpointer user) {
   (void)button;
   (void)user;

   if (!control_rows_box) {
      return;
   }
   GList *rows = gtk_container_get_children(GTK_CONTAINER(control_rows_box));
   unsigned reverted = 0, failed = 0;

   for (GList *l = rows ; l ; l = l->next) {
      GtkWidget *row = GTK_WIDGET(l->data);

      if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "control-dirty"))) {
         continue;
      }
      int previous = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "control-previous"));

      if (!admin_control_write(row, previous)) {
         failed++;
         continue;
      }
      GtkWidget *scale = g_object_get_data(G_OBJECT(row), "control-scale");
      gtk_range_set_value(GTK_RANGE(scale), previous);
      g_object_set_data(G_OBJECT(row), "control-original", GINT_TO_POINTER(previous));
      g_object_set_data(G_OBJECT(row), "control-dirty", NULL);
      reverted++;
   }

   g_list_free(rows);
   admin_control_update_revert();
   char message[160];
   snprintf(message, sizeof(message), "%u controls reverted; %u submission failures", reverted, failed);
   gtk_label_set_text(GTK_LABEL(control_status), message);
}

/* Keep raw server fields in the model for account editing. Formatting and display order belong only to the GTK view. */
/* Wrapping must follow the actual allocated column width. A permanent 112px
 * wrap width made Audit Log details consume many lines in a wide window. */
/* UUIDs remain complete in the model. Only their on-screen rendering is
 * abbreviated; commands, sorting and clipboard operations use the raw ID. */
static char *admin_short_uuid(const char *uuid) {
   if (!uuid) return g_strdup("");
   size_t length=strlen(uuid);
   if (length<=18) return g_strdup(uuid);
   return g_strdup_printf("%.8s..%s",uuid,uuid+length-8);
}
static void admin_uuid_cell(GtkTreeViewColumn *column,GtkCellRenderer *renderer,
                            GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
   (void)column;
   int field=GPOINTER_TO_INT(data);
   char *raw=NULL;
   gtk_tree_model_get(model,iter,field,&raw,-1);
   char *shortened=admin_short_uuid(raw);
   g_object_set(renderer,"text",shortened,NULL);
   g_free(shortened);
   g_free(raw);
}
static void admin_copy_uuid_clicked(GtkMenuItem *item,gpointer data) {
   (void)data;
   const char *uuid=g_object_get_data(G_OBJECT(item),"full-uuid");
   if (!uuid) return;
   GtkClipboard *clipboard=gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
   gtk_clipboard_set_text(clipboard,uuid,-1);
}
static gboolean admin_uuid_menu(GtkWidget *widget,GdkEventButton *event,gpointer data) {
   admin_table_t *table=data;
   if (event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;
   GtkTreePath *path=NULL;
   GtkTreeViewColumn *column=NULL;
   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
         (int)event->x,(int)event->y,&path,&column,NULL,NULL)) return FALSE;
   int field=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column),"rr-uuid-field"));
   if (!g_object_get_data(G_OBJECT(column),"rr-is-uuid")) {
      gtk_tree_path_free(path);
      return FALSE;
   }
   GtkTreeIter iter;
   GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
   char *raw=NULL;
   if (gtk_tree_model_get_iter(model,&iter,path))
      gtk_tree_model_get(model,&iter,field,&raw,-1);
   gtk_tree_path_free(path);
   if (!raw || !*raw) {g_free(raw);return FALSE;}
   GtkWidget *menu=gtk_menu_new();
   GtkWidget *copy=gtk_menu_item_new_with_label("Copy UUID");
   g_object_set_data_full(G_OBJECT(copy),"full-uuid",raw,g_free);
   g_signal_connect(copy,"activate",G_CALLBACK(admin_copy_uuid_clicked),NULL);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu),copy);
   gtk_widget_show_all(menu);
   g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent *)event);
   return TRUE;
}
static void admin_column_wrap_width(GObject *object,GParamSpec *pspec,gpointer data) {
   (void)pspec;
   GtkTreeViewColumn *column=GTK_TREE_VIEW_COLUMN(object);
   GtkCellRenderer *renderer=GTK_CELL_RENDERER(data);
   int width=gtk_tree_view_column_get_width(column)-12;
   if (width<48) width=48;
   int previous=0;
   g_object_get(renderer,"wrap-width",&previous,NULL);
   if (previous!=width) g_object_set(renderer,"wrap-width",width,NULL);
}
static void admin_user_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   (void)column;
   int field = GPOINTER_TO_INT(data);
   char *raw = NULL;
   gtk_tree_model_get(model, iter, field, &raw, -1);

   if (field == 1 || field == 7) {
      g_object_set(renderer, "active", raw && (!strcmp(raw, "1") || !strcasecmp(raw, "true")), "activatable", FALSE, NULL);
   } else {
      time_t stamp = raw ? strtoll(raw, NULL, 10) : 0;
      time_t current = time(NULL);
      char *text;

      if (stamp <= 0) {
         text = g_strdup("Never");
      } else if (field == 6 && stamp <= current) {
         text = g_strdup("Expired");
      } else {
         /* Show whole calendar-independent 24-hour days, rounding up for expiry so a positive fraction of a day is not shown as zero. */
         long long seconds = field == 5 ? (current > stamp ? (long long)(current - stamp) : 0)
            : (long long)(stamp - current);
         long long days = field == 5 ? seconds / 86400 : (seconds + 86399) / 86400;
         text = g_strdup_printf(field == 5 ? "%lld days ago" : "%lld days remaining", days);
      }
      g_object_set(renderer, "text", text, NULL);
      g_free(text);
   }
   g_free(raw);
}

/* Session timestamps are sent as Unix seconds; display elapsed time, not a date. */
static void admin_session_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   (void)column;
   (void)data;
   char *raw = NULL;
   gtk_tree_model_get(model, iter, 5, &raw, -1);
   time_t connected = raw ? (time_t) strtoll(raw, NULL, 10) : 0;
   time_t current = time(NULL);
   char *duration = connected > 0 ? time_t2dhms(current > connected ? current - connected : 0) : NULL;
   g_object_set(renderer, "text", duration ? duration : "Unknown", NULL);
   free(duration);
   g_free(raw);
}

/* Formatting never changes the raw values used by edits or quota commands. */
static char *admin_commas(const char *raw) {
   if (!raw || !*raw) {
      return g_strdup("");
   }
   const char *digits = raw + (*raw == '-' || *raw == '+');
   const char *end = digits;
   while (g_ascii_isdigit(*end)) {
      end++;
   }

   if (end == digits || (*end && *end != '.')) {
      return g_strdup(raw);
   }
   const char *fraction = end;

   if (*fraction == '.') {
      bool zero = true;

      for (const char *p = fraction + 1 ; *p ; p++) {
         if (!g_ascii_isdigit(*p)) {
            return g_strdup(raw);
         }

         if (*p != '0') {
            zero = false;
         }
      }

      if (zero) {
         fraction = "";
      }
   }
   GString *text = g_string_new_len(raw, digits - raw);

   for (const char *p = digits ; p < end ; p++) {
      if (p != digits && (end - p) % 3 == 0) {
         g_string_append_c(text, ',');
      }
      g_string_append_c(text, *p);
   }

   g_string_append(text, fraction);

   return g_string_free(text, FALSE);
}

static char *admin_scaled(const char *raw, bool bytes) {
   char *end;
   errno = 0;
   long long value = raw && *raw ? strtoll(raw, &end, 10) : 0;

   if (!raw || !*raw || errno || *end) {
      return g_strdup(raw ? raw : "");
   }
   unsigned long long magnitude = value < 0 ? (unsigned long long)(-(value + 1)) + 1 : (unsigned long long)value;
   unsigned long long divisor = bytes ? (magnitude >= 1000000 ? 1000000 : magnitude >= 1000 ? 1000 : 1) : 60;
   const char *unit = !bytes ? "" : divisor == 1000000 ? " MB" : divisor == 1000 ? " KB" : " B";
   unsigned long long whole = magnitude / divisor;
   unsigned decimal = ((magnitude % divisor) * 100 + divisor / 2) / divisor;

   if (decimal == 100) {
      whole++;
      decimal = 0;
   }
   char integer[32];
   snprintf(integer, sizeof(integer), "%s%llu", value < 0 ? "-" : "", whole);
   char *grouped = admin_commas(integer);
   char *text = divisor == 1 ? g_strconcat(grouped, unit, NULL) : g_strdup_printf("%s.%02u%s", grouped, decimal, unit);
   g_free(grouped);

   return text;
}

static void admin_numeric_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   admin_table_t *table = g_object_get_data(G_OBJECT(column), "rr-admin-table");
   int field = GPOINTER_TO_INT(data);
   char *raw = NULL;
   gtk_tree_model_get(model, iter, field, &raw, -1);
   char *text;

   if (!strcmp(table->name, "quotas")) {
      text = admin_scaled(raw, field >= 3);
   } else if (field == 9) {
      text = g_strdup(raw && *raw ? raw : "Unknown");
   } else if (field == 3) {
      text = raw && raw[0] && !raw[1] ? g_strdup_printf("Unknown rig.vfo-%c", g_ascii_tolower(raw[0])) : g_strdup(raw && *raw ? raw : "Unknown");
   } else {
      text = admin_commas(raw);
   }
   g_object_set(renderer, "text", text, NULL);
   g_free(text);
   g_free(raw);
}

static bool admin_command(admin_table_t *table, const char *command, const char *tail, const char *target) {
   rrconn_t *connection = rrclient_connection_find(table->profile);

   if (!connection || !connection->is_ws || !connection->authenticated) {
      gtk_label_set_text(GTK_LABEL(table->status), "Select a connected RustyRig server and Refresh");

      return false;
   }
   dict *request = dict_new();

   if (!request) {
      return false;
   }
   dict_add(request, "msg.type", "talk");

   if (!strncmp(command, "group-", 6) || !strncmp(command, "privset-", 8) || !strncmp(command, "resource-", 9) || !strcmp(command, "rig-access") || !strcmp(command, "room-access") || !strcmp(command, "user-edit")
      || !strcmp(command, "room-remove") || !strcmp(command, "session-kick") || !strcmp(command, "session-kick-all")) {
      dict_add(request, "talk.action", command);
      command = "admin";
   }
   dict_add(request, "talk.cmd", command);
   dict_add(request, "talk.data", tail);

   if (target) {
      dict_add(request, "talk.target", target);
   }
   bool sent = ws_send_dict(NULL, connection, request, WEBSOCKET_OP_TEXT);

   if (!sent) {
      gtk_label_set_text(GTK_LABEL(table->status), "Unable to send administration request");
   }
   dict_free(request);

   return sent;
}

/* Reuse the server's password command; credentials never enter chat input/history. */
static void admin_set_password(admin_table_t *table, GtkWindow *parent, const char *name) {
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Set Password", parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL,
      "Set Password", GTK_RESPONSE_OK, NULL);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   char *context = g_strdup_printf("Server: %s\nUser: %s\nPassword: 8–128 bytes, no whitespace", table->profile, name);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new(context), FALSE, FALSE, 6);
   g_free(context);
   GtkWidget *password = gtk_entry_new(), *confirmation = gtk_entry_new();
   gtk_entry_set_visibility(GTK_ENTRY(password), FALSE);
   gtk_entry_set_visibility(GTK_ENTRY(confirmation), FALSE);
   gtk_entry_set_input_purpose(GTK_ENTRY(password), GTK_INPUT_PURPOSE_PASSWORD);
   gtk_entry_set_input_purpose(GTK_ENTRY(confirmation), GTK_INPUT_PURPOSE_PASSWORD);
   gtk_entry_set_placeholder_text(GTK_ENTRY(password), "New password");
   gtk_entry_set_placeholder_text(GTK_ENTRY(confirmation), "Confirm password");
   gtk_entry_set_activates_default(GTK_ENTRY(confirmation), TRUE);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
   gtk_box_pack_start(GTK_BOX(box), password, FALSE, FALSE, 6);
   gtk_box_pack_start(GTK_BOX(box), confirmation, FALSE, FALSE, 6);
   GtkWidget *error = gtk_label_new("");
   gtk_box_pack_start(GTK_BOX(box), error, FALSE, FALSE, 6);
   gtk_widget_show_all(dialog);

   while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
      const char *value = gtk_entry_get_text(GTK_ENTRY(password));

      if (strcmp(value, gtk_entry_get_text(GTK_ENTRY(confirmation)))) {
         gtk_label_set_text(GTK_LABEL(error), "Passwords do not match");
         continue;
      }

      if (strlen(value) < 8 || strlen(value) > 128 || strpbrk(value, " \t\r\n\v\f")) {
         gtk_label_set_text(GTK_LABEL(error), "Use 8–128 bytes without whitespace");
         continue;
      }
      char *tail = g_strdup_printf("pass %s %s", name, value);
      admin_command(table, "user", tail, NULL);
      memset(tail, 0, strlen(tail));
      g_free(tail);
      break;
   }
   gtk_widget_destroy(dialog);
}

static rr_event_token_t admin_password_notice_token;
static GtkWidget *admin_account_dialog;

/* Temporary credentials are returned only to the requester, never through audit. */
static void admin_password_notice(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)unused;
   dict *notice = data ? json2dict(data) : NULL;
   const char *text = notice ? dict_get(notice, "notice.msg", "") : "";

   if (g_str_has_prefix(text, "USER: temporary password for ") ||
      g_str_has_prefix(text, "USER: failed to add ") ||
      g_str_has_prefix(text, "USER: account already exists: ") ||
      g_str_has_prefix(text, "USER: no user slots are available") ||
      g_str_has_prefix(text, "USER: administrators cannot create ") ||
      (g_str_has_prefix(text, "USER: added ") && strstr(text, " with temporary password ")) ||
      g_str_has_prefix(text, "USER: password changed for ") ||
      g_str_has_prefix(text, "USER: failed to reset password for ") ||
      g_str_has_prefix(text, "USER: failed to change password for ") || g_str_has_prefix(text, "USER: insufficient privilege to modify ")) {
      const char *server = rrclient_connection_name(connection);

      if (g_str_has_prefix(text, "USER: added ") && tables[0].store && connection == rrclient_connection_find(tables[0].profile)) {
         gtk_label_set_text(GTK_LABEL(tables[0].status), "Account created");
         admin_command(&tables[0], "admin", "users", NULL);
      }
      GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(admin_account_dialog ? admin_account_dialog : main_window), GTK_DIALOG_DESTROY_WITH_PARENT,
         GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Account result — %s", server ? server : "server");
      gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", text);
      GList *labels = gtk_container_get_children(GTK_CONTAINER(gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog))));

      for (GList *item = labels ; item ; item = item->next) {
         if (GTK_IS_LABEL(item->data)) {
            gtk_label_set_selectable(GTK_LABEL(item->data), TRUE);
         }
      }

      g_list_free(labels);
      g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
      gtk_widget_show(dialog);
      gtk_window_present(GTK_WINDOW(dialog));
   }
   dict_free(notice);
}

static void admin_password_notice_destroy(GtkWidget *widget, gpointer unused) {
   (void)widget;
   (void)unused;

   if (admin_password_notice_token) {
      event_off_token(admin_password_notice_token);
   }
   admin_password_notice_token = NULL;
}

static void admin_refresh(GtkButton *button, gpointer data) {
   (void)button;
   admin_table_t *table = data;
   const char *profile = rrclient_selected_server();
   snprintf(table->profile, sizeof(table->profile), "%s", profile ? profile : "");
   table->next[0] = 0;
   gtk_widget_set_sensitive(table->older, FALSE);
   gtk_list_store_clear(table->store);
   gtk_label_set_text(GTK_LABEL(table->status), table->profile);
   admin_command(table, "admin", table->name, NULL);
   if (!strcmp(table->name,"assignments") && group_store) {
      gtk_tree_store_clear(group_store);
      admin_command(table,"admin","groups",NULL);
   }
}

static void admin_session_kick(GtkButton *button, gpointer data) {
   admin_table_t *table = data;
   bool all = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "kick-all"));
   char *id = NULL, *username = NULL, *kickable = NULL;

   if (!all) {
      GtkTreeModel *model = NULL;
      GtkTreeIter iter;

      if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree)), &model, &iter)) {
         return;
      }
      gtk_tree_model_get(model, &iter, 0, &id, 1, &username, 6, &kickable, -1);

      if (g_strcmp0(kickable, "yes")) {
         gtk_label_set_text(GTK_LABEL(table->status), "This session is protected or belongs to your current login.");
         g_free(id);
         g_free(username);
         g_free(kickable);

         return;
      }
   }
   GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
      GTK_BUTTONS_OK_CANCEL, all ? "Disconnect all permitted sessions on %s?" : "Disconnect %s on %s?", all ? table->profile : username ? username :
      "selected session", all ? "" : table->profile);
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "The server will exclude your session and protected accounts. "
      "Active radio transmissions must be released by normal disconnect handling.");

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
      admin_command(table, all ? "session-kick-all" : "session-kick", "", all ? NULL : id);
   }
   gtk_widget_destroy(dialog);
   g_free(id);
   g_free(username);
   g_free(kickable);
}

/* A grant subject is selected from the linked directory, not typed as a UUID. */
static GtkWidget *grant_subjects;
static char *grant_profile;
static void admin_add_privilege(GtkButton *button, gpointer user) {
   (void)button;
   admin_table_t *table = user;
   rrconn_t *connection = rrclient_connection_find(table->profile);

   if (!connection || !connection->authenticated) {
      gtk_label_set_text(GTK_LABEL(table->status), "Connect to the destination server first");

      return;
   }
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Privilege", GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel",
      GTK_RESPONSE_CANCEL, "Grant", GTK_RESPONSE_OK, NULL);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_box_pack_start(GTK_BOX(area), gtk_label_new("User (from connected sites)"), FALSE, FALSE, 6);
   grant_subjects = gtk_combo_box_text_new();
   gtk_box_pack_start(GTK_BOX(area), grant_subjects, FALSE, FALSE, 4);
   /* Editable combo offers discovered privilege groups as well as a
    * free-form individual privilege token, e.g. rig.use. */
   GtkWidget *permission=gtk_combo_box_text_new_with_entry();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(permission),"rig.use");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(permission),"rig.full");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(permission),"room.join");
   admin_table_t *sets=&tables[6];
   if (sets->store) {
      GtkTreeIter entry;
      gboolean valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(sets->store),&entry);
      while (valid) {
         char *name=NULL;
         gtk_tree_model_get(GTK_TREE_MODEL(sets->store),&entry,0,&name,-1);
         if (name && *name) {
            char *label=g_strdup_printf("@%s",name);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(permission),label);
            g_free(label);
         }
         g_free(name);
         valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(sets->store),&entry);
      }
   }
   gtk_combo_box_set_active(GTK_COMBO_BOX(permission),0);
   gtk_box_pack_start(GTK_BOX(area),gtk_label_new("Privilege or @group (type or select)"),FALSE,FALSE,4);
   gtk_box_pack_start(GTK_BOX(area),permission,FALSE,FALSE,4);
   GtkWidget *scope=gtk_combo_box_text_new();
   const char *scopes[]={"site","rig","room","vfo","media","serial","file","bridge"};
   for (unsigned i=0;i<G_N_ELEMENTS(scopes);i++)
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(scope),scopes[i],scopes[i]);
   gtk_combo_box_set_active_id(GTK_COMBO_BOX(scope),"rig");
   gtk_box_pack_start(GTK_BOX(area),gtk_label_new("Resource type"),FALSE,FALSE,2);
   gtk_box_pack_start(GTK_BOX(area),scope,FALSE,FALSE,2);
   GtkWidget *resource=gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(resource),"*");
   gtk_entry_set_placeholder_text(GTK_ENTRY(resource),"Resource UUID (* for all rigs)");
   gtk_box_pack_start(GTK_BOX(area),gtk_label_new("Resource UUID"),FALSE,FALSE,2);
   gtk_box_pack_start(GTK_BOX(area),resource,FALSE,FALSE,2);
   GtkWidget *deny=gtk_check_button_new_with_label("Deny instead of allow");
   gtk_box_pack_start(GTK_BOX(area),deny,FALSE,FALSE,2);
   GtkWidget *description=gtk_label_new(
      "Grants apply on the selected server. @name assigns an entire privilege group; "
      "a plain name grants one privilege. Remote access policy will be configured separately.");
   gtk_label_set_line_wrap(GTK_LABEL(description),TRUE);
   gtk_box_pack_start(GTK_BOX(area),description,FALSE,FALSE,6);
   g_free(grant_profile);
   grant_profile = g_strdup(table->profile);
   admin_command(table, "admin", "subjects", NULL);
   gtk_widget_show_all(dialog);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
      const char *subject = gtk_combo_box_get_active_id(GTK_COMBO_BOX(grant_subjects));

      if (subject && *subject) {
         GtkWidget *editable=gtk_bin_get_child(GTK_BIN(permission));
         const char *token=gtk_entry_get_text(GTK_ENTRY(editable));
         const char *kind=gtk_combo_box_get_active_id(GTK_COMBO_BOX(scope));
         const char *uuid=gtk_entry_get_text(GTK_ENTRY(resource));
         if (!token || !*token || !kind || !*uuid || strpbrk(token," \\t\\r\\n") ||
             strpbrk(uuid," \\t\\r\\n")) {
            gtk_label_set_text(GTK_LABEL(table->status),"Enter a privilege or @group and resource UUID");
         } else {
            gboolean is_group=token[0]=='@';
            char *args=is_group ? g_strdup_printf("%s %s %s",subject,kind,uuid) :
               g_strdup_printf("%s %s %s %s",kind,uuid,token,
                  gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(deny)) ? "deny" : "allow");
            gboolean ok=admin_command(table,is_group ? "privset-assign" : "resource-grant",
                                      args,is_group ? token+1 : subject);
            gtk_label_set_text(GTK_LABEL(table->status),ok ? "Grant submitted" : "Unable to submit grant");
            g_free(args);
         }
      } else {
         gtk_label_set_text(GTK_LABEL(table->status), "Select an account before granting access");
      }
   }
   grant_subjects = NULL;
   g_clear_pointer(&grant_profile, g_free);
   gtk_widget_destroy(dialog);
}
enum { GROUP_TEXT, GROUP_DETAIL, GROUP_NAME, GROUP_KIND, GROUP_COUNT };
static gboolean admin_group_selected(char **group,char **kind,char **name,char **detail) {
   if (!group_tree) return FALSE;
   GtkTreeIter iter;
   GtkTreeModel *model=NULL;
   if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(group_tree)),
                                        &model,&iter)) return FALSE;
   gtk_tree_model_get(model,&iter,GROUP_NAME,group,GROUP_KIND,kind,
                      GROUP_TEXT,name,GROUP_DETAIL,detail,-1);
   return TRUE;
}
static void admin_group_tree_row(const char *group,const char *kind,
                                 const char *name,const char *detail) {
   if (!group_store || !group || !*group) return;
   GtkTreeIter parent,child;
   GtkTreeModel *model=GTK_TREE_MODEL(group_store);
   gboolean found=FALSE,valid=gtk_tree_model_get_iter_first(model,&parent);
   while (valid) {
      char *key=NULL;
      gtk_tree_model_get(model,&parent,GROUP_NAME,&key,-1);
      found=!g_strcmp0(key,group);
      g_free(key);
      if (found) break;
      valid=gtk_tree_model_iter_next(model,&parent);
   }
   if (!found) {
      gtk_tree_store_append(group_store,&parent,NULL);
      gtk_tree_store_set(group_store,&parent,GROUP_TEXT,group,GROUP_DETAIL,"",
                         GROUP_NAME,group,GROUP_KIND,"group",-1);
   }
   if (!strcmp(kind,"group")) {
      gtk_tree_store_set(group_store,&parent,GROUP_DETAIL,detail,-1);
      return;
   }
   gtk_tree_store_append(group_store,&child,&parent);
   gtk_tree_store_set(group_store,&child,GROUP_TEXT,name,GROUP_DETAIL,detail,
                      GROUP_NAME,group,GROUP_KIND,kind,-1);
}
static void admin_manage_groups(GtkButton *button, gpointer user) {
   (void)button;
   admin_table_t *table = user;
   rrconn_t *connection = rrclient_connection_find(table->profile);

   if (!connection || !connection->authenticated) {
      gtk_label_set_text(GTK_LABEL(table->status), "Connect to the destination server first");

      return;
   }
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Manage User Groups", GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel",
      GTK_RESPONSE_CANCEL, "Save Change", GTK_RESPONSE_OK, NULL);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *group = gtk_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(group), "e.g. club-members");
   char *selected_group=NULL,*selected_kind=NULL,*selected_name=NULL,*selected_detail=NULL;
   if (admin_group_selected(&selected_group,&selected_kind,&selected_name,&selected_detail))
      gtk_entry_set_text(GTK_ENTRY(group),selected_group);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Group name (stable symbolic name)"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), group, FALSE, FALSE, 3);
   GtkWidget *action = gtk_combo_box_text_new();
   const char *commands[] = {
      "group-create", "group-delete", "group-add-user", "group-remove-user", "group-grant", "group-ungrant"
   };
   const char *labels[] = {
      "Create group", "Delete group", "Add user", "Remove user", "Grant resource privilege", "Remove resource privilege"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(commands) ; i++) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(action), commands[i], labels[i]);
   }

   gtk_combo_box_set_active(GTK_COMBO_BOX(action), 0);
   if (selected_kind && !strcmp(selected_kind,"member"))
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(action),"group-remove-user");
   else if (selected_kind && !strcmp(selected_kind,"privilege"))
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(action),"group-ungrant");
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Action"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), action, FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("User (for Add/Remove user)"), FALSE, FALSE, 3);
   grant_subjects = gtk_combo_box_text_new();
   gtk_box_pack_start(GTK_BOX(box), grant_subjects, FALSE, FALSE, 3);
   g_free(grant_profile);
   grant_profile = g_strdup(table->profile);
   admin_command(table, "admin", "subjects", NULL);
   GtkWidget *kind = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(kind), "rig", "Rig");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(kind), "room", "Room");
   gtk_combo_box_set_active(GTK_COMBO_BOX(kind), 0);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Resource type (for privilege actions)"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), kind, FALSE, FALSE, 3);
   GtkWidget *resource = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(resource), "*");
   if (selected_kind && !strcmp(selected_kind,"privilege") && selected_name) {
      const char *separator=strchr(selected_name,':');
      if (separator) {
         char *scope=g_strndup(selected_name,separator-selected_name);
         gtk_combo_box_set_active_id(GTK_COMBO_BOX(kind),scope);
         gtk_entry_set_text(GTK_ENTRY(resource),separator+1);
         g_free(scope);
      }
   }
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Resource name: * means all, or use rig0 / #room"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), resource, FALSE, FALSE, 3);
   GtkWidget *privilege = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(privilege), "rig.full", "Full rig access");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(privilege), "rig.use", "Use rig");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(privilege), "room.join", "Join room");
   gtk_combo_box_set_active(GTK_COMBO_BOX(privilege), 0);
   if (selected_kind && !strcmp(selected_kind,"privilege") && selected_detail) {
      const char *end=strchr(selected_detail,' ');
      char *token=end ? g_strndup(selected_detail,end-selected_detail) : g_strdup(selected_detail);
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(privilege),token);
      g_free(token);
   }
   gtk_box_pack_start(GTK_BOX(box), privilege, FALSE, FALSE, 3);
   GtkWidget *effect = gtk_check_button_new_with_label("Deny instead of allow");
   if (selected_detail && strstr(selected_detail,"(deny)"))
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(effect),TRUE);
   gtk_box_pack_start(GTK_BOX(box), effect, FALSE, FALSE, 3);
   gtk_widget_show_all(dialog);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
      const char *group_name = gtk_entry_get_text(GTK_ENTRY(group));
      const char *op = gtk_combo_box_get_active_id(GTK_COMBO_BOX(action));
      const char *subject = gtk_combo_box_get_active_id(GTK_COMBO_BOX(grant_subjects));
      char *args = NULL;

      if (!strcmp(op, "group-create")) {
         args = g_strdup("User-defined group");
      } else if (!strcmp(op, "group-delete")) {
         args = g_strdup("");
      } else if (!strcmp(op, "group-add-user") || !strcmp(op, "group-remove-user")) {
         args = subject ? g_strdup(subject) : NULL;
      } else {
         const char *scope = gtk_combo_box_get_active_id(GTK_COMBO_BOX(kind));
         const char *token = gtk_combo_box_get_active_id(GTK_COMBO_BOX(privilege));
         const char *name = gtk_entry_get_text(GTK_ENTRY(resource));

         if (name && *name && !strpbrk(name, " \t\r\n")) {
            args = g_strdup_printf("%s %s %s%s", scope, name, token, !strcmp(op, "group-grant") ? (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(effect)) ?
               " deny" : " allow") : "");
         }
      }

      if (group_name && *group_name && op && args) {
         if (admin_command(table, op, args, group_name)) {
            gtk_label_set_text(GTK_LABEL(table->status), "Group change submitted; refreshing group list");
         }
      } else {
         gtk_label_set_text(GTK_LABEL(table->status), "Group name, user, or resource is missing");
      }
      g_free(args);
   }
   grant_subjects = NULL;
   g_clear_pointer(&grant_profile, g_free);
   gtk_widget_destroy(dialog);
   g_free(selected_group);
   g_free(selected_kind);
   g_free(selected_name);
   g_free(selected_detail);
}
/* Privilege groups are simply reusable collections of privilege tokens. */
static void admin_privgroup_edit(GtkButton *button,gpointer user) {
   admin_table_t *table=user;
   const char *operation=g_object_get_data(G_OBJECT(button),"privgroup-operation");
   if (!operation) return;
   GtkWidget *dialog=gtk_dialog_new_with_buttons("Privilege Group",
      GTK_WINDOW(main_window),GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_OK,NULL);
   GtkWidget *area=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *group=gtk_entry_new();
   GtkWidget *privilege=gtk_entry_new();
   GtkWidget *description=gtk_entry_new();
   GtkWidget *deny=gtk_check_button_new_with_label("Deny instead of allow");
   GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree));
   GtkTreeModel *model=NULL;GtkTreeIter iter;
   if (gtk_tree_selection_get_selected(selection,&model,&iter)) {
      char *name=NULL,*token=NULL;
      gtk_tree_model_get(model,&iter,0,&name,-1);
      if (name) gtk_entry_set_text(GTK_ENTRY(group),name);
      if (!strcmp(table->name,"setentries")) {
         gtk_tree_model_get(model,&iter,1,&token,-1);
         if (token) gtk_entry_set_text(GTK_ENTRY(privilege),token);
      }
      g_free(name);g_free(token);
   }
   gtk_entry_set_placeholder_text(GTK_ENTRY(group),"e.g. operators");
   gtk_entry_set_placeholder_text(GTK_ENTRY(privilege),"e.g. rig.use");
   gtk_box_pack_start(GTK_BOX(area),gtk_label_new("Group name"),FALSE,FALSE,2);
   gtk_box_pack_start(GTK_BOX(area),group,FALSE,FALSE,2);
   if (!strcmp(operation,"privset-create")) {
      gtk_box_pack_start(GTK_BOX(area),gtk_label_new("Description"),FALSE,FALSE,2);
      gtk_box_pack_start(GTK_BOX(area),description,FALSE,FALSE,2);
   } else if (!strcmp(operation,"privset-entry") || !strcmp(operation,"privset-unentry")) {
      gtk_box_pack_start(GTK_BOX(area),gtk_label_new("Privilege"),FALSE,FALSE,2);
      gtk_box_pack_start(GTK_BOX(area),privilege,FALSE,FALSE,2);
      if (!strcmp(operation,"privset-entry"))
         gtk_box_pack_start(GTK_BOX(area),deny,FALSE,FALSE,2);
   }
   gtk_widget_show_all(dialog);
   if (gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_OK) {
      const char *name=gtk_entry_get_text(GTK_ENTRY(group));
      const char *token=gtk_entry_get_text(GTK_ENTRY(privilege));
      const char *desc=gtk_entry_get_text(GTK_ENTRY(description));
      char *tail=NULL;
      if (!strcmp(operation,"privset-create")) tail=g_strdup(desc);
      else if (!strcmp(operation,"privset-delete")) tail=g_strdup("");
      else if (*token && !strpbrk(token," \\t\\r\\n"))
         tail=!strcmp(operation,"privset-entry") ?
            g_strdup_printf("%s %s",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(deny)) ?
               "deny" : "allow",token) : g_strdup(token);
      if (*name && !strpbrk(name," \\t\\r\\n") && tail) {
         if (admin_command(table,operation,tail,name))
            gtk_label_set_text(GTK_LABEL(table->status),"Privilege group change submitted; refresh to view");
      } else gtk_label_set_text(GTK_LABEL(table->status),"Valid group and privilege names are required");
      g_free(tail);
   }
   gtk_widget_destroy(dialog);
}
static void admin_older(GtkButton *button, gpointer data) {
   (void)button;
   admin_table_t *table = data;
   char tail[64];
   snprintf(tail, sizeof(tail), "%s %s", table->name, table->next);
   gtk_widget_set_sensitive(table->older, FALSE);
   admin_command(table, "admin", tail, NULL);
}

/* PARITY: rustyrig-www/js/webui.admin.js adminCreateUser. The server owns validation, privilege checks, auditing and temporary password generation. */
static void admin_create_user(GtkButton *button, gpointer data) {
   (void)button;
   admin_table_t *table = data;

   if (!table->profile[0]) {
      admin_refresh(NULL, table);
   }
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Create User", GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel",
      GTK_RESPONSE_CANCEL, "Edit Privs", 1003, "Create User", GTK_RESPONSE_OK, NULL);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   char *context = g_strdup_printf("Server: %s\nA temporary password will be shown after creation.", table->profile);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new(context), FALSE, FALSE, 6);
   g_free(context);
   GtkWidget *name = gtk_entry_new(), *privileges = gtk_entry_new(), *error = gtk_label_new("");
   gtk_entry_set_placeholder_text(GTK_ENTRY(name), "Username (letters, numbers, _ or -)");
   gtk_entry_set_max_length(GTK_ENTRY(name), HTTP_USER_LEN);
   gtk_entry_set_text(GTK_ENTRY(privileges), "view,chat");
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Username"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Privileges"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), privileges, FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), error, FALSE, FALSE, 3);
   gtk_widget_show_all(dialog);
   int response;
   while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) == GTK_RESPONSE_OK || response == 1003) {
      if (response == 1003) {
         char *edited = gtk_edit_privileges(GTK_WINDOW(dialog), table->profile, gtk_entry_get_text(GTK_ENTRY(name)), gtk_entry_get_text(GTK_ENTRY(privileges)));

         if (edited) {
            gtk_entry_set_text(GTK_ENTRY(privileges), edited);
         }
         g_free(edited);
         continue;
      }
      const char *username = gtk_entry_get_text(GTK_ENTRY(name));
      const char *permissions = gtk_entry_get_text(GTK_ENTRY(privileges));
      bool valid = *username != 0;

      for (const unsigned char *p = (const unsigned char *)username ; *p ; p++) {
         if (!g_ascii_isalnum(*p) && *p != '_' && *p != '-') {
            valid = false;
         }
      }

      if (!valid || !*permissions || strpbrk(permissions, " \t\r\n\v\f")) {
         gtk_label_set_text(GTK_LABEL(error), "Enter a valid username and a comma-separated privilege list without spaces.");
         continue;
      }
      char *request = g_strdup_printf("add %s %s", username, permissions);
      bool sent = admin_command(table, "user", request, NULL);
      g_free(request);

      if (!sent) {
         continue;
      }
      gtk_label_set_text(GTK_LABEL(table->status), "Creating account; awaiting server confirmation");
      break;
   }
   gtk_widget_destroy(dialog);
}
static void admin_action(GtkButton *button, gpointer data) {
   admin_table_t *table = data;
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree));
   GtkTreeModel *model;
   GtkTreeIter iter;

   if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
      return;
   }
   gchar *name = NULL, *value = NULL;
   gtk_tree_model_get(model, &iter, 0, &name, 2, &value, -1);
   const char *action = g_object_get_data(G_OBJECT(button), "action");
   char tail[512];

   if (!strcmp(table->name, "users") && !strcmp(action, "privileges")) {
      char *privileges = gtk_edit_privileges(GTK_WINDOW(main_window), table->profile, name, value);

      if (privileges && g_strcmp0(privileges, value)) {
         dict *fields = dict_new();

         if (fields) {
            dict_add(fields, "permissions", privileges);
            char *json = dict2json(fields);

            if (json && admin_command(table, "user-edit", json, name)) {
               gtk_label_set_text(GTK_LABEL(table->status), "Saving privileges; awaiting server confirmation");
            }
            free(json);
            dict_free(fields);
         }
      }
      g_free(privileges);
      g_free(name);
      g_free(value);

      return;
   }

   if (!strcmp(table->name, "users") && !strcmp(action, "edit")) {
      GtkWidget *dialog = gtk_dialog_new_with_buttons("Edit database account", GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
         "Cancel", GTK_RESPONSE_CANCEL, "Set Password", 1001, "Reset PW", 1002, "Edit Privs", 1003, "Save", GTK_RESPONSE_OK, NULL);
      admin_account_dialog = dialog;
      GtkWidget *grid = gtk_grid_new(), *entries[10];
      GtkWidget *full_rig_check = NULL;
      char *original[10] = {
         0
      };
      gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
      gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
      gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);

      for (int col = 0 ; col < 10 && table->fields[col] ; col++) {
         gchar *text = NULL;
         gtk_tree_model_get(model, &iter, col, &text, -1);

         if (col == 9) {
            full_rig_check = gtk_check_button_new_with_label("Full Rig Access — all rigs on this server");
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(full_rig_check), text && !strcmp(text, "1"));
            gtk_grid_attach(GTK_GRID(grid), full_rig_check, 0, col, 2, 1);
            entries[col] = NULL;
         } else {
            entries[col] = gtk_entry_new();
            gtk_entry_set_text(GTK_ENTRY(entries[col]), text ? text : "");
            gtk_editable_set_editable(GTK_EDITABLE(entries[col]), col != 0 && col != 5 && col != 8);
            gtk_grid_attach(GTK_GRID(grid), gtk_label_new(table->fields[col] ? table->fields[col] : ""), 0, col, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), entries[col], 1, col, 1, 1);
         }
         original[col] = text;
      }

      GtkWidget *hint = gtk_label_new("enabled / password_change_required: 0 or 1; expiry: Unix seconds, blank = never");
      gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), hint);
      gtk_widget_show_all(dialog);

      int response;
      while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) == 1001 || response == 1002 || response == 1003) {
         if (response == 1003) {
            char *privileges = gtk_edit_privileges(GTK_WINDOW(dialog), table->profile, name, gtk_entry_get_text(GTK_ENTRY(entries[2])));

            if (privileges) {
               gtk_entry_set_text(GTK_ENTRY(entries[2]), privileges);
            }
            g_free(privileges);
         } else if (response == 1001) {
            admin_set_password(table, GTK_WINDOW(dialog), name);
         } else {
            char *request = g_strdup_printf("resetpw %s", name);
            admin_command(table, "user", request, NULL);
            g_free(request);
         }
      }

      if (response == GTK_RESPONSE_OK) {
         dict *fields = dict_new();

         if (fields) {
            bool changed = false;

            for (int col = 1 ; col < 10 && table->fields[col] ; col++) {
               if (col == 5 || col == 8) {
                  continue;
               }

               if (col == 9) {
                  const char *value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(full_rig_check)) ? "1" : "0";

                  if (g_strcmp0(value, original[col] ? original[col] : "0")) {
                     dict_add(fields, "full_rig_access", value);
                     changed = true;
                  }
                  continue;
               }
               const char *edited = gtk_entry_get_text(GTK_ENTRY(entries[col]));

               if (g_strcmp0(edited, original[col] ? original[col] : "")) {
                  dict_add(fields, table->fields[col], edited);
                  changed = true;
               }
            }

            char *json = dict2json(fields);

            if (!changed) {
               gtk_label_set_text(GTK_LABEL(table->status), "No account fields changed");
            } else if (json) {
               if (admin_command(table, "user-edit", json, name)) {
                  gtk_label_set_text(GTK_LABEL(table->status), "Saving account; awaiting server confirmation");
               }
            } else {
               gtk_label_set_text(GTK_LABEL(table->status), "Unable to serialize account changes");
            }
            free(json);
            dict_free(fields);
         } else {
            gtk_label_set_text(GTK_LABEL(table->status), "Unable to allocate account changes");
         }
      }
      admin_account_dialog = NULL;
      gtk_widget_destroy(dialog);

      for (int col = 0 ; col < 10 ; col++) {
         g_free(original[col]);
      }

      g_free(name);
      g_free(value);

      return;
   } else if (!strcmp(table->name, "rooms") && !strcmp(action, "remove")) {
      admin_command(table, "room-remove", "", name);
   } else if (!strcmp(action, "edit") || !strcmp(table->name, "quotas")) {
      bool quota = !strcmp(table->name, "quotas");
      GtkWidget *dialog = gtk_dialog_new_with_buttons(quota ? action : "Edit room topic", GTK_WINDOW(main_window), GTK_DIALOG_MODAL |
         GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_OK, NULL);
      GtkWidget *entry = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(entry), quota ? "" : value ? value : "");
      const char *hint = !quota ? "Topic" : strstr(action, "SET") ? "TX: e.g. 30m; BW: e.g. 10M. SET 0 exhausts allowance." : "TX: e.g. 30m; BW: e.g. 10M.";
      gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), gtk_label_new(hint), FALSE, FALSE, 8);
      gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, FALSE, FALSE, 8);
      gtk_widget_show_all(dialog);

      if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
         if (quota) {
            snprintf(tail, sizeof(tail), "%s %s %s", action, name, gtk_entry_get_text(GTK_ENTRY(entry)));
            admin_command(table, "quota", tail, NULL);
         } else {
            admin_command(table, "topic", gtk_entry_get_text(GTK_ENTRY(entry)), name);
         }
      }
      gtk_widget_destroy(dialog);
   } else {
      bool confirmed = true;

      if (!strcmp(action, "remove")) {
         GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK_CANCEL, "Remove account %s?", name);
         confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
         gtk_widget_destroy(dialog);
      }

      if (confirmed) {
         snprintf(tail, sizeof(tail), "%s %s", action, name);
         admin_command(table, !strcmp(table->name, "users") ? "user" : "room", tail, NULL);
      }
   }
   g_free(name);
   g_free(value);
   admin_command(table, "admin", table->name, NULL);
}

void gtk_admin_result(const char *data, rrconn_t *connection) {
   dict *reply = data ? json2dict(data) : NULL;

   if (!reply) {
      return;
   }
   const char *name = dict_get(reply, "talk.target", "");
   const char *action = dict_get(reply, "talk.action", "");
   dict *row = json2dict(dict_get(reply, "talk.data", "{}"));

   if (row && !strcmp(name, "subjects") && grant_subjects && grant_profile && !g_strcmp0(grant_profile, rrclient_connection_name(connection))) {
      if (!strcmp(action, "begin")) {
         gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(grant_subjects));
      } else if (!strcmp(action, "row")) {
         const char *username = dict_get(row, "user", "");
         const char *host = dict_get(row, "host", "");
         const char *label = dict_get(row, "label", username);

         if (*username) {
            char *id = *host ? g_strdup_printf("%s@%s", username, host) : g_strdup(username);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(grant_subjects), id, label);
            g_free(id);
         }
      }
      dict_free(row);
      dict_free(reply);

      return;
   }
   if (row && !strcmp(name,"groups") && group_store &&
       !g_strcmp0(rrclient_connection_name(connection),tables[8].profile)) {
      if (!strcmp(action,"begin")) gtk_tree_store_clear(group_store);
      else if (!strcmp(action,"row"))
         admin_group_tree_row(dict_get(row,"group_name",""),
            dict_get(row,"entry_type",""),dict_get(row,"entry_name",""),
            dict_get(row,"entry_detail",""));
      else if (!strcmp(action,"end") && group_tree)
         gtk_tree_view_expand_all(GTK_TREE_VIEW(group_tree));
      else if (!strcmp(action,"updated"))
         admin_command(&tables[8],"admin","groups",NULL);
      dict_free(row);dict_free(reply);return;
   }
   const char *source_profile = rrclient_connection_name(connection);
   const char *view_host = control_hosts ? gtk_combo_box_get_active_id(GTK_COMBO_BOX(control_hosts)) : NULL;
   gboolean all_nodes = view_host && !strcmp(view_host, "all");

   if (row && !strcmp(name, "controls") && control_combo && source_profile && (all_nodes || (view_host && !strcmp(view_host, source_profile)))) {
      if (!strcmp(action, "begin")) {
         if (!all_nodes) {
            gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(control_combo));
            admin_control_rows_clear();
         }
         gtk_widget_set_sensitive(control_scale, FALSE);
         gtk_widget_set_sensitive(control_apply, TRUE);
         gtk_label_set_text(GTK_LABEL(control_status), "Discovering available controls…");
      } else if (!strcmp(action, "row")) {
         const char *id = dict_get(row, "name", "");
         const char *label = dict_get(row, "label", id);

         if (*id) {
            if (!all_nodes) {
               gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(control_combo), id, label);
            }
            admin_control_row_add(row, source_profile);
         }
      } else if (!strcmp(action, "end")) {
         if (all_nodes) {
            if (control_pending_lists) {
               --control_pending_lists;
            }

            if (!control_pending_lists) {
               gtk_label_set_text(GTK_LABEL(control_status), "Controls loaded from connected nodes");
            }
         } else if (control_pending_name) {
            char *requested = g_strdup(control_pending_name);
            admin_control_choose(requested);
            g_free(requested);
         } else {
            gtk_combo_box_set_active(GTK_COMBO_BOX(control_combo), 0);
         }

         if (!all_nodes && !gtk_combo_box_get_active_id(GTK_COMBO_BOX(control_combo))) {
            gtk_label_set_text(GTK_LABEL(control_status), "No hardware controls available");
         }
      } else if (!strcmp(action, "state")) {
         admin_control_row_state(row, source_profile);
         const char *active = gtk_combo_box_get_active_id(GTK_COMBO_BOX(control_combo));

         if (active && !strcmp(active, dict_get(row, "name", ""))) {
            int minimum = atoi(dict_get(row, "minimum", "0"));
            int maximum = atoi(dict_get(row, "maximum", "0"));
            int current = atoi(dict_get(row, "current", "0"));
            int step = atoi(dict_get(row, "step", "1"));

            if (maximum >= minimum && current >= minimum && current <= maximum && step > 0) {
               gtk_range_set_range(GTK_RANGE(control_scale), minimum, maximum);
               gtk_range_set_increments(GTK_RANGE(control_scale), step, step);
               gtk_range_set_value(GTK_RANGE(control_scale), current);
               bool writable = dict_get_bool(row, "writable", false);
               gtk_widget_set_sensitive(control_scale, writable);
               admin_control_update_revert();
               char message[256];
               snprintf(message, sizeof(message), "%s: %d / %d %s", dict_get(row, "label", active), current, maximum, dict_get(row, "unit", ""));
               gtk_label_set_text(GTK_LABEL(control_status), message);
            }
         }
      }
      dict_free(row);
      dict_free(reply);

      return;
   }

   for (size_t i = 0 ; row && i < sizeof(tables) / sizeof(tables[0]) ; i++) {
      admin_table_t *table = &tables[i];

      if (strcmp(name, table->name) || !table->store || connection != rrclient_connection_find(table->profile)) {
         continue;
      }

      if (!strcmp(action, "confirm") && !strcmp(name, "rooms")) {
         GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK_CANCEL, dict_get_bool(row, "deleted", false)
            ? "Permanently delete room %s? All chat history will be lost. This cannot be undone."
            : "Remove room %s? It can be restored later.", dict_get(row, "room", ""));

         if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
            admin_command(table, "room-remove", dict_get(row, "confirmation", ""), dict_get(row, "room", ""));
         }
         gtk_widget_destroy(dialog);
      } else if (!strcmp(action, "error")) {
         const char *message = dict_get(row, "message", "Account operation failed");
         gtk_label_set_text(GTK_LABEL(table->status), message);
         GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE, "%s", message);
         gtk_dialog_run(GTK_DIALOG(dialog));
         gtk_widget_destroy(dialog);
      } else if (!strcmp(action, "changed")) {
         gtk_label_set_text(GTK_LABEL(table->status), "Account saved");
         admin_command(table, "admin", table->name, NULL);
      } else if (!strcmp(action, "begin")) {
         if (!dict_get_bool(row, "append", false)) {
            gtk_list_store_clear(table->store);
         }
      } else if (!strcmp(action, "row")) {
         GtkTreeIter iter;
         gtk_list_store_append(table->store, &iter);

         for (int col = 0 ; col < 10 && table->fields[col] ; col++) {
            gtk_list_store_set(table->store, &iter, col, dict_get(row, table->fields[col], ""), -1);
         }
      } else if (!strcmp(action, "end")) {
         snprintf(table->next, sizeof(table->next), "%s", dict_get(row, "next", "0"));
         gtk_widget_set_sensitive(table->older, strcmp(table->next, "0") != 0);
      }
   }

   dict_free(row);
   dict_free(reply);
}

static void admin_table_destroy(GtkWidget *widget, gpointer data) {
   (void)widget;
   admin_table_t *table = data;
   g_clear_object(&table->store);
   table->tree = table->older = table->status = NULL;
   table->profile[0] = 0;
}

void gtk_admin_refresh(void) {
   for (size_t i = 0 ; i < sizeof(tables) / sizeof(tables[0]) ; i++) {
      admin_refresh(NULL, &tables[i]);
   }

   if (control_combo) {
      admin_control_refresh(NULL, NULL);
   }
}

static void admin_page_selected(GtkNotebook *notebook, GtkWidget *page, guint number, gpointer data) {
   (void)notebook;
   (void)number;
   (void)data;

   if (page != admin_view) {
      return;
   }

   gtk_admin_refresh();
}

GtkWidget *init_admin_tab(void) {
   admin_view = gtk_notebook_new();
   admin_password_notice_token = event_on_token("notice.msg", admin_password_notice, NULL);
   g_signal_connect(admin_view, "destroy", G_CALLBACK(admin_password_notice_destroy), NULL);
   g_signal_connect_object(main_notebook, "switch-page", G_CALLBACK(admin_page_selected), admin_view, 0);

   GtkWidget *privileges_page = NULL;
   GtkWidget *grants_panel = NULL, *groups_panel = NULL;

   for (size_t i = 0 ; i < sizeof(tables) / sizeof(tables[0]) ; i++) {
      admin_table_t *table = &tables[i];
      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
      GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
      GtkWidget *refresh = gtk_button_new_with_label("Refresh");
      table->older = gtk_button_new_with_label("Load older / more");
      table->status = gtk_label_new("Select a server and Refresh");
      gtk_box_pack_start(GTK_BOX(buttons), refresh, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(buttons), table->older, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(buttons), table->status, FALSE, FALSE, 0);
      gtk_widget_set_sensitive(table->older, FALSE);
      g_signal_connect(refresh, "clicked", G_CALLBACK(admin_refresh), table);
      g_signal_connect(table->older, "clicked", G_CALLBACK(admin_older), table);

      if (!strcmp(table->name, "sessions")) {
         GtkWidget *kick = gtk_button_new_with_label("Disconnect selected…");
         GtkWidget *kick_all = gtk_button_new_with_label("Disconnect all…");
         g_object_set_data(G_OBJECT(kick_all), "kick-all", GINT_TO_POINTER(1));
         g_signal_connect(kick, "clicked", G_CALLBACK(admin_session_kick), table);
         g_signal_connect(kick_all, "clicked", G_CALLBACK(admin_session_kick), table);
         gtk_box_pack_start(GTK_BOX(buttons), kick, FALSE, FALSE, 0);
         gtk_box_pack_start(GTK_BOX(buttons), kick_all, FALSE, FALSE, 0);
      }

      if (!strcmp(table->name, "assignments")) {
         GtkWidget *add = gtk_button_new_with_label("Add Privilege…");
         g_signal_connect(add, "clicked", G_CALLBACK(admin_add_privilege), table);
         gtk_box_pack_start(GTK_BOX(buttons), add, FALSE, FALSE, 0);

      }

      if (!strcmp(table->name,"privsets") || !strcmp(table->name,"setentries")) {
         const char *ops[]={ "privset-create","privset-delete","privset-entry","privset-unentry" };
         const char *labels[]={ "New Group…","Delete Group…","Add Privilege…","Remove Privilege…" };
         int offset=!strcmp(table->name,"privsets") ? 0 : 2;
         for (int n=offset;n<offset+2;n++) {
            GtkWidget *control=gtk_button_new_with_label(labels[n]);
            g_object_set_data(G_OBJECT(control),"privgroup-operation",(gpointer)ops[n]);
            g_signal_connect(control,"clicked",G_CALLBACK(admin_privgroup_edit),table);
            gtk_box_pack_start(GTK_BOX(buttons),control,FALSE,FALSE,0);
         }
      }

      if (i == 0) {
         GtkWidget *create = gtk_button_new_with_label("Create User");
         g_signal_connect(create, "clicked", G_CALLBACK(admin_create_user), table);
         gtk_box_pack_start(GTK_BOX(buttons), create, FALSE, FALSE, 0);
      }

      if (i < 3) {
         const char *labels[] = {
            "Edit", i == 0 ? "Lock" : "Remove", i == 0 ? "Unlock" : "Restore", "Remove", "Reset PW", "Edit Privs"
         };
         const char *actions[] = {
            "edit", i == 0 ? "lock" : "remove", i == 0 ? "unlock" : "add", "remove", "resetpw", "privileges"
         };

         if (i == 2) {
            labels[0] = actions[0] = "TX SET";
            labels[1] = actions[1] = "TX ADD";
            labels[2] = actions[2] = "BW SET";
            labels[3] = actions[3] = "BW ADD";
         }

         for (int n = 0 ; n < (i == 0 ? 6 : i == 2 ? 4 : 3) ; n++) {
            GtkWidget *button = gtk_button_new_with_label(labels[n]);
            g_object_set_data(G_OBJECT(button), "action", (gpointer) actions[n]);
            g_signal_connect(button, "clicked", G_CALLBACK(admin_action), table);
            gtk_box_pack_start(GTK_BOX(buttons), button, FALSE, FALSE, 0);
         }
      }
      gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
      table->store = gtk_list_store_new(10, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
      table->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(table->store));
      g_signal_connect(table->tree,"button-press-event",G_CALLBACK(admin_uuid_menu),table);

      const int user_order[] = {
         8, 1, 0, 3, 4, 5, 6, 2, 7
      };
      const char *user_titles[] = {
         "UID", "Enabled", "Name", "Email", "MaxClones", "Pass Set", "Pass Exp", "Privileges", "Chg Req", "Full Rig Access"
      };
      const int ptt_order[] = {
         0, 9, 1, 2, 3, 4, 5, 6, 7, 8
      };
      const char *ptt_titles[] = {
         "ID", "Server", "Start", "Name", "VFO", "Frequency", "Mode", "TxLen", "Stop Reason", "Recording"
      };
      const char *quota_titles[] = {
         "name", "TX Min", "TX Used", "BWRemain", "BWTxUsed", "BWRxUsed"
      };
      bool users = !strcmp(table->name, "users");
      bool quotas = !strcmp(table->name, "quotas"), ptt = !strcmp(table->name, "ptt");

      for (int col = 0 ; col < 10 && table->fields[col] ; col++) {
         int field = users ? user_order[col] : ptt ? ptt_order[col] : col;
         bool flag = users && (field == 1 || field == 7);
         GtkCellRenderer *renderer = flag ? gtk_cell_renderer_toggle_new() : gtk_cell_renderer_text_new();
         g_object_set(renderer, "ypad", 1, NULL);
         GtkTreeViewColumn *column = gtk_tree_view_column_new();
         gtk_tree_view_column_set_title(column, users ? user_titles[col] : quotas ? quota_titles[col] : ptt ? ptt_titles[col] : table->fields[col]);
         gtk_tree_view_column_pack_start(column, renderer, TRUE);

         if (users && (flag || field == 5 || field == 6)) {
            gtk_tree_view_column_set_cell_data_func(column, renderer, admin_user_cell, GINT_TO_POINTER(field), NULL);
         } else if (!strcmp(table->name, "sessions") && field == 5) {
            gtk_tree_view_column_set_cell_data_func(column, renderer, admin_session_cell, NULL, NULL);
         } else if ((quotas && field > 0) || (ptt && (field == 3 || field == 4 || field == 9))) {
            g_object_set_data(G_OBJECT(column), "rr-admin-table", table);
            gtk_tree_view_column_set_cell_data_func(column, renderer, admin_numeric_cell, GINT_TO_POINTER(field), NULL);
         } else if (!strcmp(table->fields[field],"uuid") ||
                    !strcmp(table->fields[field],"resource_uuid")) {
            gtk_tree_view_column_set_cell_data_func(column,renderer,admin_uuid_cell,
               GINT_TO_POINTER(field),NULL);
            g_object_set_data(G_OBJECT(column),"rr-is-uuid",GINT_TO_POINTER(1));
            g_object_set_data(G_OBJECT(column),"rr-uuid-field",GINT_TO_POINTER(field));
         } else {
            gtk_tree_view_column_add_attribute(column, renderer, "text", field);
         }
         /* Fixed column requisitions prevent long database values from
          * widening the notebook. Text wraps and GTK sizes rows naturally. */
         if (GTK_IS_CELL_RENDERER_TEXT(renderer)) {
            g_object_set(renderer,
               "wrap-mode", PANGO_WRAP_WORD_CHAR,
               "wrap-width", 112,
               "ellipsize", PANGO_ELLIPSIZE_NONE,
               "single-paragraph-mode", FALSE, NULL);
         }
         gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
         gtk_tree_view_column_set_fixed_width(column, (users && field == 2) ? 165 :
            (users && field == 3) ? 180 : quotas ? 110 :
            !strcmp(table->name, "rooms") ? 145 :
            (!strcmp(table->name,"audit") && field==4) ? 420 : 125);
         gtk_tree_view_column_set_min_width(column, 48);
         gtk_tree_view_column_set_resizable(column, TRUE);
         gtk_tree_view_column_set_expand(column, (users && field == 2) ||
            ((!strcmp(table->name, "rooms") || quotas) && field == 0) ||
            (!strcmp(table->name,"audit") && field == 4));
         if (GTK_IS_CELL_RENDERER_TEXT(renderer)) {
            g_signal_connect(column,"notify::width",G_CALLBACK(admin_column_wrap_width),renderer);
            admin_column_wrap_width(G_OBJECT(column),NULL,renderer);
         }

         if (users && field == 7) {
            PangoLayout *layout = gtk_widget_create_pango_layout(table->tree, "Chg Req");
            int width;
            pango_layout_get_pixel_size(layout, &width, NULL);
            g_object_unref(layout);
            gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
            gtk_tree_view_column_set_fixed_width(column, width + 20);
            gtk_tree_view_column_set_resizable(column, FALSE);
         }

         if (quotas && (field == 1 || field == 2)) {
            GtkWidget *title = gtk_label_new(quota_titles[col]);
            gtk_widget_set_tooltip_text(title, field == 1 ? "Remaining TX time in minutes" : "Used TX time in minutes");
            gtk_widget_show(title);
            gtk_tree_view_column_set_widget(column, title);
         }
         gtk_tree_view_column_set_sort_column_id(column, field);
         gtk_tree_view_append_column(GTK_TREE_VIEW(table->tree), column);
      }

      GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
      gtk_container_add(GTK_CONTAINER(scroll), table->tree);
      gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
      g_signal_connect(box, "destroy", G_CALLBACK(admin_table_destroy), table);

      gboolean grant_table=!strcmp(table->name,"assignments") || !strcmp(table->name,"grants");
      gboolean group_table=!strcmp(table->name,"privsets") || !strcmp(table->name,"setentries");
      if (grant_table || group_table) {
         if (!privileges_page) {
            privileges_page=gtk_paned_new(GTK_ORIENTATION_VERTICAL);
            grants_panel=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
            groups_panel=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
            GtkWidget *grants_title=gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(grants_title),"<b>Grants — individual privileges or @groups assigned to users</b>");
            gtk_label_set_xalign(GTK_LABEL(grants_title),0);
            gtk_box_pack_start(GTK_BOX(grants_panel),grants_title,FALSE,FALSE,0);
            GtkWidget *groups_title=gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(groups_title),"<b>Privilege Groups — reusable collections of privileges</b>");
            gtk_label_set_xalign(GTK_LABEL(groups_title),0);
            gtk_box_pack_start(GTK_BOX(groups_panel),groups_title,FALSE,FALSE,0);
            gtk_paned_pack1(GTK_PANED(privileges_page),grants_panel,TRUE,FALSE);
            gtk_paned_pack2(GTK_PANED(privileges_page),groups_panel,TRUE,FALSE);
            gtk_notebook_append_page(GTK_NOTEBOOK(admin_view),privileges_page,gtk_label_new("Privileges"));
         }
         GtkWidget *panel=grant_table ? grants_panel : groups_panel;
         const char *section=!strcmp(table->name,"assignments") ? "Group grants" :
            !strcmp(table->name,"grants") ? "Individual grants" :
            !strcmp(table->name,"privsets") ? "Groups" : "Privileges in groups";
         GtkWidget *label=gtk_label_new(section);
         gtk_label_set_xalign(GTK_LABEL(label),0);
         gtk_box_pack_start(GTK_BOX(panel),label,FALSE,FALSE,0);
         gtk_box_pack_start(GTK_BOX(panel),box,TRUE,TRUE,0);
      } else {
         gtk_notebook_append_page(GTK_NOTEBOOK(admin_view),box,gtk_label_new(!strcmp(table->name,"audit") ? "Audit Log" :
            !strcmp(table->name,"ptt") ? "PTT Log" :
            !strcmp(table->name,"quotas") ? "Quotas" :
            !strcmp(table->name,"users") ? "Users" :
            !strcmp(table->name,"sessions") ? "Sessions" : "Rooms"));
      }
   }

   {
      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
      GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
      GtkWidget *filter_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
      control_filter = gtk_search_entry_new();
      gtk_entry_set_width_chars(GTK_ENTRY(control_filter), 24);
      gtk_entry_set_placeholder_text(GTK_ENTRY(control_filter), "Filter controls… (=key, ^prefix)");
      gtk_box_pack_start(GTK_BOX(filter_header), gtk_label_new("Filter:"), FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(filter_header), control_filter, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(filter_header), gtk_label_new("Host / Node:"), FALSE, FALSE, 0);
      control_hosts = gtk_combo_box_text_new();
      gtk_widget_set_hexpand(control_hosts, TRUE);
      gtk_box_pack_start(GTK_BOX(filter_header), control_hosts, TRUE, TRUE, 0);
      admin_control_hosts_refresh();
      control_host_handler = g_signal_connect(control_hosts, "changed", G_CALLBACK(admin_control_host_changed), NULL);
      g_signal_connect(main_window, "key-press-event", G_CALLBACK(admin_controls_star), NULL);
      gtk_box_pack_start(GTK_BOX(box), filter_header, FALSE, FALSE, 0);
      g_signal_connect(control_filter, "changed", G_CALLBACK(admin_control_filter_changed), NULL);
      control_status = gtk_label_new("Select a connected server and Refresh.");
      control_combo = gtk_combo_box_text_new();
      control_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
      gtk_scale_set_digits(GTK_SCALE(control_scale), 0);
      gtk_widget_set_sensitive(control_scale, FALSE);
      GtkWidget *refresh = gtk_button_new_with_label("Refresh controls");
      control_apply = gtk_button_new_with_label("Revert");
      gtk_widget_set_sensitive(control_apply, FALSE);
      gtk_box_pack_start(GTK_BOX(buttons), refresh, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(buttons), control_apply, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(box), control_status, FALSE, FALSE, 0);
      GtkWidget *control_headers = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
      const char *headings[] = {
         "Node / Control", "Value", "Description / Access"
      };

      for (int i = 0 ; i < 3 ; i++) {
         GtkWidget *header = gtk_button_new_with_label(headings[i]);
         g_object_set_data(G_OBJECT(header), "sort-field", GINT_TO_POINTER(i));

         if (i < 2) {
            gtk_widget_set_size_request(header, i == 0 ? 110 : 125, -1);
         } else {
            gtk_widget_set_hexpand(header, TRUE);
         }
         g_signal_connect(header, "clicked", G_CALLBACK(admin_control_sort_clicked), NULL);
         gtk_box_pack_start(GTK_BOX(control_headers), header, i == 2, i == 2, 0);
      }

      gtk_box_pack_start(GTK_BOX(box), control_headers, FALSE, FALSE, 0);
      control_rows_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
      control_row_buttons = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
      control_scroll = gtk_scrolled_window_new(NULL, NULL);
      gtk_widget_set_vexpand(control_scroll, TRUE);
      gtk_container_add(GTK_CONTAINER(control_scroll), control_rows_box);
      gtk_box_pack_start(GTK_BOX(box), control_scroll, TRUE, TRUE, 0);
      /* Keep old selection machinery hidden for protocol response correlation. */
      gtk_widget_set_no_show_all(control_combo, TRUE);
      gtk_widget_set_no_show_all(control_scale, TRUE);
      gtk_box_pack_start(GTK_BOX(box), control_combo, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(box), control_scale, FALSE, FALSE, 0);

      gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
      g_signal_connect(refresh, "clicked", G_CALLBACK(admin_control_refresh), NULL);
      g_signal_connect(control_combo, "changed", G_CALLBACK(admin_control_selection), NULL);
      g_signal_connect(control_apply, "clicked", G_CALLBACK(admin_control_apply), NULL);
      gtk_notebook_append_page(GTK_NOTEBOOK(admin_view), box, gtk_label_new("Controls"));
      g_object_set_data(G_OBJECT(control_combo), "control-page", box);
   }
   /* Operator workflow first; diagnostics and history last. */
   const char *order[] = {
      "Controls", "Sessions", "Users", "Rooms", "Assignments",
      "Direct Grants", "Privilege Sets", "Quotas", "Audit Log", "PTT Log"
   };

   for (guint position = 0 ; position < G_N_ELEMENTS(order) ; ++position) {
      gint count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(admin_view));

      for (gint j = position ; j < count ; ++j) {
         GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(admin_view), j);
         GtkWidget *tab = gtk_notebook_get_tab_label(GTK_NOTEBOOK(admin_view), page);

         if (GTK_IS_LABEL(tab) && !strcmp(gtk_label_get_text(GTK_LABEL(tab)), order[position])) {
            gtk_notebook_reorder_child(GTK_NOTEBOOK(admin_view), page, position);
            break;
         }
      }
   }

   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), admin_view, gtk_label_new("(1) Admin"));

   return admin_view;
}

/* Nonblocking: several elmers/noobs and background server profiles may coexist. */
typedef struct {
   GtkWidget *dialog;
   char *profile, *id, *room;
   rrconn_t *connection;
} supervision_dialog_t;
static GList *supervision_dialogs;
static void supervision_response(GtkDialog *dialog, gint response, gpointer data) {
   supervision_dialog_t *prompt = data;
   rrconn_t *client = rrclient_connection_find(prompt->profile);

   if ((response == GTK_RESPONSE_YES || response == GTK_RESPONSE_NO) && client == prompt->connection && client->authenticated) {
      dict *d = dict_new();

      if (d) {
         char tail[96];
         snprintf(tail, sizeof(tail), "%s %s", response == GTK_RESPONSE_YES ? "accept" : "decline", prompt->id);
         dict_add(d, "msg.type", "talk");
         dict_add(d, "talk.cmd", "supervise");
         dict_add(d, "talk.room", prompt->room);
         dict_add(d, "talk.data", tail);
         ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT);
         dict_free(d);
      }
   }
   supervision_dialogs = g_list_remove(supervision_dialogs, prompt);
   gtk_widget_destroy(GTK_WIDGET(dialog));
   g_free(prompt->profile);
   g_free(prompt->id);
   g_free(prompt->room);
   g_free(prompt);
}
void gtk_supervision_result(const char *data, rrconn_t *connection) {
   dict *d = data ? json2dict(data) : NULL;
   const char *id = d ? dict_get(d, "talk.target", "") : "";
   const char *action = d ? dict_get(d, "talk.action", "") : "closed";

   for (GList *p = supervision_dialogs, *next ; p ; p = next) {
      next = p->next;
      supervision_dialog_t *prompt = p->data;

      if (prompt->connection == connection && (!d || !strcmp(prompt->id, id))) {
         if (!strcmp(action, "request")) {
            dict_free(d);

            return;
         }
         gtk_dialog_response(GTK_DIALOG(prompt->dialog), GTK_RESPONSE_CANCEL);
      }
   }

   if (d && !strcmp(action, "request")) {
      supervision_dialog_t *prompt = g_new0(supervision_dialog_t, 1);
      prompt->profile = g_strdup(rrclient_connection_name(connection));
      prompt->connection = connection;
      prompt->id = g_strdup(id);
      prompt->room = g_strdup(dict_get(d, "talk.room", ""));
      prompt->dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
         "Supervise %s in %s (%s)?", dict_get(d, "talk.user", ""), prompt->room, prompt->profile);
      gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(prompt->dialog), "%s", dict_get(d, "talk.data", ""));
      gtk_dialog_add_buttons(GTK_DIALOG(prompt->dialog), "Decline", GTK_RESPONSE_NO, "Accept responsibility", GTK_RESPONSE_YES, NULL);
      g_signal_connect(prompt->dialog, "response", G_CALLBACK(supervision_response), prompt);
      supervision_dialogs = g_list_prepend(supervision_dialogs, prompt);
      gtk_widget_show(prompt->dialog);
   }

   if (rrclient_present_context()) {
      ptt_button_refresh();
   }
   dict_free(d);
}
