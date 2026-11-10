// Local recording manager; GTK3 floating panel, themed zebra tree. GPL-2.0-only.
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <rrclient/audio.h>
#include <rrclient/recordings.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.floating-panel.h>
#include <rrclient/gtk/gtk.freqentry.h>
#include "gtk.tree-style.h"
#include "gtk.recordings.h"
#include <librustyaxe/event-bus.h>
#include <string.h>

extern GtkWidget *main_window, *main_notebook, *freq_entry, *mode_combo;
static struct {
   RRFloatPanel *panel;
   GtkWidget *tree, *status, *format, *record_button;
   GtkListStore *store;
   char *directory, *root;
   gboolean closing;
   rr_event_token_t open_token, finished_token, playback_token;
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
static char *selected(gboolean *folder) {
   GtkTreeIter iter; GtkTreeModel *model = NULL;
   if (!ui.tree || !gtk_tree_selection_get_selected(
      gtk_tree_view_get_selection(GTK_TREE_VIEW(ui.tree)), &model, &iter)) return NULL;
   char *path = NULL;
   gtk_tree_model_get(model, &iter, COL_PATH, &path, COL_DIR, folder, -1);
   return path;
}
static void refresh(void) {
   if (!ui.store || !ui.directory) return;
   gtk_list_store_clear(ui.store);
   GDir *dir = g_dir_open(ui.directory, 0, NULL);
   if (!dir) { gtk_label_set_text(GTK_LABEL(ui.status), "Unable to open directory"); return; }
   const char *name;
   while ((name = g_dir_read_name(dir))) {
      if (name[0] == '.') continue;
      char *path = g_build_filename(ui.directory, name, NULL);
      GFileInfo *info = NULL;
      GFile *file = g_file_new_for_path(path);
      info = g_file_query_info(file, "standard::type,standard::size,time::modified,standard::is-symlink",
         G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
      gboolean folder = info && g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
      gboolean symlink = info && g_file_info_get_is_symlink(info);
      gboolean audio = g_str_has_suffix(name, ".ogg") || g_str_has_suffix(name, ".flac");
      if (!symlink && (folder || audio)) {
         guint64 bytes = info ? g_file_info_get_size(info) : 0;
         char *size = folder ? g_strdup("<DIR>") : g_format_size(bytes);
         guint64 modified = info ? g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED) : 0;
         GDateTime *date = modified ? g_date_time_new_from_unix_local((gint64)modified) : NULL;
         char *stamp = date ? g_date_time_format(date, "%Y-%m-%d %H:%M") : g_strdup("");
         GtkTreeIter row; gtk_list_store_append(ui.store, &row);
         gtk_list_store_set(ui.store, &row, COL_NAME, name, COL_SIZE, size,
            COL_DATE, stamp, COL_DIR, folder, COL_PATH, path, -1);
         if (date) g_date_time_unref(date);
         g_free(stamp); g_free(size);
      }
      if (info) g_object_unref(info);
      g_object_unref(file);
      g_free(path);
   }
   g_dir_close(dir);
   gtk_label_set_text(GTK_LABEL(ui.status), ui.directory);
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
   else if (path && !audio_recording_play(path))
      show_error("Playback is already active or the audio engine is unavailable.");
   else if (path) gtk_label_set_text(GTK_LABEL(ui.status), "Playing recording...");
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
   if (g_strcmp0(fields[0], "ok")) show_error("Recording encoder failed. Check ffmpeg and free disk space.");
   else {
      rename_pair(fields[1], fields[2]);
      if (ui.status) gtk_label_set_text(GTK_LABEL(ui.status),
         "Recording saved. Refresh to update the file listing.");
   }
   g_strfreev(fields);
   refresh();
}
static void playback_finished(const char *event, const char *payload,
                              rrconn_t *connection, void *user) {
   (void)event; (void)payload; (void)connection; (void)user;
   audio_recording_play_collect();
   if (ui.status) gtk_label_set_text(GTK_LABEL(ui.status), "Playback complete");
}
static void record_clicked(GtkButton *button, gpointer data) {
   (void)data;
   if (rr_recordings_active()) {
      rr_recordings_stop();
      gtk_button_set_label(button, "Finalizing...");
      gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
      return;
   }
   if (gtk_widget_get_sensitive(GTK_WIDGET(button)) == FALSE) return;
   char freq[32] = "unknown";
   if (freq_entry && GTK_IS_WIDGET(freq_entry))
      g_snprintf(freq, sizeof(freq), "%lu", gtk_freq_entry_get_frequency(GTK_FREQ_ENTRY(freq_entry)));
   gchar *mode = mode_combo && GTK_IS_COMBO_BOX_TEXT(mode_combo) ?
      gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(mode_combo)) : NULL;
   GDateTime *now = g_date_time_new_now_utc();
   char *start = g_date_time_format(now, "%Y-%m-%dT%H:%M:%SZ");
   // The displayed VFO is read from the GTK-owned snapshot, not from live core connection structures.
   const char *shown = freq_entry ? g_object_get_data(G_OBJECT(freq_entry), "rr-displayed-vfo") : NULL;
   char **pieces = shown ? g_strsplit(shown, "/", 2) : NULL;
   rr_recording_tags tags = {
      .title = "RustyRig stereo RX monitor", .user = g_get_user_name(),
      .start_time = start, .site = pieces && pieces[0] ? pieces[0] : "local",
      .server = "", .rig = pieces && pieces[0] ? pieces[0] : "receiver",
      .vfo = pieces && pieces[1] ? pieces[1] : "A",
      .frequency = freq, .mode = mode ? mode : "unknown",
      .grid = "", .latitude = "", .longitude = "", .sources = ""
   };
   rr_recording_format format = (rr_recording_format)gtk_combo_box_get_active(GTK_COMBO_BOX(ui.format));
   if (rr_recordings_start(format, &tags)) {
      gtk_button_set_label(button, "STOP");
      gtk_label_set_text(GTK_LABEL(ui.status), "Recording the stereo monitor...");
   } else show_error("Unable to start recording. Ensure ffmpeg is installed.");
   if (pieces) g_strfreev(pieces);
   g_free(start); g_free(mode); g_date_time_unref(now);
}
static void action(GtkButton *button, gpointer data) {
   const char *op = data;
   gboolean folder = FALSE;
   char *path = selected(&folder);
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
      char *base = g_path_get_basename(path);
      char *name = ask_name("Rename / Move (relative to recordings)", base);
      if (name && *name && !g_path_is_absolute(name)) {
         char *dest = g_build_filename(ui.directory, name, NULL);
         char *canonical = g_canonicalize_filename(dest, NULL);
         if (!in_root(canonical) || g_file_test(canonical, G_FILE_TEST_EXISTS))
            show_error("Destination must be inside Recordings and not already exist.");
         else if (g_rename(path, canonical)) show_error("Unable to move recording.");
         g_free(dest); g_free(canonical); refresh();
      } g_free(name); g_free(base);
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
   ui.store = gtk_list_store_new(N_COLS, G_TYPE_STRING, G_TYPE_STRING,
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
}
void gtk_recordings_shutdown(void) {
   event_off_token(ui.open_token);
   event_off_token(ui.finished_token);
   event_off_token(ui.playback_token);
   ui.open_token = ui.finished_token = ui.playback_token = NULL;
   rr_recordings_stop();
   audio_recording_play_stop();
   if (ui.panel) rr_floating_panel_destroy(ui.panel);
}
