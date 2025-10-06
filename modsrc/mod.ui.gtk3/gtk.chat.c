//
// rrclient/gtk.chat.c: Chat stuff
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// XXX: Need to break this into pieces and wrap up our custom widgets, soo we can do
// XXX: nice things like pop-out (floating) VFOs
#include <librustyaxe/core.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
#include <librrprotocol/rrprotocol.h>
#include <rrclient/ui.help.h>
#include <rrclient/ui.speech.h>
#include "mod.ui.gtk3/gtk.core.h"

extern dict *cfg;		// main.c
extern time_t now;		// main.c
extern bool cfg_use_gtk;	// gtk.core.c

///////////////
static GPtrArray *input_history = NULL;
static int history_index = -1;
GtkWidget *chat_textview = NULL;
GtkWidget *chat_entry = NULL;
GtkTextBuffer *text_buffer = NULL;

// Scroll to the end of a GtkTextView
gboolean ui_scroll_to_end(gpointer data) {
   if (!data) {
      return FALSE;
   }

   GtkTextView *chat_textview = GTK_TEXT_VIEW(data);
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(chat_textview);
   GtkTextIter end;

   if (!data) {
      printf("ui_scroll_to_end: data == NULL\n");
      return FALSE;
   }
   gtk_text_buffer_get_end_iter(buffer, &end);
   gtk_text_view_scroll_to_iter(chat_textview, &end, 0.0, TRUE, 0.0, 1.0);

   // remove the idle handler after it runs
   return FALSE;
}

static void on_send_button_clicked(GtkButton *button, gpointer entry) {
   const gchar *msg = gtk_entry_get_text(GTK_ENTRY(chat_entry));

   if (!msg) {
      return;
   }

   parse_chat_input(button, entry);

   g_ptr_array_add(input_history, g_strdup(msg));
   history_index = input_history->len;
   gtk_entry_set_text(GTK_ENTRY(chat_entry), "");
   gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
}

// Here we support input history for the chat/control window entry input
static gboolean on_entry_key_press(GtkWidget *entry, GdkEventKey *event, gpointer user_data) {
   if (!event || !entry) {
      return FALSE;
   }

   if (!input_history || input_history->len == 0) {
      return FALSE;
   }

   if (event->keyval == GDK_KEY_Up) {
      if (history_index > 0) {
         history_index--;
      }
   } else if (event->keyval == GDK_KEY_Down) {
      if (history_index < input_history->len - 1) {
         history_index++;
      } else {
         gtk_entry_set_text(GTK_ENTRY(chat_entry), "");
         history_index = input_history->len;
         return TRUE;
      }
   } else {
      return FALSE;
   }

   const char *text = g_ptr_array_index(input_history, history_index);
   gtk_entry_set_text(GTK_ENTRY(chat_entry), text);
   gtk_editable_set_position(GTK_EDITABLE(chat_entry), -1);
   return TRUE;
}

GtkWidget *create_chat_box(void) {
//   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
//   gui_window_t *main_window_t = ui_new_window(main_window, "main");
   GtkWidget *chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   if (!chat_box) {
      return NULL;
   }

   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(scrolled, -1, 200);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

   // Chat view
   chat_textview = gtk_text_view_new();
   text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_textview));
   gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_textview), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chat_textview), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat_textview), GTK_WRAP_WORD_CHAR);
   gtk_container_add(GTK_CONTAINER(scrolled), chat_textview);
   gtk_box_pack_start(GTK_BOX(chat_box), scrolled, TRUE, TRUE, 0);

   // Chat INPUT
   chat_entry = gtk_entry_new();
   gtk_box_pack_start(GTK_BOX(chat_box), chat_entry, FALSE, FALSE, 0);
   g_signal_connect(chat_entry, "activate", G_CALLBACK(on_send_button_clicked), chat_entry);
   g_signal_connect(chat_entry, "key-press-event", G_CALLBACK(on_entry_key_press), NULL);

   // SEND the command/message
   GtkWidget *button = gtk_button_new_with_label("Send (enter)");
   gtk_box_pack_start(GTK_BOX(chat_box), button, FALSE, FALSE, 0);
   g_signal_connect(button, "clicked", G_CALLBACK(on_send_button_clicked), chat_entry);

   return chat_box;
}

bool chat_init(void) {
   input_history = g_ptr_array_new_with_free_func(g_free);
   return false;
}
