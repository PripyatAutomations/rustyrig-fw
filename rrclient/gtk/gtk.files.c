#include "gtk.tree-style.h"
// FTP-style share browser presentation. Part of RustyRig; MIT/GPL.

// Transfer state and wire requests belong to rrclient/files.c.
#include <gtk/gtk.h>
#include <unistd.h>
#include <string.h>
#include <rrclient/files.h>
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.file-editor.h>
#include "gtk.files.h"
#include "gtk.floating-panel.h"
extern GtkWidget *main_window, *main_notebook;
static RRFloatPanel *files_panel;

/* Theme-relative alternating row backgrounds, including sorted/filtered lists. */
static void rr_list_zebra_cell(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer unused) {
   (void)unused;
   GtkWidget *view = g_object_get_data(G_OBJECT(column), "rr-zebra-view");
   GtkTreePath *path = gtk_tree_model_get_path(model, iter);
   int *index = path ? gtk_tree_path_get_indices(path) : NULL;
   gboolean odd = index && (index[0] & 1);
   gboolean selected = view && path && gtk_tree_selection_path_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), path);

   if (odd && !selected && view) {
      GtkStyleContext *style = gtk_widget_get_style_context(view);
      GdkRGBA base, ink, shade;
      gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL, &base);
      gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &ink);
      shade.red = base.red * 0.915 + ink.red * 0.085;
      shade.green = base.green * 0.915 + ink.green * 0.085;
      shade.blue = base.blue * 0.915 + ink.blue * 0.085;
      shade.alpha = 1.0;
      g_object_set(cell, "cell-background-rgba", &shade, "cell-background-set", TRUE, NULL);
   } else {
      g_object_set(cell, "cell-background-set", FALSE, NULL);
   }

   if (path) {
      gtk_tree_path_free(path);
   }
}

enum {
   NAME, PATH, DIRECTORY, SIZE, READABLE, WRITABLE, LOADED, COLS
};
static struct {
   GtkWidget *window, *tree, *server, *status;
   GtkTreeStore *store;
   GHashTable *requests;
   GHashTable *site_requests;   /* discovery request ID -> home server profile */
   guint timer;
   gulong server_changed_handler;
   char *wanted_site;
} browser;
struct listing {
   GtkTreeRowReference *row;
   uint64_t started;
};
/* Only the displayed size is abbreviated; the model retains exact bytes. */
static void size_cell(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer unused) {
   guint64 bytes = 0;
   gboolean directory = FALSE;
   char *path = NULL;
   gtk_tree_model_get(model, iter, SIZE, &bytes, DIRECTORY, &directory, PATH, &path, -1);
   char size[48] = "";

   if (path && *path && !directory) {
      if (bytes >= 1000000000ULL) {
         g_snprintf(size, sizeof(size), "%.2f GB", bytes / 1000000000.0);
      } else if (bytes >= 1000000ULL) {
         g_snprintf(size, sizeof(size), "%.2f MB", bytes / 1000000.0);
      } else if (bytes >= 1000ULL) {
         g_snprintf(size, sizeof(size), "%.2f kB", bytes / 1000.0);
      } else {
         g_snprintf(size, sizeof(size), "%" G_GUINT64_FORMAT " B", bytes);
      }
   }
   g_object_set(cell, "text", size, NULL);
   rr_list_zebra_cell(column, cell, model, iter, unused);
   g_free(path);
}
static void listing_free(gpointer data) {
   struct listing *r = data;
   gtk_tree_row_reference_free(r->row);
   g_free(r);
}
static gboolean listing_expired(gpointer unused) {
   GHashTableIter iterator;
   gpointer key, value;
   g_hash_table_iter_init(&iterator, browser.requests);
   while (g_hash_table_iter_next(&iterator, &key, &value)) {
      struct listing *r = value;

      if ((uint64_t) mono_us() - r->started > 30000000) {
         GtkTreePath *path = gtk_tree_row_reference_get_path(r->row);
         GtkTreeIter iter;

         if (path && gtk_tree_model_get_iter(GTK_TREE_MODEL(browser.store), &iter, path)) {
            gtk_tree_store_set(browser.store, &iter, LOADED, FALSE, -1);
         }

         if (path) {
            gtk_tree_path_free(path);
         }
         gtk_label_set_text(GTK_LABEL(browser.status), "File listing timed out. Expand the folder again or Refresh to retry.");
         g_hash_table_iter_remove(&iterator);
      }
   }
   return G_SOURCE_CONTINUE;
}

