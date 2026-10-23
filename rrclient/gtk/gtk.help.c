#include <gtk/gtk.h>
#include <rrclient/cmd.help.h>
#include <rrclient/gtk/gtk.help.h>
#include <rrclient/gtk/gtk.core.h>
#include "gtk.floating-panel.h"

extern GtkWidget *main_window;
extern GtkWidget *main_notebook;
static RRFloatPanel *help_panel;
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

void gtk_help_show(void) {
   if (!help_panel) {
      GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;
      if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) return;
      help_panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook),
                                         "client-help", "RustyRig Help");
      if (!help_panel) return;
      gtk_widget_set_size_request(rr_floating_panel_body(help_panel), 800, 500);
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
      gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(help_panel)), scroll, TRUE, TRUE, 0);
   }
   /* Refresh content/privileges on every open, without writing help into chat. */
   gtk_text_buffer_set_text(help_buffer, "", 0);
   rrclient_help_render(help_line, help_buffer);
   GtkTextIter start, end;
   gtk_text_buffer_get_bounds(help_buffer, &start, &end);
   gtk_text_buffer_apply_tag_by_name(help_buffer, "help-font", &start, &end);
   rr_floating_panel_restore(help_panel);
   rr_floating_panel_show(help_panel);
}

void gtk_help_shutdown(void) {
   if (help_panel) {
      rr_floating_panel_destroy(help_panel);
      help_panel = NULL;
   }
   help_buffer = NULL;
}
