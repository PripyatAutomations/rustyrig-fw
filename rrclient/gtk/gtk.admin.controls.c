// Generic platform and node controls: GTK presentation only.
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
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
#include <rrclient/gtk/gtk.admin.controls.h>

extern GtkWidget *admin_view;
extern GtkWidget *main_notebook;
static GtkWidget *control_combo, *control_scale, *control_status, *control_apply;
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
static void admin_control_stripe_rows(void) {
   if (!control_rows_box) return;
   GList *rows=gtk_container_get_children(GTK_CONTAINER(control_rows_box));
   guint index=0;
   for (GList *l=rows;l;l=l->next) {
      GtkWidget *row=l->data;
      GtkStyleContext *style=gtk_widget_get_style_context(row);
      gtk_style_context_remove_class(style,"rr-control-odd");
      if (gtk_widget_get_visible(row) && (index++ & 1))
         gtk_style_context_add_class(style,"rr-control-odd");
   }
   g_list_free(rows);
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
   admin_control_stripe_rows();
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
   admin_control_stripe_rows();
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
   gtk_widget_set_size_request(title, 240, -1);
   gtk_widget_set_hexpand(title, FALSE);
   gtk_box_pack_start(GTK_BOX(row), title, FALSE, FALSE, 0);
   GtkWidget *editor = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_widget_set_size_request(editor, 300, -1);
   gtk_widget_set_hexpand(editor, FALSE);
   GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_scale_set_digits(GTK_SCALE(scale), 0);
   gtk_widget_set_hexpand(scale, TRUE);
   GtkWidget *minimum_label = gtk_label_new("0"), *maximum_label = gtk_label_new("100");
   gtk_box_pack_start(GTK_BOX(editor), minimum_label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(editor), scale, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(editor), maximum_label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(row), editor, FALSE, FALSE, 0);
   GtkWidget *detail = gtk_label_new("");
   gtk_label_set_xalign(GTK_LABEL(detail), 0);
   gtk_label_set_line_wrap(GTK_LABEL(detail), TRUE);
   gtk_widget_set_hexpand(detail, TRUE);
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


bool gtk_admin_controls_result(const char *name, const char *action, dict *row, rrconn_t *connection) {
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
      return true;
   }
   return false;
}

void gtk_admin_controls_refresh(void) {
   if (control_combo) {
      admin_control_refresh(NULL, NULL);
   }
}

GtkWidget *gtk_admin_controls_page(void) {
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
            gtk_widget_set_size_request(header, i == 0 ? 240 : 300, -1);
         } else {
            gtk_widget_set_hexpand(header, TRUE);
         }
         g_signal_connect(header, "clicked", G_CALLBACK(admin_control_sort_clicked), NULL);
         gtk_box_pack_start(GTK_BOX(control_headers), header, i == 2, i == 2, 0);
      }

      gtk_box_pack_start(GTK_BOX(box), control_headers, FALSE, FALSE, 0);
      control_rows_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
      GtkCssProvider *stripe=gtk_css_provider_new();
      gtk_css_provider_load_from_data(stripe,
         "box.rr-control-odd { background-color: alpha(currentColor, 0.085); }",-1,NULL);
      gtk_style_context_add_provider_for_screen(
         gtk_widget_get_screen(control_rows_box),GTK_STYLE_PROVIDER(stripe),
         GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
      g_object_unref(stripe);
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
      g_object_set_data(G_OBJECT(control_combo), "control-page", box);
   return box;
}