static rr_event_token_t open_token, row_token, done_token;
struct remote_edit {
   char *profile, *path, *local;
};
static struct remote_edit *pending_edit;
static void edit_free(gpointer data) {
   struct remote_edit *e = data;

   if (e) {
      g_free(e->profile);
      g_free(e->path);
      g_free(e->local);
      g_free(e);
   }
}

/* Selector IDs are PROFILE (local site) or PROFILE|@NODEUUID (linked site). All file operations still go through the authenticated home connection. */
static rrconn_t *connection(void) {
   const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(browser.server));

   if (!id) {
      return NULL;
   }
   const char *sep = strchr(id, '|');
   char *profile = sep ? g_strndup(id, (gsize) (sep - id)) : g_strdup(id);
   rrconn_t *c = rrclient_connection_find(profile);
   g_free(profile);

   return c;
}
static const char *site_root(void) {
   const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(browser.server));
   const char *sep = id ? strchr(id, '|') : NULL;

   return sep ? sep + 1 : "/";
}
static void status(const char *text) {
   if (browser.window) {
      gtk_label_set_text(GTK_LABEL(browser.status), text);
   }
}
static char *selection(gboolean *directory, gboolean *readable, gboolean *writable, guint64 *size) {
   GtkTreeIter iter;
   GtkTreeModel *model;
   char *path = NULL;

   if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser.tree)), &model, &iter)) {
      gtk_tree_model_get(model, &iter, PATH, &path, DIRECTORY, directory, READABLE, readable, WRITABLE, writable, SIZE, size, -1);
   }

   return path;
}
static void placeholder(GtkTreeIter *parent) {
   GtkTreeIter child;
   gtk_tree_store_append(browser.store, &child, parent);
   gtk_tree_store_set(browser.store, &child, NAME, "", PATH, "", -1);
}

/* Retain the placeholder until a real child exists, so loading never collapses the expanded directory merely because its last temporary row was removed. */
static void remove_placeholders(GtkTreeIter *parent) {
   GtkTreeIter child;
   gboolean valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(browser.store), &child, parent);
   while (valid) {
      char *path = NULL;
      gtk_tree_model_get(GTK_TREE_MODEL(browser.store), &child, PATH, &path, -1);

      if (!path || !*path) {
         valid = gtk_tree_store_remove(browser.store, &child);
      } else {
         valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(browser.store), &child);
      }
      g_free(path);
   }
}
static void load(GtkTreeIter *parent) {
   char *path;
   gtk_tree_model_get(GTK_TREE_MODEL(browser.store), parent, PATH, &path, -1);
   char *id = rrclient_files_request(connection(), "list", path, NULL);
   g_free(path);

   if (!id) {
      gtk_tree_store_set(browser.store, parent, LOADED, FALSE, -1);
      status("Connect to a RustyRig server first.");

      return;
   }
   GtkTreeIter child;

   if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(browser.store), &child, parent)) {
      placeholder(parent);
   }
   GtkTreePath *treepath = gtk_tree_model_get_path(GTK_TREE_MODEL(browser.store), parent);
   struct listing *request = g_new0(struct listing, 1);
   request->row = gtk_tree_row_reference_new(GTK_TREE_MODEL(browser.store), treepath);
   request->started = mono_us();
   g_hash_table_insert(browser.requests, id, request);
   gtk_tree_path_free(treepath);
   gtk_tree_store_set(browser.store, parent, LOADED, TRUE, -1);
   status("Loading…");
}
static void expand(GtkTreeView *tree, GtkTreeIter *iter, GtkTreePath *path, gpointer data) {
   gboolean loaded;
   gtk_tree_model_get(GTK_TREE_MODEL(browser.store), iter, LOADED, &loaded, -1);

   if (!loaded) {
      load(iter);
   }
}

