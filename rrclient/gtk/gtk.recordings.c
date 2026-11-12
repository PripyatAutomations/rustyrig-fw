// Local recording manager; GTK3 floating panel, themed zebra tree. GPL-2.0-only.
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <rrclient/audio.h>
#include <rrclient/vfo.snapshot.h>
#include <rrclient/recordings.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.floating-panel.h>
#include <rrclient/gtk/gtk.freqentry.h>
#include "gtk.tree-style.h"
#include "gtk.recordings.h"
#include "gtk.recording-player.h"
#include <librustyaxe/event-bus.h>
#include <string.h>

extern GtkWidget *main_window, *main_notebook, *freq_entry, *mode_combo;
static struct {
   RRFloatPanel *panel;
   GtkWidget *tree, *status, *format, *record_button;
   GtkTreeStore *store;
   char *directory, *root;
   gboolean closing;
   rr_event_token_t open_token, finished_token, playback_token, locator_token, refresh_token;
   gboolean waiting_locator;
   rr_recording_format requested_format;
} ui;
enum { COL_NAME, COL_SIZE, COL_DATE, COL_DIR, COL_PATH, N_COLS };

static void show_error(const char *message) {
   GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL,
      GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
}
static char *ask_name(const char *title, const char *original) {
   GtkWidget *dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(main_window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(entry), original ? original : "");
   gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
   gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
      entry, FALSE, FALSE, 12);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
   gtk_widget_show_all(dialog);
   char *answer = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT ?
      g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))) : NULL;
   gtk_widget_destroy(dialog);
   return answer;
}
static gboolean valid_name(const char *name) {
   return name && *name && strcmp(name, ".") && strcmp(name, "..") &&
      !strchr(name, '/') && !strchr(name, '\\');
}
static gboolean in_root(const char *path) {
   if (!path || !ui.root) return FALSE;
   return g_str_has_prefix(path, ui.root) &&
      (path[strlen(ui.root)] == '/' || path[strlen(ui.root)] == 0);
}
/* A destination is only valid within the managed recordings tree. Avoid
 * following a symlink to a directory outside the tree while moving files. */
static gboolean move_folder_allowed(const char *folder) {
   if (!folder || !ui.root) return FALSE;
   char *canonical = g_canonicalize_filename(folder, NULL);
   char *root = g_canonicalize_filename(ui.root, NULL);
   gboolean allowed = g_str_has_prefix(canonical, root) &&
      (canonical[strlen(root)] == '/' || canonical[strlen(root)] == 0) &&
      g_file_test(canonical, G_FILE_TEST_IS_DIR);
   if (allowed) {
      char *part = g_strdup(root);
      const char *remaining = canonical + strlen(root);
      if (g_file_test(part, G_FILE_TEST_IS_SYMLINK)) allowed = FALSE;
      while (allowed && *remaining) {
         while (*remaining == '/') remaining++;
         if (!*remaining) break;
         const char *slash = strchr(remaining, '/');
         char *component = slash ? g_strndup(remaining, slash - remaining) : g_strdup(remaining);
         char *next = g_build_filename(part, component, NULL);
         g_free(part);
         part = next;
         g_free(component);
         if (g_file_test(part, G_FILE_TEST_IS_SYMLINK)) allowed = FALSE;
         remaining = slash ? slash : remaining + strlen(remaining);
      }
      g_free(part);
   }
   g_free(canonical);
   g_free(root);
   return allowed;
}

/* Enumerate existing recording folders, including nested subdirectories.
 * Symlinked directories are not followed; only destinations in our root
 * are offered. The combo stores absolute paths but displays relative names. */
static void move_folder_collect(GPtrArray *folders, const char *folder, unsigned depth) {
   if (depth >= 32 || !move_folder_allowed(folder)) return;
   g_ptr_array_add(folders, g_strdup(folder));
   GDir *dir = g_dir_open(folder, 0, NULL);
   if (!dir) return;
   const char *name;
   while ((name = g_dir_read_name(dir))) {
      if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
      char *child = g_build_filename(folder, name, NULL);
      if (!g_file_test(child, G_FILE_TEST_IS_SYMLINK) &&
          g_file_test(child, G_FILE_TEST_IS_DIR))
         move_folder_collect(folders, child, depth + 1);
      g_free(child);
   }
   g_dir_close(dir);
}
static gint move_folder_sort(gconstpointer left, gconstpointer right) {
   const char *a = *(char *const *)left, *b = *(char *const *)right;
   return g_utf8_collate(a, b);
}

