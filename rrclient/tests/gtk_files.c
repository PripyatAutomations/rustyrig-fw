// Exercise native transfer state and GTK tree against production share handlers.
#include <assert.h>
#include <sys/stat.h>
#include <rrclient/gtk/gtk.files.c>
#include <rrserver/files.h>
#include <rrclient/ui.h>
time_t now;
bool dying, restarting;
enum GuiMode ui_mode = UI_MODE_GTK;
rrconn_t *ws_conn;
static rrconn_t client;
static unsigned completions, pending_commits, failures;
static GMutex flush_lock;
static GCond flush_condition;
static bool flush_blocked, flush_entered;
/* Deterministically hold the worker while the main thread cancels or revokes access. The separate live-server test exercises actual filesystem fsync. */
int fsync(int fd) {
   g_mutex_lock(&flush_lock);
   flush_entered = true;
   g_cond_broadcast(&flush_condition);
   while (flush_blocked) {
      g_cond_wait(&flush_condition, &flush_lock);
   }
   g_mutex_unlock(&flush_lock);

   return 0;
}
static void block_flush(void) {
   g_mutex_lock(&flush_lock);
   flush_blocked = true;
   flush_entered = false;
   g_mutex_unlock(&flush_lock);
}
static void release_flush(void) {
   g_mutex_lock(&flush_lock);
   flush_blocked = false;
   g_cond_broadcast(&flush_condition);
   g_mutex_unlock(&flush_lock);
}

static char *editor_file;
static bool (*editor_save)(const char *, void *);
static void *editor_data;
static GDestroyNotify editor_destroy;
const char *rrclient_connection_name(const rrconn_t *c) {
   return c == &client ? "test" : NULL;
}
rrconn_t *rrclient_connection_find(const char *name) {
   return !strcmp(name, "test") ? &client : NULL;
}
const char *rrclient_connection_iter(unsigned i) {
   return i ? NULL : "test";
}
const char *rrclient_selected_server(void) {
   return "test";
}
bool ui_print(const char *room, const char *format, ...) {
   return false;
}
bool gui_file_editor_open_remote(const char *file, const char *title, bool (*save) (const char *, void *), void *data, GDestroyNotify destroy) {
   assert(!editor_file);
   editor_file = g_strdup(file);
   editor_save = save;
   editor_data = data;
   editor_destroy = destroy;

   return true;
}