/* Double-click a folder to open it, or click its disclosure triangle. A stale or failed listing can be retried by activating the folder again. */
static void folder_activated(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *column, gpointer unused) {
   (void)column;
   (void)unused;
   GtkTreeIter iter;

   if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(browser.store), &iter, path)) {
      return;
   }
   gboolean directory = FALSE, loaded = FALSE;
   gtk_tree_model_get(GTK_TREE_MODEL(browser.store), &iter, DIRECTORY, &directory, LOADED, &loaded, -1);

   if (!directory) {
      return;
   }

   if (!loaded) {
      load(&iter);
   }

   if (!gtk_tree_view_row_expanded(tree, path)) {
      gtk_tree_view_expand_row(tree, path, FALSE);
   } else if (loaded) {
      gtk_tree_view_collapse_row(tree, path);
   }
}
static void refresh(void) {
   g_hash_table_remove_all(browser.requests);
   gtk_tree_store_clear(browser.store);
   GtkTreeIter root;
   gtk_tree_store_append(browser.store, &root, NULL);
   gtk_tree_store_set(browser.store, &root, NAME, "/", PATH, site_root(), DIRECTORY, TRUE, READABLE, TRUE, WRITABLE, FALSE, -1);
   /* A newly selected server must display its shares without requiring a manual expansion. The placeholder only keeps the root expandable while its initial
    * response is pending. */
   placeholder(&root);
   load(&root);
   GtkTreePath *path = gtk_tree_path_new_first();
   gtk_tree_view_expand_row(GTK_TREE_VIEW(browser.tree), path, FALSE);
   gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser.tree)), path);
   gtk_tree_path_free(path);
}

