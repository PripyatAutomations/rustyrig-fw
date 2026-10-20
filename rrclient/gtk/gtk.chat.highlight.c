// Chat mention prefix styling. Part of rustyrig-fw; MIT without mongoose, GPL with it.
#include <gtk/gtk.h>
#include <stdbool.h>
#include <string.h>
#include <rrclient/gtk/gtk.chat.h>

/* GtkTextView spans use tags; resolve their color from the configurable CSS palette. */
static void highlight_style(GtkWidget *view, gpointer unused) {
   (void)unused;
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
   GtkTextTag *tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "rr-highlight-nick");
   GdkRGBA color;

   if (tag && gtk_style_context_lookup_color(gtk_widget_get_style_context(view), "rr_chat_highlight_nick", &color)) {
      g_object_set(tag, "foreground-rgba", &color, NULL);
   }
}

/* Style only the sender token, after IRC markup has become plain buffer text. */
void gtk_chat_highlight_prefix(GtkWidget *view, int start_offset) {
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
   bool highlight = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(buffer), "rr-highlight-prefix"));
   g_object_set_data(G_OBJECT(buffer), "rr-highlight-prefix", NULL);

   if (!highlight) {
      return;
   }
   GtkTextIter start, end;
   gtk_text_buffer_get_iter_at_offset(buffer, &start, start_offset);
   gtk_text_buffer_get_end_iter(buffer, &end);
   char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
   char *prefix = text;

   if (*prefix == '[') {
      char *timestamp_end = strchr(prefix, ']');

      if (timestamp_end) {
         prefix = timestamp_end + 1;
      }
   }
   while (*prefix == ' ') {
      prefix++;
   }
   char *finish = NULL;

   if (*prefix == '<') {
      finish = strchr(++prefix, '>');
   } else if (*prefix == '*' && prefix[1] == ' ') {
      prefix += 2;
      finish = strchr(prefix, ' ');
   } else if (*prefix == '*' || *prefix == '-') {
      char delimiter = *prefix++;
      finish = strchr(prefix, delimiter);
   }

   if (finish && finish > prefix) {
      GtkTextTag *tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "rr-highlight-nick");

      if (!tag) {
         tag = gtk_text_buffer_create_tag(buffer, "rr-highlight-nick", NULL);
      }
      /* New IRC formatting tags may have higher priority; the highlight must win. */
      gtk_text_tag_set_priority(tag, gtk_text_tag_table_get_size(gtk_text_buffer_get_tag_table(buffer)) - 1);
      highlight_style(view, NULL);

      if (!g_object_get_data(G_OBJECT(view), "rr-highlight-style")) {
         g_object_set_data(G_OBJECT(view), "rr-highlight-style", GINT_TO_POINTER(1));
         g_signal_connect(view, "style-updated", G_CALLBACK(highlight_style), NULL);
      }
      gtk_text_buffer_get_iter_at_offset(buffer, &start, start_offset + g_utf8_pointer_to_offset(text, prefix));
      gtk_text_buffer_get_iter_at_offset(buffer, &end, start_offset + g_utf8_pointer_to_offset(text, finish));
      gtk_text_buffer_apply_tag(buffer, tag, &start, &end);
   }
   g_free(text);
}