struct delivery {
   rrconn_t *client;
   dict *message;
};
static gboolean deliver(gpointer data) {
   struct delivery *d = data;
   const char *command = dict_get(d->message, "file.cmd", "");
   bool reply = !strcmp(command, "result") || !strcmp(command, "entry") || !strcmp(command, "end") || !strcmp(command, "data");
   event_emit_dict(reply ? "ws.msg.file" : "file.request", d->client, d->message);
   dict_free(d->message);
   g_free(d);

   return G_SOURCE_REMOVE;
}
bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *message, int type) {
   assert(dest == &client);
   const char *command = dict_get(message, "file.cmd", "");

   if (!strcmp(command, "commit")) {
      pending_commits++;
   } else if (!strcmp(command, "result") && pending_commits) {
      pending_commits--;
   }
   struct delivery *d = g_new0(struct delivery, 1);
   d->client = dest;
   d->message = dict_clone(message);
   g_idle_add(deliver, d);

   return true;
}
static void completed(const char *event, const char *data, rrconn_t *c, void *context) {
   dict *d = json2dict(data);

   if (*dict_get(d, "file.error", "")) {
      failures++;
   }
   completions++;
   dict_free(d);
}
static void drain(void) {
   for (unsigned i = 0 ; i < 5000 ; i++) {
      while (g_main_context_pending(NULL)) {
         g_main_context_iteration(NULL, FALSE);
      }
      event_emit("server.poll", NULL, NULL);
      g_usleep(1000);

      if (!pending_commits && i >= 30) {
         break;
      }
   }
}
static void wait_flush(void) {
   for (unsigned i = 0 ; i < 5000 ; i++) {
      while (g_main_context_pending(NULL)) {
         g_main_context_iteration(NULL, FALSE);
      }
      event_emit("server.poll", NULL, NULL);
      g_mutex_lock(&flush_lock);
      bool entered = flush_entered;
      g_mutex_unlock(&flush_lock);

      if (entered) {
         return;
      }
      g_usleep(1000);
   }

   assert(!"Upload flush worker did not start");
}
static bool find(GtkTreeIter *parent, const char *path, GtkTreeIter *out) {
   GtkTreeIter iter;

   for (gboolean valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(browser.store), &iter, parent) ; valid ;
      valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(browser.store), &iter)) {
      char *value;
      gtk_tree_model_get(GTK_TREE_MODEL(browser.store), &iter, PATH, &value, -1);
      bool match = value && !strcmp(value, path);
      g_free(value);

      if (match) {
         *out = iter;

         return true;
      }

      if (find(&iter, path, out)) {
         return true;
      }
   }

   return false;
}
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   event_init();
   cfg = dict_new();
   char *work = g_dir_make_tmp("rr-gtk-files-XXXXXX", NULL);
   assert(work);
   dict_add(cfg, "share:test.path", work);
   client.authenticated = client.is_ws = true;
   client.user = &http_users[1];
   client.user->uid = 1;
   strcpy(client.user->privs, "files.read,files.write");
   ws_conn = &client;
   rrserver_files_register();
   rrclient_files_register();
   gtk_files_init();
   event_on("client.files.complete", completed, NULL);
   char *source = g_build_filename(work, "source.bin", NULL), *download = g_build_filename(work, "download.bin", NULL), *destination =
      g_build_filename(work, "remote.bin", NULL);
   unsigned char bytes[200000];

   for (unsigned i = 0 ; i < sizeof(bytes) ; i++) {
      bytes[i] = i % 251;
   }

   assert(g_file_set_contents(source, (const char *)bytes, sizeof(bytes), NULL));
   assert(rrclient_files_transfer(&client, "/test/remote.bin", source, true, false));
   drain();
   assert(completions == 1);
   assert(rrclient_files_transfer(&client, "/test/remote.bin", download, false, false));
   drain();
   assert(completions == 2);
   gchar *contents;
   gsize length;
   assert(g_file_get_contents(download, &contents, &length, NULL));
   assert(length == sizeof(bytes) && !memcmp(bytes, contents, length));
   g_free(contents);
   char *config = g_build_filename(work, "station.cfg", NULL);
   assert(g_file_set_contents(config, "setting=before\n", -1, NULL));
   event_emit("client.files.open", &client, NULL);
   drain();
   assert(browser.window);
   GtkTreeIter share;
   assert(find(NULL, "/test", &share));
   GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser.store), &share);
   gtk_tree_view_expand_row(GTK_TREE_VIEW(browser.tree), path, FALSE);
   gtk_tree_path_free(path);
   drain();
   GtkTreeIter selected;
   assert(find(NULL, "/test/station.cfg", &selected));
   path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser.store), &selected);
   gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser.tree)), path);
   gtk_tree_path_free(path);
   const char *screenshot = g_getenv("RR_FILES_SCREENSHOT");

   if (screenshot) {
      drain();
      GdkPixbuf *image = gdk_pixbuf_get_from_window(gtk_widget_get_window(browser.window), 0, 0, gtk_widget_get_allocated_width(browser.window),
         gtk_widget_get_allocated_height(browser.window));
      assert(image);
      assert(gdk_pixbuf_save(image, screenshot, "png", NULL, NULL));
      g_object_unref(image);
   }
   action(NULL, "edit");
   drain();
   assert(editor_file);
   assert(g_file_set_contents(editor_file, "setting=after\n", -1, NULL));
   assert(editor_save(editor_file, editor_data));
   drain();
   assert(completions == 4);
   assert(g_file_get_contents(config, &contents, &length, NULL));
   assert(!strcmp(contents, "setting=after\n"));
   g_free(contents);
   unlink(editor_file);
   g_free(editor_file);
   editor_destroy(editor_data);
   assert(!failures);
   block_flush();
   assert(rrclient_files_transfer(&client, "/test/cancelled.bin", source, true, false));
   wait_flush();
   event_emit("protocol.session.closed", &client, NULL);
   event_emit("disconnected", &client, NULL);
   pending_commits = 0;
   release_flush();
   drain();
   char *cancelled = g_build_filename(work, "cancelled.bin", NULL);
   assert(!g_file_test(cancelled, G_FILE_TEST_EXISTS));
   g_free(cancelled);
   assert(failures == 1);
   block_flush();
   assert(rrclient_files_transfer(&client, "/test/revoked.bin", source, true, false));
   wait_flush();
   strcpy(client.user->privs, "files.read");
   release_flush();
   drain();
   char *revoked = g_build_filename(work, "revoked.bin", NULL);
   assert(!g_file_test(revoked, G_FILE_TEST_EXISTS));
   g_free(revoked);
   assert(failures == 2);
   strcpy(client.user->privs, "files.read,files.write");
   gtk_files_shutdown();
   event_emit("protocol.session.closed", &client, NULL);
   rrserver_files_fini();
   event_shutdown();
   unlink(source);
   unlink(download);
   unlink(destination);
   unlink(config);
   rmdir(work);
   g_free(source);
   g_free(download);
   g_free(destination);
   g_free(config);
   g_free(work);
   dict_free(cfg);
   cfg = NULL;
   puts("PASS: native multi-chunk upload/download, expandable share tree, remote editor load/save, cancelled flushes and commit-time revocation");
}