static void changed(GtkComboBox *box, gpointer data);
/* Rebuild both direct and linked site choices. Refresh must discover links established after the Files window was first opened. */
static void refresh_sites(void) {
   g_free(browser.wanted_site);
   browser.wanted_site = g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(browser.server)));
   const char *selected = browser.wanted_site;
   g_hash_table_remove_all(browser.site_requests);

   if (browser.server_changed_handler) {
      g_signal_handler_block(browser.server, browser.server_changed_handler);
   }
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(browser.server));
   const char *first = NULL;

   for (unsigned i = 0 ;; ++i) {
      const char *profile = rrclient_connection_iter(i);

      if (!profile) {
         break;
      }
      rrconn_t *candidate = rrclient_connection_find(profile);

      if (!candidate || !candidate->is_ws || !candidate->authenticated) {
         continue;
      }

      if (!first) {
         first = profile;
      }
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(browser.server), profile, profile);
      char *id = rrclient_files_request(candidate, "list", "/", NULL);

      if (id) {
         g_hash_table_insert(browser.site_requests, id, g_strdup(profile));
      }
   }

   if (selected) {
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(browser.server), selected);
   }

   if (gtk_combo_box_get_active(GTK_COMBO_BOX(browser.server)) < 0 && first) {
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(browser.server), first);
   }

   if (browser.server_changed_handler) {
      g_signal_handler_unblock(browser.server, browser.server_changed_handler);
   }
}
static void changed(GtkComboBox *box, gpointer data) {
   (void)box;
   (void)data;
   g_clear_pointer(&browser.wanted_site, g_free);
   refresh();
}
static gboolean browser_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
   (void)widget;
   (void)data;

   if (!(event->state & GDK_CONTROL_MASK) || (event->keyval != GDK_KEY_Tab && event->keyval != GDK_KEY_ISO_Left_Tab)) {
      return FALSE;
   }
   gint count = gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(browser.server)), NULL);

   if (count < 2) {
      return TRUE;
   }
   gint current = gtk_combo_box_get_active(GTK_COMBO_BOX(browser.server));
   gboolean reverse = (event->state & GDK_SHIFT_MASK) || event->keyval == GDK_KEY_ISO_Left_Tab;
   gint next = current < 0 ? 0 : (current + (reverse ? count - 1 : 1)) % count;
   gtk_combo_box_set_active(GTK_COMBO_BOX(browser.server), next);

   return TRUE;
}
static char *prompt(const char *title, const char *initial) {
   GtkWidget *dialog =
      gtk_dialog_new_with_buttons(title, GTK_WINDOW(main_window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "OK", GTK_RESPONSE_OK, NULL);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(entry), initial ? initial : "");
   gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry);
   gtk_widget_show_all(dialog);
   char *text = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK ? g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))) : NULL;
   gtk_widget_destroy(dialog);

   return text;
}
static bool confirm(const char *message) {
   GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", message);
   bool yes = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
   gtk_widget_destroy(d);

   return yes;
}
static bool edit_save(const char *local, void *data) {
   struct remote_edit *e = data;
   bool ok = rrclient_files_transfer(rrclient_connection_find(e->profile), e->path, local, true, true);
   status(ok ? "Saving remote file…" : "Could not start upload; reconnect or wait for the current transfer.");

   return ok;
}
static void complete(const char *event, const char *text, rrconn_t *c, void *data) {
   dict *d = json2dict(text);

   if (!d) {
      return;
   }
   const char *error = dict_get(d, "file.error", "");

   if (browser.window && c == connection()) {
      status(*error ? error : "Transfer complete");

      if (!*error) {
         refresh();
      }
   }

   if (pending_edit && !strcmp(pending_edit->local, dict_get(d, "file.local", ""))) {
      struct remote_edit *e = pending_edit;
      pending_edit = NULL;
      char *title = g_strdup_printf("%s:%s", e->profile, e->path);

      if (*error || !gui_file_editor_open_remote(e->local, title, edit_save, e, edit_free)) {
         status(*error ? error : "Close the existing configuration editor before editing another file.");
         unlink(e->local);
         edit_free(e);
      }
      g_free(title);
   }
   dict_free(d);
}

