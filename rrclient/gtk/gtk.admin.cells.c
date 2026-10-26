// Cell renderers for database-backed administrative tables.
#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <time.h>
#include <librustyaxe/util.time.h>
#include <rrclient/gtk/gtk.admin.cells.h>

static char *admin_short_uuid(const char *uuid) {
   if (!uuid) {
      return g_strdup("");
   }
   size_t length = strlen(uuid);

   if (length <= 18) {
      return g_strdup(uuid);
   }

   return g_strdup_printf("%.8s..%s", uuid, uuid + length - 8);
}
void admin_uuid_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   (void)column;
   int field = GPOINTER_TO_INT(data);
   char *raw = NULL;
   gtk_tree_model_get(model, iter, field, &raw, -1);
   char *shortened = admin_short_uuid(raw);
   g_object_set(renderer, "text", shortened, NULL);
   g_free(shortened);
   g_free(raw);
}
static void admin_copy_uuid_clicked(GtkMenuItem *item, gpointer data) {
   (void)data;
   const char *uuid = g_object_get_data(G_OBJECT(item), "full-uuid");

   if (!uuid) {
      return;
   }
   GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
   gtk_clipboard_set_text(clipboard, uuid, -1);
}
gboolean admin_uuid_menu(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   admin_table_t *table = data;

   if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
      return FALSE;
   }
   GtkTreePath *path = NULL;
   GtkTreeViewColumn *column = NULL;

   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (int)event->x, (int)event->y, &path, &column, NULL, NULL)) {
      return FALSE;
   }
   int field = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "rr-uuid-field"));

   if (!g_object_get_data(G_OBJECT(column), "rr-is-uuid")) {
      gtk_tree_path_free(path);

      return FALSE;
   }
   GtkTreeIter iter;
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
   char *raw = NULL;

   if (gtk_tree_model_get_iter(model, &iter, path)) {
      gtk_tree_model_get(model, &iter, field, &raw, -1);
   }
   gtk_tree_path_free(path);

   if (!raw || !*raw) {
      g_free(raw);

      return FALSE;
   }
   GtkWidget *menu = gtk_menu_new();
   GtkWidget *copy = gtk_menu_item_new_with_label("Copy UUID");
   g_object_set_data_full(G_OBJECT(copy), "full-uuid", raw, g_free);
   g_signal_connect(copy, "activate", G_CALLBACK(admin_copy_uuid_clicked), NULL);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy);
   gtk_widget_show_all(menu);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
}
void admin_column_wrap_width(GObject *object, GParamSpec *pspec, gpointer data) {
   (void)pspec;
   GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN(object);
   GtkCellRenderer *renderer = GTK_CELL_RENDERER(data);
   int width = gtk_tree_view_column_get_width(column) - 12;

   if (width < 48) {
      width = 48;
   }
   int previous = 0;
   g_object_get(renderer, "wrap-width", &previous, NULL);

   if (previous != width) {
      g_object_set(renderer, "wrap-width", width, NULL);
   }
}
void admin_user_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
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
void admin_session_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
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

void admin_numeric_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
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


static void admin_room_flag_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user) {
   (void)column;
   int field = GPOINTER_TO_INT(user);
   char *value = NULL;
   gtk_tree_model_get(model, iter, field, &value, -1);
   g_object_set(renderer, "active", value && (!strcmp(value, "1") || !g_ascii_strcasecmp(value, "true")), NULL);
   g_free(value);
}
static void admin_privilege_cell(GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, int field, bool remote) {
   char *value = NULL;
   gtk_tree_model_get(model, iter, field, &value, -1);
   if (remote) {
      bool enabled = value && (!g_ascii_strcasecmp(value, "true") || !g_ascii_strcasecmp(value, "yes") || !strcmp(value, "1"));
      g_object_set(renderer, "active", enabled, "activatable", FALSE, NULL);
   } else {
      char *display = NULL;
      if (!value || !*value || !strcmp(value, "0")) {
         display = g_strdup("Never");
      } else {
         char *end = NULL;
         gint64 timestamp = g_ascii_strtoll(value, &end, 10);
         if (end && !*end && timestamp > 0) {
            GDateTime *date = g_date_time_new_from_unix_local(timestamp);
            if (date) {
               display = g_date_time_format(date, "%Y-%m-%d %H:%M");
               g_date_time_unref(date);
            }
         }
         if (!display) display = g_strdup(value);
      }
      g_object_set(renderer, "text", display, NULL);
      g_free(display);
   }
   g_free(value);
}

void admin_log_stripe_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   int field = GPOINTER_TO_INT(data);
   admin_table_t *table = g_object_get_data(G_OBJECT(column), "rr-admin-table");

   if (table) {
      if (!strcmp(table->name, "ptt") && (field == 3 || field == 4 || field == 9)) {
         admin_numeric_cell(column, renderer, model, iter, data);
      } else if (!strcmp(table->name, "rooms") && (field == 2 || field == 4)) {
         admin_room_flag_cell(column, renderer, model, iter, data);
      } else if (!strcmp(table->name, "users") && (field == 1 || field == 7 || field == 5 || field == 6)) {
         admin_user_cell(column, renderer, model, iter, data);
      } else if ((!strcmp(table->name, "assignments") && (field == 5 || field == 6)) ||
         (!strcmp(table->name, "grants") && (field == 6 || field == 7))) {
         admin_privilege_cell(renderer, model, iter, field,
            (!strcmp(table->name, "assignments") && field == 6) ||
            (!strcmp(table->name, "grants") && field == 7));
      } else if (!strcmp(table->name, "sessions") && field == 5) {
         admin_session_cell(column, renderer, model, iter, NULL);
      } else if (!strcmp(table->name, "quotas") && field > 0) {
         admin_numeric_cell(column, renderer, model, iter, data);
      } else if (!strcmp(table->fields[field], "uuid") || !strcmp(table->fields[field], "resource_uuid")) {
         admin_uuid_cell(column, renderer, model, iter, data);
      }
   }
   GtkTreePath *path = gtk_tree_model_get_path(model, iter);
   int *indices = path ? gtk_tree_path_get_indices(path) : NULL;
   gboolean odd = indices && (indices[0] & 1);
   gboolean selected = table && path && gtk_tree_selection_path_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree)), path);

   if (odd && !selected && table) {
      GtkStyleContext *style = gtk_widget_get_style_context(table->tree);
      GdkRGBA base, ink, shade;
      gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL, &base);
      gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &ink);
      const double mix = 0.085;
      shade.red = base.red * (1.0 - mix) + ink.red * mix;
      shade.green = base.green * (1.0 - mix) + ink.green * mix;
      shade.blue = base.blue * (1.0 - mix) + ink.blue * mix;
      shade.alpha = 1.0;
      g_object_set(renderer, "cell-background-rgba", &shade, "cell-background-set", TRUE, NULL);
   } else {
      g_object_set(renderer, "cell-background-set", FALSE, NULL);
   }

   if (path) {
      gtk_tree_path_free(path);
   }
}

/* Never pass untrusted network text directly to Pango. Keep the wire value unchanged elsewhere, and warn once per malformed field for diagnostics. */
char *admin_display_utf8(const char *value, const char *field) {
   if (!value) {
      return g_strdup("");
   }

   if (g_utf8_validate(value, -1, NULL)) {
      return g_strdup(value);
   }
   static GHashTable *reported;

   if (!reported) {
      reported = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   }

   if (!g_hash_table_contains(reported, field)) {
      g_warning("Admin display received invalid UTF-8 in %s", field);
      g_hash_table_add(reported, g_strdup(field));
   }

   return g_utf8_make_valid(value, -1);
}