/* Both destination folder and optional new filename are explicit.
 * The root and every available subdirectory appear in the selector. */
static char *choose_move_destination(const char *source) {
   char *basename = g_path_get_basename(source);
   char *parent = g_path_get_dirname(source);
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Move / Rename Recording",
      GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Move / Rename", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *grid = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
   gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
   GtkWidget *entry = gtk_entry_new();
   GtkWidget *folders = gtk_combo_box_text_new();
   gtk_entry_set_text(GTK_ENTRY(entry), basename);
   gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
   gtk_widget_set_hexpand(entry, TRUE);
   gtk_widget_set_hexpand(folders, TRUE);

   GPtrArray *available = g_ptr_array_new_with_free_func(g_free);
   move_folder_collect(available, ui.root, 0);
   g_ptr_array_sort(available, move_folder_sort);
   for (guint i = 0; i < available->len; i++) {
      const char *dir = g_ptr_array_index(available, i);
      const char *relative = dir + strlen(ui.root);
      while (*relative == G_DIR_SEPARATOR) relative++;
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(folders), dir,
         *relative ? relative : "Recordings (root)");
   }
   if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(folders), parent))
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(folders), ui.root);
   g_ptr_array_free(available, TRUE);

   gtk_grid_attach(GTK_GRID(grid), gtk_label_new("New name:"), 0, 0, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), entry, 1, 0, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Destination folder:"), 0, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), folders, 1, 1, 1, 1);
   gtk_box_pack_start(GTK_BOX(area), grid, TRUE, TRUE, 0);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
   gtk_widget_show_all(dialog);
   char *destination = NULL;
   while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
      const char *folder = gtk_combo_box_get_active_id(GTK_COMBO_BOX(folders));
      if (!valid_name(name) || !move_folder_allowed(folder)) {
         show_error("Select an existing Recordings folder and a valid filename.");
      } else {
         destination = g_build_filename(folder, name, NULL);
         break;
      }
   }
   gtk_widget_destroy(dialog);
   g_free(basename);
   g_free(parent);
   return destination;
}
static char *selected(gboolean *folder) {
   GtkTreeIter iter; GtkTreeModel *model = NULL;
   if (!ui.tree || !gtk_tree_selection_get_selected(
      gtk_tree_view_get_selection(GTK_TREE_VIEW(ui.tree)), &model, &iter)) return NULL;
   char *path = NULL;
   gtk_tree_model_get(model, &iter, COL_PATH, &path, COL_DIR, folder, -1);
   return path;
}
/* Display the real nested directory hierarchy, without following symlinks.
 * Bound expansion so a malicious or enormous tree cannot freeze GTK forever. */