/* Prefer an authenticated direct connection over a redundant relayed route when the remote node advertises the same site name/profile. */
static gboolean directly_connected_site(const char *alias) {
   if (!alias || !*alias) {
      return FALSE;
   }

   for (unsigned i = 0 ;; i++) {
      const char *profile = rrclient_connection_iter(i);

      if (!profile) {
         break;
      }
      rrconn_t *direct = rrclient_connection_find(profile);

      if (direct && direct->is_ws && direct->authenticated && !g_ascii_strcasecmp(profile, alias)) {
         return TRUE;
      }
   }

   return FALSE;
}
static void row(const char *event, const char *text, rrconn_t *c, void *data) {
   if (!browser.window) {
      return;
   }
   dict *d = json2dict(text);

   if (!d) {
      return;
   }
   const char *id = dict_get(d, "request.id", ""), *cmd = dict_get(d, "file.cmd", "");
   /* Root listings also discover linked sites. Don't insert /@UUID entries into the file tree: they are selector choices, not share directories. */
   const char *home = g_hash_table_lookup(browser.site_requests, id);

   if (home) {
      if (!strcmp(cmd, "entry")) {
         const char *name = dict_get(d, "file.name", "");

         if (g_str_has_prefix(name, "@") && strlen(name) == 37) {
            char *key = g_strdup_printf("%s|/%s", home, name);
            const char *alias = dict_get(d, "file.site-name", name);

            /* Don't show a relayed route if that site already has a direct authenticated connection in the selector. */
            if (directly_connected_site(alias)) {
               g_free(key);
               dict_free(d);

               return;
            }
            char *label = g_strdup_printf("%s (%s)", alias, home);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(browser.server), key, label);

            if (browser.wanted_site && !strcmp(browser.wanted_site, key)) {
               gtk_combo_box_set_active_id(GTK_COMBO_BOX(browser.server), key);
               g_clear_pointer(&browser.wanted_site, g_free);
            }
            g_free(label);
            g_free(key);
         }
      } else if (!strcmp(cmd, "end") || !strcmp(cmd, "result")) {
         g_hash_table_remove(browser.site_requests, id);
      }
      dict_free(d);

      return;
   }

   if (c != connection()) {
      dict_free(d);

      return;
   }
   struct listing *request = g_hash_table_lookup(browser.requests, id);
   GtkTreeRowReference *ref = request ? request->row : NULL;

   if (ref && !strcmp(cmd, "entry")) {
      GtkTreePath *path = gtk_tree_row_reference_get_path(ref);
      GtkTreeIter parent, child;

      if (path && gtk_tree_model_get_iter(GTK_TREE_MODEL(browser.store), &parent, path)) {
         char *base;
         gtk_tree_model_get(GTK_TREE_MODEL(browser.store), &parent, PATH, &base, -1);
         const char *name = dict_get(d, "file.name", "");

         /* Linked site roots belong in the server selector, not this tree. */
         if (g_str_has_prefix(name, "@") && strlen(name) == 37) {
            gtk_tree_path_free(path);
            dict_free(d);

            return;
         }
         char *full = g_strdup_printf("%s%s%s", base, !strcmp(base, "/") ? "" : "/", name);
         g_free(base);
         gboolean directory = !strcmp(dict_get(d, "file.kind", ""), "directory");
         gtk_tree_store_append(browser.store, &child, &parent);
         gtk_tree_store_set(browser.store, &child, NAME, name, PATH, full, DIRECTORY, directory, SIZE, dict_get_ullong(d, "file.size", 0), READABLE,
            dict_get_bool(d, "file.readable", false), WRITABLE, dict_get_bool(d, "file.writable", false), -1);
         remove_placeholders(&parent);

         if (directory) {
            placeholder(&child);
         }
         g_free(full);
      }

      if (path) {
         gtk_tree_path_free(path);
      }
   } else if (!strcmp(cmd, "end") || !strcmp(cmd, "result")) {
      const char *error = dict_get(d, "file.error", "");
      status(*error ? error : "Ready");

      if (ref && (!strcmp(cmd, "result") || *error)) {
         GtkTreePath *failed = gtk_tree_row_reference_get_path(ref);
         GtkTreeIter parent;

         if (failed && gtk_tree_model_get_iter(GTK_TREE_MODEL(browser.store), &parent, failed)) {
            gtk_tree_store_set(browser.store, &parent, LOADED, FALSE, -1);
         }

         if (failed) {
            gtk_tree_path_free(failed);
         }
      }

      if (ref && !strcmp(cmd, "end")) {
         GtkTreePath *root = gtk_tree_row_reference_get_path(ref);

         if (root) {
            GtkTreeIter parent;

            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(browser.store), &parent, root)) {
               remove_placeholders(&parent);
            }
         }

         if (root && gtk_tree_path_get_depth(root) == 1) {
            gtk_tree_view_expand_row(GTK_TREE_VIEW(browser.tree), root, FALSE);
         }

         if (root) {
            gtk_tree_path_free(root);
         }
      }
      g_hash_table_remove(browser.requests, id);

      if (!ref && !*error && !strcmp(cmd, "result")) {
         refresh();
      }
   }
   dict_free(d);
}
static void action(GtkButton *button, gpointer data) {
   const char *op = data;

   if (!strcmp(op, "refresh")) {
      refresh_sites();
      refresh();

      return;
   }
   gboolean directory = FALSE, readable = FALSE, writable = FALSE;
   guint64 size = 0;
   char *path = selection(&directory, &readable, &writable, &size);

   if (!path || !*path) {
      g_free(path);

      return;
   }

   if (!strcmp(op, "download") || !strcmp(op, "edit")) {
      if (directory || !readable) {
         status("Select a readable file.");
         goto done;
      }
      char *local = NULL;

      if (!strcmp(op, "edit")) {
         if (!writable || size > 1024 * 1024 || pending_edit) {
            status("Edit requires a writable text file up to 1 MiB and an idle editor.");
            goto done;
         }
         int fd = g_file_open_tmp("rr-edit-XXXXXX", &local, NULL);

         if (fd < 0) {
            goto done;
         }
         close(fd);
         pending_edit = g_new0(struct remote_edit, 1);
         pending_edit->local = g_strdup(local);
         pending_edit->path = g_strdup(path);
         pending_edit->profile = g_strdup(rrclient_connection_name(connection()));
      } else {
         GtkWidget *dialog =
            gtk_file_chooser_dialog_new("Download", GTK_WINDOW(main_window), GTK_FILE_CHOOSER_ACTION_SAVE, "Cancel", GTK_RESPONSE_CANCEL, "Save",
            GTK_RESPONSE_ACCEPT, NULL);
         gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
         char *base = g_path_get_basename(path);
         gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), base);
         g_free(base);

         if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            local = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
         }
         gtk_widget_destroy(dialog);
      }

      if (local) {
         if (!rrclient_files_transfer(connection(), path, local, false, false)) {
            status("Cannot start download; check the connection and current transfer.");

            if (pending_edit) {
               unlink(pending_edit->local);
               edit_free(pending_edit);
               pending_edit = NULL;
            }
         } else {
            status("Downloading…");
         }
         g_free(local);
      }
   } else if (!strcmp(op, "upload")) {
      if (!directory || !writable) {
         status("Select a writable directory.");
         goto done;
      }
      GtkWidget *dialog = gtk_file_chooser_dialog_new("Upload", GTK_WINDOW(main_window), GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Open",
         GTK_RESPONSE_ACCEPT, NULL);

      if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
         char *local = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

         if (!local) {
            gtk_widget_destroy(dialog);
            goto done;
         }
         char *base = g_path_get_basename(local), *remote = g_strdup_printf("%s/%s", path, base);

         if (rrclient_files_transfer(connection(), remote, local, true, false)) {
            status("Uploading…");
         } else {
            status("Cannot start upload.");
         }
         g_free(local);
         g_free(base);
         g_free(remote);
      }
      gtk_widget_destroy(dialog);
   } else if (!strcmp(op, "delete")) {
      if (writable && strcmp(path, "/") && confirm("Delete the selected file or empty directory?")) {
         char *id = rrclient_files_request(connection(), "delete", path, NULL);
         g_free(id);
      }
   } else if (!strcmp(op, "move")) {
      if (!writable) {
         status("Select a writable file or directory.");
         goto done;
      }
      /* Rename is a move to another basename in the same virtual directory. Accept either a plain basename or a full virtual destination path. */
      char *value = prompt("Move / Rename — new name or destination path", path);

      if (value && *value && strcmp(value, path)) {
         char *parent = g_path_get_dirname(path);
         char *dest = value[0] == '/' ? g_strdup(value) : g_build_filename(parent, value, NULL);
         char *id = rrclient_files_request(connection(), "rename", path, dest);

         if (!id) {
            status("Unable to request move / rename.");
         }
         g_free(id);
         g_free(dest);
         g_free(parent);
      }
      g_free(value);
   } else if (!strcmp(op, "mkdir") && directory && writable) {
      char *name = prompt("New directory", "");

      if (name && *name) {
         char *dest = g_strdup_printf("%s/%s", path, name), *id = rrclient_files_request(connection(), "mkdir", dest, NULL);
         g_free(dest);
         g_free(id);
      }
      g_free(name);
   }
