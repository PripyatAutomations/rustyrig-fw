#include <assert.h>
#include <rrclient/gtk/gtk.core.c>
#include <rrclient/gtk/gtk.serverpick.c>

GtkWidget *chat_entry;
static gui_window_t picker;
static char connected_profile[512], edited_profile[512];
GtkWidget *gtk_show_server_editor(const char *profile) {
   snprintf(edited_profile, sizeof(edited_profile), "%s", profile);

   return NULL;
}

const char *server_name;
time_t now;

gui_window_t *gui_find_window(GtkWidget *widget, const char *name) {
   (void)widget;

   return name && !strcmp(name, "serverpick") && picker.gtk_win ? &picker : NULL;
}
gui_window_t *ui_new_window(GtkWidget *window, const char *name) {
   assert(!strcmp(name, "serverpick"));
   picker.gtk_win = window;

   return &picker;
}
bool connect_server(const char *name) {
   snprintf(connected_profile, sizeof(connected_profile), "%s", name);

   return false;
}
const char *get_server_property(const char *name, const char *property) {
   char key[1024];
   snprintf(key, sizeof(key), "server:%s.%s", name, property);

   return cfg_get(key);
}
static GtkWidget *find_tree(GtkWidget *widget) {
   if (GTK_IS_TREE_VIEW(widget)) {
      return widget;
   }

   if (GTK_IS_CONTAINER(widget)) {
      GList *children = gtk_container_get_children(GTK_CONTAINER(widget));

      for (GList *item = children ; item ; item = item->next) {
         GtkWidget *tree = find_tree(item->data);

         if (tree) {
            g_list_free(children);

            return tree;
         }
      }

      g_list_free(children);
   }

   return NULL;
}
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   main_notebook = gtk_notebook_new();
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), gtk_label_new("First"), NULL);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), gtk_label_new("Second"), NULL);
   gtk_widget_show_all(main_notebook);
   gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), 1);
   cfg = dict_new();
   dict_add(cfg, "server:libera.server.url", "irc://irc.libera.chat:6667/");
   dict_add(cfg, "server:libera.server.user", "testnick");
   dict_add(cfg, "server:libera.server.proxy", "socks5h://proxy.example:1080");
   dict_add(cfg, "server:irc.example.org.server.url", "ircs://irc.example.org");
   dict_add(cfg, "server:missing-url.server.user", "only-user");
   /* Exercise the real frontend callback: calling the core dispatcher here previously recursed until its stack overflowed. */
   frontend_gtk_show_server_chooser();
   assert(picker.gtk_win && GTK_IS_WINDOW(picker.gtk_win));
   GtkWidget *first = picker.gtk_win;
   frontend_gtk_show_server_chooser();
   assert(picker.gtk_win == first);
   GtkWidget *tree = find_tree(picker.gtk_win);
   assert(tree);
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
   assert(gtk_tree_model_iter_n_children(model, NULL) == 2);
   GtkTreeIter iter;
   assert(gtk_tree_model_get_iter_first(model, &iter));
   unsigned count = 0;
   do {
      char *name, *proxy;
      gtk_tree_model_get(model, &iter, 0, &name, -1);
      gtk_tree_model_get(model, &iter, 3, &proxy, -1);
      assert(!strcmp(name, "libera") || !strcmp(name, "irc.example.org"));

      if (!strcmp(name, "libera")) {
         assert(!strcmp(proxy, "socks5h://proxy.example:1080"));
      }
      count++;
      g_free(name);
      g_free(proxy);
   } while (gtk_tree_model_iter_next(model, &iter));
   assert(count == 2);
   assert(gtk_tree_model_get_iter_first(model, &iter));
   gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &iter);
   gchar *expected;
   gtk_tree_model_get(model, &iter, 0, &expected, -1);
   on_edit_clicked(NULL, tree);
   assert(!strcmp(edited_profile, expected) && !connected_profile[0]);
   char key[640];
   snprintf(key, sizeof(key), "server:%s.server.user", expected);
   dict_add(cfg, key, "updated-user");
   gtk_server_picker_refresh_profile(expected);
   gchar *updated;
   gtk_tree_model_get(model, &iter, 1, &updated, -1);
   assert(!strcmp(updated, "updated-user"));
   g_free(updated);
   do_connect_from_tree(GTK_TREE_VIEW(tree));
   assert(!strcmp(connected_profile, expected));
   g_free(expected);
   picker.gtk_win = NULL;
   frontend_gtk_show_server_chooser();
   GdkEventKey escape = {
      .type = GDK_KEY_PRESS, .keyval = GDK_KEY_Escape
   };
   assert(on_key(picker.gtk_win, &escape, NULL));
   assert(gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook)) == 1);
   gtk_widget_destroy(main_notebook);
   main_notebook = NULL;
   dict_free(cfg);
   puts("PASS: real GTK picker callback, existing-window reuse, URL profiles and selected profile connection");
}