static unsigned refresh_rows;
static void append_directory(GtkTreeIter *parent, const char *directory, unsigned depth) {
   if (depth >= 24 || refresh_rows >= 3000 || !move_folder_allowed(directory)) return;
   GDir *dir = g_dir_open(directory, 0, NULL);
   if (!dir) return;
   const char *name;
   while ((name = g_dir_read_name(dir)) && refresh_rows < 3000) {
      if (name[0] == '.') continue;
      char *path = g_build_filename(directory, name, NULL);
      GFile *file = g_file_new_for_path(path);
      GFileInfo *info = g_file_query_info(file,
         "standard::type,standard::size,time::modified,standard::is-symlink",
         G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
      gboolean folder = info && g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
      gboolean symlink = !info || g_file_info_get_is_symlink(info);
      gboolean audio = g_str_has_suffix(name, ".ogg") || g_str_has_suffix(name, ".flac");
      if (!symlink && (folder || audio)) {
         char *size = folder ? g_strdup("<DIR>") :
             g_format_size(g_file_info_get_size(info));
         guint64 modified = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
         GDateTime *date = modified ? g_date_time_new_from_unix_local((gint64)modified) : NULL;
         char *stamp = date ? g_date_time_format(date, "%Y-%m-%d %H:%M") : g_strdup("");
         GtkTreeIter row;
         gtk_tree_store_append(ui.store, &row, parent);
         gtk_tree_store_set(ui.store, &row, COL_NAME, name, COL_SIZE, size,
            COL_DATE, stamp, COL_DIR, folder, COL_PATH, path, -1);
         refresh_rows++;
         if (folder) append_directory(&row, path, depth + 1);
         if (date) g_date_time_unref(date);
         g_free(stamp); g_free(size);
      }
      if (info) g_object_unref(info);
      g_object_unref(file);
      g_free(path);
   }
   g_dir_close(dir);
}
static void refresh(void) {
   if (!ui.store || !ui.directory) return;
   gtk_tree_store_clear(ui.store);
   refresh_rows = 0;
   if (g_strcmp0(ui.directory, ui.root)) {
      char *parent = g_path_get_dirname(ui.directory);
      if (in_root(parent)) {
         GtkTreeIter up;
         gtk_tree_store_append(ui.store, &up, NULL);
         gtk_tree_store_set(ui.store, &up, COL_NAME, "..",
            COL_SIZE, "<UP>", COL_DATE, "", COL_DIR, TRUE,
            COL_PATH, parent, -1);
      }
      g_free(parent);
   }
   append_directory(NULL, ui.directory, 0);
   gtk_label_set_text(GTK_LABEL(ui.status),
      refresh_rows >= 3000 ? "Showing first 3000 entries (directory tree limit)" :
      ui.directory);
   gtk_tree_view_expand_all(GTK_TREE_VIEW(ui.tree));
}
static void navigate(const char *path) {
   if (!in_root(path)) return;
   g_free(ui.directory);
   ui.directory = g_strdup(path);
   refresh();
}
static void do_play(void) {
   gboolean folder = FALSE;
   char *path = selected(&folder);
   if (path && folder) navigate(path);
   else if (path) gtk_recording_player_show(path);
   g_free(path);
}
static void row_activated(GtkTreeView *view, GtkTreePath *path,
                          GtkTreeViewColumn *col, gpointer data) {
   (void)view; (void)path; (void)col; (void)data;
   do_play();
}
static void rename_pair(const char *ogg, const char *flac) {
   const char *first = *ogg ? ogg : flac;
   if (!first || !*first) return;
   char *basename = g_path_get_basename(first);
   char *dot = strrchr(basename, '.');
   if (dot) *dot = 0;
   char *name = ask_name("Save recording as (optional)", basename);
   if (valid_name(name)) {
      const char *paths[] = {ogg, flac};
      for (unsigned i = 0; i < 2; i++) {
         if (!paths[i] || !*paths[i] || !g_file_test(paths[i], G_FILE_TEST_EXISTS)) continue;
         const char *ext = strrchr(paths[i], '.');
         char *parent = g_path_get_dirname(paths[i]);
         char *newpath = g_strdup_printf("%s/%s%s", parent, name, ext ? ext : "");
         if (g_strcmp0(newpath, paths[i]) && g_file_test(newpath, G_FILE_TEST_EXISTS))
            show_error("A recording with that name already exists.");
         else if (g_strcmp0(newpath, paths[i]) && g_rename(paths[i], newpath))
            show_error("Unable to rename recording.");
         g_free(newpath); g_free(parent);
      }
   }
   g_free(name); g_free(basename);
}
static void recording_finished(const char *event, const char *payload,
                               rrconn_t *connection, void *user) {
   (void)event; (void)connection; (void)user;
   rr_recordings_collect();
   if (ui.record_button) {
      gtk_button_set_label(GTK_BUTTON(ui.record_button), "RECORD");
      gtk_widget_set_sensitive(ui.record_button, TRUE);
   }
   if (!payload) return;
   char **fields = g_strsplit(payload, "\n", 4);
   if (g_strcmp0(fields[0], "ok")) show_error("Recording encoder failed. Check the codec logs and free disk space.");
   else {
      rename_pair(fields[1], fields[2]);
      guint64 dropped = fields[3] ? g_ascii_strtoull(fields[3], NULL, 10) : 0;
      if (ui.status) {
         if (dropped) {
            char *message = g_strdup_printf("Recording saved with %" G_GUINT64_FORMAT " dropped PCM frames", dropped);
            gtk_label_set_text(GTK_LABEL(ui.status), message);
            g_free(message);
         } else gtk_label_set_text(GTK_LABEL(ui.status), "Recording saved.");
      }
   }
   g_strfreev(fields);
   refresh();
}
static void playback_finished(const char *event, const char *payload,
                              rrconn_t *connection, void *user) {
   (void)event; (void)payload; (void)connection; (void)user;
   if (ui.status) gtk_label_set_text(GTK_LABEL(ui.status), "Playback finished");
}
static void recording_refresh(const char *event, const char *payload,
                              rrconn_t *connection, void *user) {
   (void)event; (void)payload; (void)connection; (void)user;
   if (ui.store) refresh();
}
/* Approximate position only: use the center of the station's Maidenhead square. */
static gboolean grid_center(const char *grid, double *lat, double *lon) {
   if (!grid) return FALSE;
   size_t n = strlen(grid);
   if (n < 4 || (n & 1)) return FALSE;
   char a = g_ascii_toupper(grid[0]), b = g_ascii_toupper(grid[1]);
   if (a < 'A' || a > 'R' || b < 'A' || b > 'R' ||
      !g_ascii_isdigit(grid[2]) || !g_ascii_isdigit(grid[3])) return FALSE;
   *lon = -180.0 + (a - 'A') * 20.0 + (grid[2] - '0') * 2.0;
   *lat = -90.0 + (b - 'A') * 10.0 + (grid[3] - '0');
   double lon_step = 2.0, lat_step = 1.0;
   if (n >= 6) {
      a = g_ascii_toupper(grid[4]);
      b = g_ascii_toupper(grid[5]);
      if (a < 'A' || a > 'X' || b < 'A' || b > 'X') return FALSE;
      lon_step /= 24.0;
      lat_step /= 24.0;
      *lon += (a - 'A') * lon_step;
      *lat += (b - 'A') * lat_step;
   }
   if (n >= 8) {
      if (!g_ascii_isdigit(grid[6]) || !g_ascii_isdigit(grid[7])) return FALSE;
      lon_step /= 10.0;
      lat_step /= 10.0;
      *lon += (grid[6] - '0') * lon_step;
      *lat += (grid[7] - '0') * lat_step;
   }
   *lon += lon_step / 2.0;
   *lat += lat_step / 2.0;
   return TRUE;
}
static void locator_record_start(const char *event, const void *data, size_t size,
                                 rrconn_t *conn, void *unused) {
   (void)event; (void)conn; (void)unused;
   if (!ui.waiting_locator) return;
   ui.waiting_locator = FALSE;
   if (!ui.record_button) return;
   const rrclient_locator_snapshot_t *snap =
      data && size == sizeof(rrclient_locator_snapshot_t) ? data : NULL;
   if (snap && (!memchr(snap->profile, 0, sizeof(snap->profile)) ||
      !memchr(snap->room, 0, sizeof(snap->room)) ||
      !memchr(snap->mode, 0, sizeof(snap->mode)) ||
      !memchr(snap->grid, 0, sizeof(snap->grid)) ||
      !memchr(snap->recording_user, 0, sizeof(snap->recording_user)) ||
      !memchr(snap->recording_sources, 0, sizeof(snap->recording_sources)))) snap = NULL;
   char freq[32] = "unknown", vfo[4] = "A";
   const char *site = "local", *rig = "receiver", *mode = "unknown", *server = "";
   const char *grid = "";
   char latitude[32] = "", longitude[32] = "";
   const char *location_source = "";
   char *station = NULL, *rig_name = NULL;
   if (snap && snap->online) {
      g_snprintf(freq, sizeof(freq), "%ld", snap->frequency);
      g_snprintf(vfo, sizeof(vfo), "%c", snap->vfo ? snap->vfo : 'A');
      server = snap->profile;
      mode = snap->mode;
      grid = snap->grid;
      double lat, lon;
      if (grid_center(grid, &lat, &lon)) {
         g_ascii_formatd(latitude, sizeof(latitude), "%.7f", lat);
         g_ascii_formatd(longitude, sizeof(longitude), "%.7f", lon);
         location_source = "maidenhead-grid-center (approximate)";
      }
      const char *room = snap->room[0] == '#' ? snap->room + 1 : snap->room;
      const char *marker = strstr(room, "-rig");
      if (marker) {
         station = g_strndup(room, marker - room);
         rig_name = g_strdup(marker + 1);
         site = station;
         rig = rig_name;
      } else site = room;
   } else {
      if (freq_entry && GTK_IS_WIDGET(freq_entry))
         g_snprintf(freq, sizeof(freq), "%lu", gtk_freq_entry_get_frequency(GTK_FREQ_ENTRY(freq_entry)));
   }
   GDateTime *now = g_date_time_new_now_utc();
   char *start = g_date_time_format(now, "%Y-%m-%dT%H:%M:%SZ");
   rr_recording_tags tags = {
      .title = "RustyRig stereo RX monitor",
      .user = snap && snap->recording_user[0] ? snap->recording_user : g_get_user_name(),
      .start_time = start, .site = site, .server = server,
      .rig = rig, .vfo = vfo, .frequency = freq, .mode = mode,
      .grid = grid, .latitude = latitude, .longitude = longitude,
      .location_source = location_source,
      .sources = snap ? snap->recording_sources : ""
   };
   if (rr_recordings_start(ui.requested_format, &tags)) {
      gtk_button_set_label(GTK_BUTTON(ui.record_button), "STOP");
      gtk_label_set_text(GTK_LABEL(ui.status), "Recording the stereo monitor...");
   } else show_error("Unable to start recording. Check permissions, disk space, and codec logs.");
   gtk_widget_set_sensitive(ui.record_button, TRUE);
   g_free(station); g_free(rig_name); g_free(start); g_date_time_unref(now);
}
static void record_clicked(GtkButton *button, gpointer data) {
   (void)data;
   if (rr_recordings_active()) {
      rr_recordings_stop();
      gtk_button_set_label(button, "Finalizing...");
      gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
      return;
   }
   if (ui.waiting_locator || !gtk_widget_get_sensitive(GTK_WIDGET(button))) return;
   ui.requested_format = (rr_recording_format)gtk_combo_box_get_active(GTK_COMBO_BOX(ui.format));
   ui.waiting_locator = TRUE;
   gtk_button_set_label(button, "Preparing...");
   gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
   event_emit(RRCLIENT_LOCATOR_REQUEST_EVENT, NULL, NULL);
}

/* Keep the two representations of one capture together when managed in the browser. */
static gboolean move_recording_pair(const char *source, const char *destination) {
   const char *extension = strrchr(source, '.');
   if (!extension || (strcmp(extension, ".ogg") && strcmp(extension, ".flac")))
      return g_rename(source, destination) == 0;
   const char *sibling_ext = !strcmp(extension, ".ogg") ? ".flac" : ".ogg";
   char *stem = g_strndup(source, extension - source);
   char *sibling = g_strconcat(stem, sibling_ext, NULL);
   g_free(stem);
   const char *dest_ext = strrchr(destination, '.');
   char *dest_stem = dest_ext && (!strcmp(dest_ext, ".ogg") || !strcmp(dest_ext, ".flac")) ?
      g_strndup(destination, dest_ext - destination) : g_strdup(destination);
   char *other_dest = g_strconcat(dest_stem, sibling_ext, NULL);
   g_free(dest_stem);
   gboolean have_other = g_file_test(sibling, G_FILE_TEST_IS_REGULAR);
   gboolean valid = !g_file_test(destination, G_FILE_TEST_EXISTS) &&
      (!have_other || !g_file_test(other_dest, G_FILE_TEST_EXISTS));
   if (valid && g_rename(source, destination) == 0) {
      if (have_other && g_rename(sibling, other_dest) != 0) {
         g_rename(destination, source);  // roll back if partner cannot move
         valid = FALSE;
      }
   } else valid = FALSE;
   g_free(sibling);
   g_free(other_dest);
   return valid;
}
/* Drag files from this browser onto directory rows. No external URI imports.
 * Pair-aware move maintains Ogg/FLAC siblings together. */
static void drag_file_get(GtkWidget *widget, GdkDragContext *context,
                          GtkSelectionData *selection, guint info, guint time, gpointer unused) {
   (void)widget; (void)context; (void)info; (void)time; (void)unused;
   gboolean folder = FALSE;
   char *path = selected(&folder);
   if (path && !folder && in_root(path)) {
      GFile *file = g_file_new_for_path(path);
      gchar *uri = g_file_get_uri(file);
      gchar *uris[] = {uri, NULL};
      gtk_selection_data_set_uris(selection, uris);
      g_free(uri);
      g_object_unref(file);
   }
   g_free(path);
}
static void drag_file_received(GtkWidget *widget, GdkDragContext *context,
                               gint x, gint y, GtkSelectionData *selection,
                               guint info, guint time, gpointer unused) {
   (void)info; (void)unused;
   gboolean success = FALSE;
   GtkTreePath *row_path = NULL;
   GtkTreeViewDropPosition position;
   if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(widget), x, y,
                                        &row_path, &position) && row_path) {
      GtkTreeIter iter;
      if (gtk_tree_model_get_iter(GTK_TREE_MODEL(ui.store), &iter, row_path)) {
         gboolean folder = FALSE;
         char *target = NULL;
         gtk_tree_model_get(GTK_TREE_MODEL(ui.store), &iter,
                            COL_DIR, &folder, COL_PATH, &target, -1);
         gchar **uris = gtk_selection_data_get_uris(selection);
         if (folder && target && move_folder_allowed(target) &&
             uris && uris[0] && !uris[1]) {
            GFile *source_file = g_file_new_for_uri(uris[0]);
            char *source = g_file_get_path(source_file);
            if (source && in_root(source) &&
                g_file_test(source, G_FILE_TEST_IS_REGULAR) &&
                !g_file_test(source, G_FILE_TEST_IS_SYMLINK)) {
               char *basename = g_path_get_basename(source);
               char *destination = g_build_filename(target, basename, NULL);
               success = g_strcmp0(source, destination) != 0 &&
                         move_recording_pair(source, destination);
               g_free(destination);
               g_free(basename);
            }
            g_free(source);
            g_object_unref(source_file);
         }
         g_strfreev(uris);
         g_free(target);
      }
      gtk_tree_path_free(row_path);
   }
   gtk_drag_finish(context, success, FALSE, time);
   if (success) refresh();
   else show_error("Move failed. Drop an existing recording onto a folder inside Recordings.");
}
static void action(GtkButton *button, gpointer data) {
   (void)button;
   const char *op = data;
   gboolean folder = FALSE;
   char *path = selected(&folder);
   /* '..' is navigational only, never a rename/delete/move target. */
   GtkTreeIter sel_iter; GtkTreeModel *sel_model = NULL;
   char *sel_name = NULL;
   if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(ui.tree)),
                                        &sel_model, &sel_iter))
      gtk_tree_model_get(sel_model, &sel_iter, COL_NAME, &sel_name, -1);
   if (!g_strcmp0(sel_name, "..") && strcmp(op, "play") &&
       strcmp(op, "up") && strcmp(op, "refresh")) {
      g_free(path); g_free(sel_name); return;
   }
   g_free(sel_name);
   if (!strcmp(op, "play")) do_play();
   else if (!strcmp(op, "stop")) {
      audio_recording_play_stop();
      gtk_label_set_text(GTK_LABEL(ui.status), "Stopping playback...");
   } else if (!strcmp(op, "up")) {
      if (g_strcmp0(ui.directory, ui.root)) {
         char *parent = g_path_get_dirname(ui.directory);
         navigate(parent); g_free(parent);
      }
   } else if (!strcmp(op, "refresh")) refresh();
   else if (!strcmp(op, "mkdir")) {
      char *name = ask_name("New Folder", "");
      if (valid_name(name)) {
         char *dest = g_build_filename(ui.directory, name, NULL);
         if (g_mkdir(dest, 0700)) show_error("Unable to create folder.");
         g_free(dest); refresh();
      } g_free(name);
   } else if (path && !strcmp(op, "rename")) {
      char *dest = choose_move_destination(path);
      if (dest && g_strcmp0(dest, path)) {
         char *canonical = g_canonicalize_filename(dest, NULL);
         char *parent = g_path_get_dirname(canonical);
         gboolean nested_folder = folder && g_str_has_prefix(canonical, path) &&
            canonical[strlen(path)] == '/';
         if (!move_folder_allowed(parent) || nested_folder ||
             g_file_test(canonical, G_FILE_TEST_EXISTS))
            show_error("Destination must be an unused name in a valid Recordings folder.");
         else if (folder ? (g_rename(path, canonical) != 0) :
                  !move_recording_pair(path, canonical))
            show_error("Unable to move recording (or its paired format).");
         g_free(parent); g_free(canonical); refresh();
      }
      g_free(dest);
   } else if (path && !folder && !strcmp(op, "download")) {
      GtkWidget *dialog = gtk_file_chooser_dialog_new("Save Copy", GTK_WINDOW(main_window),
         GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL,
         "_Save", GTK_RESPONSE_ACCEPT, NULL);
      char *base = g_path_get_basename(path);
      gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), base);
      gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
      if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
         char *dest = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
         GFile *from = g_file_new_for_path(path), *to = g_file_new_for_path(dest);
         GError *err = NULL;
         if (!g_file_copy(from, to, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &err)) {
            show_error(err ? err->message : "Download failed");
            g_clear_error(&err);
         }
         g_object_unref(from); g_object_unref(to); g_free(dest);
      }
      g_free(base); gtk_widget_destroy(dialog);
   } else if (path && !strcmp(op, "delete")) {
      GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
         GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
         "Delete %s?", path);
      gboolean yes = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;
      gtk_widget_destroy(dialog);
      if (yes) {
         int result = folder ? g_rmdir(path) : g_unlink(path);
         if (result) show_error("Unable to delete (folders must be empty).");
         refresh();
      }
   }
   g_free(path);
}
static void closed(gpointer unused) {
   (void)unused;
   if (ui.panel) rr_floating_panel_destroy(ui.panel);
}
static void destroyed(GtkWidget *widget, gpointer unused) {
   (void)widget; (void)unused;
   ui.panel = NULL;
   ui.tree = ui.status = ui.format = ui.record_button = NULL;
   if (ui.store) { g_object_unref(ui.store); ui.store = NULL; }
   g_clear_pointer(&ui.directory, g_free);
   g_clear_pointer(&ui.root, g_free);
}
static void open_browser(const char *event, const char *payload,
                         rrconn_t *connection, void *user) {
   (void)event; (void)payload; (void)connection; (void)user;
   if (ui.panel) { rr_floating_panel_restore(ui.panel); rr_floating_panel_show(ui.panel); return; }
   GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;
   if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) return;
   ui.panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook),
      "client-recordings", "Recordings");
   if (!ui.panel) return;
   rr_floating_panel_set_close_callback(ui.panel, closed, NULL);
   GtkWidget *body = rr_floating_panel_body(ui.panel);
   gtk_widget_set_size_request(body, 850, 520);
   GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(body), top, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(top), gtk_label_new("Format:"), FALSE, FALSE, 0);
   ui.format = gtk_combo_box_text_new();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.format), "Ogg / Vorbis");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.format), "FLAC");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.format), "Both");
   gtk_combo_box_set_active(GTK_COMBO_BOX(ui.format), 0);
   gtk_box_pack_start(GTK_BOX(top), ui.format, FALSE, FALSE, 0);
   ui.record_button = gtk_button_new_with_label(rr_recordings_active() ? "STOP" : "RECORD");
   g_signal_connect(ui.record_button, "clicked", G_CALLBACK(record_clicked), NULL);
   gtk_box_pack_end(GTK_BOX(top), ui.record_button, FALSE, FALSE, 0);
   ui.store = gtk_tree_store_new(N_COLS, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);
   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(ui.store), COL_NAME, GTK_SORT_ASCENDING);
   ui.tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ui.store));
   rr_gtk_tree_style(ui.tree);
   const char *columns[] = {"Name", "Size", "Modified"};
   for (unsigned i = 0; i < 3; i++) {
      GtkCellRenderer *cell = gtk_cell_renderer_text_new();
      GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(columns[i], cell, "text", i, NULL);
      gtk_tree_view_column_set_cell_data_func(col, cell, rr_gtk_tree_stripe_cell, ui.tree, NULL);
      gtk_tree_view_column_set_resizable(col, TRUE);
      gtk_tree_view_column_set_sort_column_id(col, i);
      if (i == 0) gtk_tree_view_column_set_expand(col, TRUE);
      gtk_tree_view_append_column(GTK_TREE_VIEW(ui.tree), col);
   }
   g_signal_connect(ui.tree, "row-activated", G_CALLBACK(row_activated), NULL);
   GtkTargetEntry drag_targets[] = {{"text/uri-list", 0, 0}};
   gtk_drag_source_set(ui.tree, GDK_BUTTON1_MASK, drag_targets, 1, GDK_ACTION_MOVE);
   gtk_drag_dest_set(ui.tree, GTK_DEST_DEFAULT_ALL, drag_targets, 1, GDK_ACTION_MOVE);
   g_signal_connect(ui.tree, "drag-data-get", G_CALLBACK(drag_file_get), NULL);
   g_signal_connect(ui.tree, "drag-data-received", G_CALLBACK(drag_file_received), NULL);

   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(scroll), ui.tree);
   gtk_box_pack_start(GTK_BOX(body), scroll, TRUE, TRUE, 0);
   GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   gtk_box_pack_start(GTK_BOX(body), buttons, FALSE, FALSE, 0);
   const char *names[] = {"Up", "Refresh", "Play", "Stop Playback", "Rename / Move", "Download", "New Folder", "Delete"};
   const char *ops[] = {"up", "refresh", "play", "stop", "rename", "download", "mkdir", "delete"};
   for (unsigned i = 0; i < G_N_ELEMENTS(names); i++) {
      GtkWidget *btn = gtk_button_new_with_label(names[i]);
      gtk_box_pack_start(GTK_BOX(buttons), btn, FALSE, FALSE, 0);
      g_signal_connect(btn, "clicked", G_CALLBACK(action), (gpointer)ops[i]);
   }
   ui.status = gtk_label_new("");
   gtk_label_set_xalign(GTK_LABEL(ui.status), 0);
   gtk_box_pack_start(GTK_BOX(body), ui.status, FALSE, FALSE, 0);
   g_signal_connect(rr_floating_panel_widget(ui.panel), "destroy", G_CALLBACK(destroyed), NULL);
   ui.root = rr_recordings_directory();
   ui.directory = g_strdup(ui.root);
   refresh();
   gtk_widget_show_all(body);
   rr_floating_panel_show(ui.panel);
}
void gtk_recordings_init(void) {
   ui.open_token = event_on_domain_token("client.recordings.open", open_browser, NULL, "gtk");
   ui.finished_token = event_on_domain_token("client.recordings.finished", recording_finished, NULL, "gtk");
   ui.playback_token = event_on_domain_token("client.recordings.playback-finished", playback_finished, NULL, "gtk");
   ui.locator_token = event_on_binary_domain_token(RRCLIENT_LOCATOR_SNAPSHOT_EVENT, locator_record_start, NULL, "gtk");
   ui.refresh_token = event_on_domain_token("client.recordings.refresh", recording_refresh, NULL, "gtk");
}
void gtk_recordings_shutdown(void) {
   event_off_token(ui.open_token);
   event_off_token(ui.finished_token);
   event_off_token(ui.playback_token);
   event_off_token(ui.locator_token);
   event_off_token(ui.refresh_token);
   ui.waiting_locator = FALSE;
   ui.open_token = ui.finished_token = ui.playback_token = ui.locator_token = ui.refresh_token = NULL;
   rr_recordings_stop();
   if (ui.panel) rr_floating_panel_destroy(ui.panel);
}