done: g_free(path);
}
static gint sort(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data) {
   char *an, *bn;
   gboolean ad, bd;
   gtk_tree_model_get(model, a, NAME, &an, DIRECTORY, &ad, -1);
   gtk_tree_model_get(model, b, NAME, &bn, DIRECTORY, &bd, -1);
   char *af = g_utf8_casefold(an, -1), *bf = g_utf8_casefold(bn, -1);
   char *ak = g_utf8_collate_key_for_filename(af, -1), *bk = g_utf8_collate_key_for_filename(bf, -1);
   g_free(af);
   g_free(bf);
   int order = ad != bd ? bd - ad : strcmp(ak, bk);
   g_free(an);
   g_free(bn);
   g_free(ak);
   g_free(bk);

   return order;
}
static void destroyed(GtkWidget *window, gpointer data) {
   files_panel = NULL;

   if (browser.timer) {
      g_source_remove(browser.timer);
   }
   g_hash_table_destroy(browser.requests);
   g_hash_table_destroy(browser.site_requests);
   g_free(browser.wanted_site);
   g_object_unref(browser.store);
   memset(&browser, 0, sizeof(browser));
}
static void files_close(gpointer data) {
   (void)data;

   if (files_panel) {
      rr_floating_panel_destroy(files_panel);
   }
}
static void open_browser(const char *event, const char *text, rrconn_t *c, void *data) {
   if (browser.window) {
      rr_floating_panel_restore(files_panel);
      rr_floating_panel_show(files_panel);

      return;
   }
   GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;

   if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) {
      return;
   }
   files_panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook), "remote-files", "Remote Files");

   if (!files_panel) {
      return;
   }
   rr_floating_panel_set_close_callback(files_panel, files_close, NULL);
   browser.window = rr_floating_panel_widget(files_panel);
   GtkWidget *box = rr_floating_panel_body(files_panel);
   gtk_widget_set_size_request(box, 850, 600);
   GtkWidget *server_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   GtkWidget *server_label = gtk_label_new("Server:");
   gtk_box_pack_start(GTK_BOX(box), server_row, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(server_row), server_label, FALSE, FALSE, 0);
   browser.server = gtk_combo_box_text_new();
   gtk_widget_set_hexpand(browser.server, TRUE);
   gtk_box_pack_start(GTK_BOX(server_row), browser.server, TRUE, TRUE, 0);
   gtk_widget_set_tooltip_text(browser.server, "Ctrl+Tab / Ctrl+Shift+Tab switches servers");
   const char *selected = c ? rrclient_connection_name(c) : rrclient_selected_server(), *first = NULL;

   for (unsigned i = 0 ;; i++) {
      const char *profile = rrclient_connection_iter(i);

      if (!profile) {
         break;
      }
      rrconn_t *candidate = rrclient_connection_find(profile);

      if (candidate && candidate->is_ws && candidate->authenticated) {
         if (!first) {
            first = profile;
         }
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(browser.server), profile, profile);
      }
   }

   if (selected) {
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(browser.server), selected);
   }

   if (gtk_combo_box_get_active(GTK_COMBO_BOX(browser.server)) < 0 && first) {
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(browser.server), first);
   }
   browser.store = gtk_tree_store_new(COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_UINT64, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
   gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(browser.store), NAME, sort, NULL, NULL);
   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(browser.store), NAME, GTK_SORT_ASCENDING);
   browser.tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(browser.store));
   rr_gtk_tree_style(browser.tree);
   const char *labels[] = {
      "Name", "Path", "Directory", "Bytes", "Read", "Write"
   };

   for (int col = 0 ; col < 6 ; col++) {
      /* Readability remains in the model for authorization and actions, but displaying a Read checkbox adds no useful information. */
      if (col == PATH || col == DIRECTORY || col == READABLE) {
         continue;
      }
      GtkCellRenderer *cell = col == READABLE || col == WRITABLE ? gtk_cell_renderer_toggle_new() : gtk_cell_renderer_text_new();

      if (col == READABLE || col == WRITABLE) {
         g_object_set(cell, "activatable", FALSE, NULL);
      }
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(labels[col], cell, col == READABLE
         || col == WRITABLE ? "active" : "text", col, NULL);
      g_object_set_data(G_OBJECT(column), "rr-zebra-view", browser.tree);

      gtk_tree_view_column_set_cell_data_func(column, cell, col == SIZE ? size_cell : rr_list_zebra_cell, NULL, NULL);
      gtk_tree_view_column_set_resizable(column, TRUE);

      if (col == NAME) {
         gtk_tree_view_column_set_expand(column, TRUE);
         gtk_tree_view_column_set_min_width(column, 260);
      } else if (col == SIZE) {
         gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
         gtk_tree_view_column_set_fixed_width(column, 95);
         g_object_set(cell, "xalign", 1.0f, NULL);
      } else {
         gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
         gtk_tree_view_column_set_fixed_width(column, 54);
         gtk_tree_view_column_set_min_width(column, 48);
      }

      gtk_tree_view_column_set_sort_column_id(column, col);
      gtk_tree_view_append_column(GTK_TREE_VIEW(browser.tree), column);
   }

   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(scroll), browser.tree);
   gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
   GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
   const char *names[] = {
      "Refresh", "Upload", "Download", "Move / Rename", "Edit", "New Dir", "Delete"
   }, *ops[] = {
      "refresh", "upload", "download", "move", "edit", "mkdir", "delete"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(names) ; i++) {
      /* Four visual groups: refresh/transfers | editing | moving/creation |
       * destructive action. Keep the gaps stretch-free. */
      if (i == 3 || i == 5 || i == 6) {
         GtkWidget *gap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
         gtk_widget_set_size_request(gap, 18, -1);
         gtk_box_pack_start(GTK_BOX(buttons), gap, FALSE, FALSE, 0);
      }
      GtkWidget *button = gtk_button_new_with_label(names[i]);
      gtk_box_pack_start(GTK_BOX(buttons), button, FALSE, FALSE, 0);
      g_signal_connect(button, "clicked", G_CALLBACK(action), (gpointer) ops[i]);
   }

   browser.status = gtk_label_new("Ready");
   gtk_box_pack_start(GTK_BOX(box), browser.status, FALSE, FALSE, 0);
   browser.requests = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, listing_free);
   browser.site_requests = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   browser.timer = g_timeout_add_seconds(1, listing_expired, NULL);
   g_signal_connect(browser.window, "destroy", G_CALLBACK(destroyed), NULL);
   g_signal_connect(browser.window, "key-press-event", G_CALLBACK(browser_key_press), NULL);
   g_signal_connect(browser.tree, "row-expanded", G_CALLBACK(expand), NULL);
   g_signal_connect(browser.tree, "row-activated", G_CALLBACK(folder_activated), NULL);
   browser.server_changed_handler = g_signal_connect(browser.server, "changed", G_CALLBACK(changed), NULL);
   rr_floating_panel_show(files_panel);
   refresh();
   refresh_sites();
}
void gtk_files_init(void) {
   open_token = event_on_token("client.files.open", open_browser, NULL);
   row_token = event_on_token("client.files.row", row, NULL);
   done_token = event_on_token("client.files.complete", complete, NULL);
}
void gtk_files_shutdown(void) {
   event_off_token(open_token);
   event_off_token(row_token);
   event_off_token(done_token);

   if (browser.window) {
      files_close(NULL);
   }

   if (pending_edit) {
      unlink(pending_edit->local);
      edit_free(pending_edit);
      pending_edit = NULL;
   }
}
