// rrclient/gtk/gtk.chat.links.c: Deferred chat link presentation
// This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.chat.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>

/* PARITY: rustyrig-www/js/webui.chat.js chat_room_links/ActivateRoom. One scan per main-loop batch avoids rescanning formatted markup fragments. Two shared
 * tags keep storage bounded by scrollback, rather than URL history. */
static gboolean chat_links_scan(gpointer data) {
   GtkTextBuffer *buffer = data;
   GtkTextMark *mark = g_object_get_data(G_OBJECT(buffer), "rr-link-dirty");
   g_object_set_data(G_OBJECT(buffer), "rr-link-pending", NULL);

   if (!mark) {
      return G_SOURCE_REMOVE;
   }
   GtkTextIter start, end;
   gtk_text_buffer_get_iter_at_mark(buffer, &start, mark);
   gtk_text_iter_set_line_offset(&start, 0);
   gtk_text_buffer_get_end_iter(buffer, &end);
   gtk_text_buffer_delete_mark(buffer, mark);
   g_object_set_data(G_OBJECT(buffer), "rr-link-dirty", NULL);
   GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
   GtkTextTag *room_tag = gtk_text_tag_table_lookup(table, "rr-room-link");
   GtkTextTag *url_tag = gtk_text_tag_table_lookup(table, "rr-url-link");
   gtk_text_buffer_remove_tag(buffer, room_tag, &start, &end);
   gtk_text_buffer_remove_tag(buffer, url_tag, &start, &end);
   char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
   GRegex *pattern = g_object_get_data(G_OBJECT(buffer), "rr-link-pattern");
   GMatchInfo *matches = NULL;
   g_regex_match(pattern, text, 0, &matches);
   const char *previous = text;
   GtkTextIter cursor = start;
   while (g_match_info_matches(matches)) {
      gint first, last;
      g_match_info_fetch_pos(matches, 0, &first, &last);
      bool url = text[first] != '#';
      while (last > first && strchr(url ? ".,;:!?)]}" : ".", text[last - 1])) {
         last--;
      }
      gtk_text_iter_forward_chars(&cursor, g_utf8_pointer_to_offset(previous, text + first));
      GtkTextIter finish = cursor;
      gtk_text_iter_forward_chars(&finish, g_utf8_pointer_to_offset(text + first, text + last));
      gtk_text_buffer_apply_tag(buffer, url ? url_tag : room_tag, &cursor, &finish);
      cursor = finish;
      previous = text + last;
      g_match_info_next(matches, NULL);
   }
   g_match_info_free(matches);
   g_free(text);

   return G_SOURCE_REMOVE;
}

static void chat_links_insert(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint length, gpointer user) {
   (void)text;
   (void)length;
   (void)user;

   if (!g_object_get_data(G_OBJECT(buffer), "rr-link-dirty")) {
      GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, NULL, location, TRUE);
      g_object_set_data(G_OBJECT(buffer), "rr-link-dirty", mark);
   }

   if (!g_object_get_data(G_OBJECT(buffer), "rr-link-pending")) {
      g_object_set_data(G_OBJECT(buffer), "rr-link-pending", GINT_TO_POINTER(1));
      g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, chat_links_scan, g_object_ref(buffer), g_object_unref);
   }
}

static char *chat_link_at(GtkWidget *view, double x, double y) {
   gint bx, by;
   GtkTextIter iter;
   gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_TEXT, x, y, &bx, &by);
   gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(view), &iter, bx, by);
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
   GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
   GtkTextTag *tag = gtk_text_tag_table_lookup(table, "rr-url-link");

   if (!gtk_text_iter_has_tag(&iter, tag)) {
      tag = gtk_text_tag_table_lookup(table, "rr-room-link");
   }

   if (!gtk_text_iter_has_tag(&iter, tag)) {
      return NULL;
   }
   GtkTextIter start = iter, end = iter;

   if (!gtk_text_iter_begins_tag(&start, tag)) {
      gtk_text_iter_backward_to_tag_toggle(&start, tag);
   }
   gtk_text_iter_forward_to_tag_toggle(&end, tag);

   return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void chat_open_external(GtkWidget *view, const char *link, guint32 time) {
   GError *error = NULL;
   gtk_show_uri_on_window(GTK_WINDOW(gtk_widget_get_toplevel(view)), link, time, &error);

   if (error) {
      Log(LOG_WARN, "ui.gtk", "Cannot open chat link: %s", error->message);
      g_error_free(error);
   }
}

