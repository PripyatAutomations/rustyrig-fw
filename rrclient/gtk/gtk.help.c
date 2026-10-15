#include <gtk/gtk.h>
#include <rrclient/cmd.help.h>
#include <rrclient/gtk/gtk.help.h>
#include <rrclient/gtk/gtk.core.h>

extern GtkWidget *main_window;
static GtkWidget *help_window;
static GtkTextBuffer *help_buffer;

static void help_line(const char *line, void *data) {
   GtkTextBuffer *buffer = data;
   char *markup = gtk_colorize_string(line);
   GtkTextIter end;
   gtk_text_buffer_get_end_iter(buffer, &end);
   if (markup) {
      gtk_text_buffer_insert_markup(buffer, &end, markup, -1);
      g_free(markup);
   }
   gtk_text_buffer_insert(buffer, &end, "\n", 1);
}

static gboolean help_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
   (void)data;
   if (event->keyval == GDK_KEY_Escape) {
      gtk_widget_destroy(widget);
      return TRUE;
   }
   return FALSE;
}

void gtk_help_show(void) {
   if (!help_window) {
      help_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      g_object_add_weak_pointer(G_OBJECT(help_window), (gpointer *)&help_window);
      gtk_window_set_title(GTK_WINDOW(help_window), "RustyRig Help");
      gtk_window_set_default_size(GTK_WINDOW(help_window), 800, 600);
      if (main_window) {
         gtk_window_set_transient_for(GTK_WINDOW(help_window), GTK_WINDOW(main_window));
         gtk_window_set_destroy_with_parent(GTK_WINDOW(help_window), TRUE);
      }
      g_signal_connect(help_window, "key-press-event", G_CALLBACK(help_key), NULL);
      GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
      GtkWidget *view = gtk_text_view_new();
      gtk_widget_set_name(view, "help-view");
      gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
      gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
      gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
      gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 12);
      gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 12);
      help_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
      gtk_text_buffer_create_tag(help_buffer, "help-font", "family", "Monospace", NULL);
      gtk_container_add(GTK_CONTAINER(scroll), view);
      gtk_container_add(GTK_CONTAINER(help_window), scroll);
   }
   /* Refresh content/privileges on every open, without writing help into chat. */
   gtk_text_buffer_set_text(help_buffer, "", 0);
   rrclient_help_render(help_line, help_buffer);
   GtkTextIter start, end;
   gtk_text_buffer_get_bounds(help_buffer, &start, &end);
   gtk_text_buffer_apply_tag_by_name(help_buffer, "help-font", &start, &end);
   gtk_widget_show_all(help_window);
   gtk_window_present(GTK_WINDOW(help_window));
}

void gtk_help_shutdown(void) {
   if (help_window) gtk_widget_destroy(help_window);
   help_buffer = NULL;
}
