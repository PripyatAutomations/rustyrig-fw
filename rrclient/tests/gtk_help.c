#include <assert.h>
#include <rrclient/gtk/gtk.core.c>
#include <rrclient/gtk/gtk.help.c>
#include <rrclient/cmd.h>
bool dying, restarting;
time_t now;
enum GuiMode ui_mode = UI_MODE_GTK;
GtkWidget *gtk_tabs_panel_restore_add(GtkNotebook *notebook, const char *title, GCallback callback, gpointer data) {
   (void)notebook;
   (void)title;
   (void)callback;
   (void)data;
   return NULL;
}
void gtk_tabs_panel_restore_remove(GtkWidget *tab) {
   (void)tab;
}
static bool staff;
client_cmd_t client_cmds[] = {
   {
      .cmd = "help", .desc = "Show help", .help_section = "Connection"
   },
   {
      .cmd = "admin", .desc = "Manage accounts", .help_section = "Administration", .admin = true
   },
   {
      0
   }
};
bool media_have_priv(const char *priv) {
   return staff;
}
static char *contents(void) {
   GtkWidget *view = NULL;
   GQueue queue = G_QUEUE_INIT;
   g_queue_push_tail(&queue, gtk_bin_get_child(GTK_BIN(main_window)));
   while (!g_queue_is_empty(&queue) && !view) {
      GtkWidget *widget = g_queue_pop_head(&queue);
      if (!g_strcmp0(gtk_widget_get_name(widget), "help-view")) {
         view = widget;
      } else if (GTK_IS_CONTAINER(widget)) {
         GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
         for (GList *item = children; item; item = item->next)
            g_queue_push_tail(&queue, item->data);
         g_list_free(children);
      }
   }
   assert(view);
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
   GtkTextIter start, end;
   gtk_text_buffer_get_bounds(buffer, &start, &end);

   return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   GtkWidget *overlay = gtk_overlay_new();
   main_notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(overlay), main_notebook);
   gtk_container_add(GTK_CONTAINER(main_window), overlay);
   gtk_widget_show_all(main_window);
   gtk_help_show();
   char *text = contents();
   assert(strstr(text, "/help") && strstr(text, "Connection") && !strstr(text, "/admin"));
   g_free(text);
   staff = true;
   gtk_help_show();
   text = contents();
   assert(strstr(text, "/admin") && strstr(text, "Administration"));
   g_free(text);
   gtk_help_shutdown();
   assert(gtk_widget_get_visible(main_window));
   gtk_widget_destroy(main_window);
   main_window = NULL;
   main_notebook = NULL;
   puts("PASS: GTK help popup shares categorized/filtered help, refreshes, reopens and shuts down");
}