static void chat_link_menu_action(GtkMenuItem *item, gpointer user_data) {
   const char *link = g_object_get_data(G_OBJECT(item), "rr-external-link");

   if (!link) {
      return;
   }

   if (GPOINTER_TO_INT(user_data)) {
      GtkWidget *view = g_object_get_data(G_OBJECT(item), "rr-link-view");

      if (view) {
         chat_open_external(view, link, gtk_get_current_event_time());
      }
   } else {
      GtkWidget *view = g_object_get_data(G_OBJECT(item), "rr-link-view");
      GtkClipboard *clipboard = view ? gtk_widget_get_clipboard(view, GDK_SELECTION_CLIPBOARD) : gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
      gtk_clipboard_set_text(clipboard, link, -1);
   }
}

static gboolean chat_link_context(GtkWidget *view, GdkEventButton *event) {
   char *link = chat_link_at(view, event->x, event->y);

   if (!link) {
      return FALSE;
   }

   /* Room links remain native navigation, not external browser URLs. */
   if (link[0] == '#') {
      g_free(link);

      return FALSE;
   }
   GtkWidget *menu = gtk_menu_new();
   const char *labels[] = {
      "Copy Link", "Open Link"
   };

   for (unsigned i = 0 ; i < 2 ; i++) {
      GtkWidget *item = gtk_menu_item_new_with_label(labels[i]);
      g_object_set_data_full(G_OBJECT(item), "rr-external-link", g_strdup(link), g_free);
      g_object_set_data(G_OBJECT(item), "rr-link-view", view);
      g_signal_connect(item, "activate", G_CALLBACK(chat_link_menu_action), GINT_TO_POINTER(i));
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   }

   gtk_widget_show_all(menu);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);
   g_free(link);

   return TRUE;
}

static gboolean chat_link_event(GtkWidget *view, GdkEvent *event, gpointer data) {
   (void)data;

   if (event->type == GDK_MOTION_NOTIFY) {
      char *link = chat_link_at(view, event->motion.x, event->motion.y);
      GdkWindow *window = gtk_text_view_get_window(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_TEXT);
      GdkCursor *cursor = gdk_cursor_new_from_name(gtk_widget_get_display(view), link ? "pointer" : "text");

      if (window) {
         gdk_window_set_cursor(window, cursor);
      }
      g_clear_object(&cursor);
      g_free(link);

      return FALSE;
   }

   if (event->type == GDK_BUTTON_PRESS && event->button.button == 3) {
      return chat_link_context(view, &event->button);
   }

   if (event->type == GDK_2BUTTON_PRESS && event->button.button == 1) {
      char *link = chat_link_at(view, event->button.x, event->button.y);

      if (!link || link[0] == '#') {
         g_free(link);

         return FALSE;
      }
      GtkClipboard *clipboard = gtk_widget_get_clipboard(view, GDK_SELECTION_CLIPBOARD);
      gtk_clipboard_set_text(clipboard, link, -1);
      g_free(link);

      return TRUE;
   }

   if (event->type != GDK_BUTTON_RELEASE || event->button.button != 1) {
      return FALSE;
   }

   if (gtk_text_buffer_get_has_selection(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)))) {
      return FALSE;
   }
   char *link = chat_link_at(view, event->button.x, event->button.y);

   if (!link) {
      return FALSE;
   }

   if (link[0] != '#') {
      if (!(event->button.state & GDK_CONTROL_MASK)) {
         g_free(link);

         return FALSE;
      }
      chat_open_external(view, link, event->button.time);
   } else {
      const char *server = g_object_get_data(G_OBJECT(view), "rr-server-name");

      if (!server || rrclient_connection_select(server)) {
         char command[] = "/join";
         char *args[] = {
            command, link
         };
         cmd_join(2, args);
      }
   }
   g_free(link);

   return TRUE;
}

void gtk_chat_links_attach(GtkWidget *view) {
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
   gtk_text_buffer_create_tag(buffer, "rr-url-link", "underline", PANGO_UNDERLINE_SINGLE, NULL);
   gtk_text_buffer_create_tag(buffer, "rr-room-link", "underline", PANGO_UNDERLINE_SINGLE, NULL);
   GRegex *pattern = g_regex_new("(?i:https?://)[^\\s<>\"']+|(?<![A-Za-z0-9_/#])#[A-Za-z0-9_][A-Za-z0-9_.+-]*", G_REGEX_OPTIMIZE, 0, NULL);
   g_object_set_data_full(G_OBJECT(buffer), "rr-link-pattern", pattern, (GDestroyNotify) g_regex_unref);
   g_signal_connect(buffer, "insert-text", G_CALLBACK(chat_links_insert), NULL);
   gtk_widget_add_events(view, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
   g_signal_connect(view, "motion-notify-event", G_CALLBACK(chat_link_event), NULL);
   g_signal_connect(view, "button-release-event", G_CALLBACK(chat_link_event), NULL);
   g_signal_connect(view, "button-press-event", G_CALLBACK(chat_link_event), NULL);
}
