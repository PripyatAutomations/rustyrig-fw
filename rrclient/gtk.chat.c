//
// rrclient/gtk.chat.c: Chat stuff
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/cmd.h>
#include <rrclient/cmd.help.h>
#include <rrclient/ui.h>
#include <rrclient/ui.speech.h>
#include <rrclient/gtk.core.h>

extern dict *cfg;                // main.c
extern time_t now;               // main.c
extern GtkWidget *main_notebook,
                 *status_tab;
extern bool cfg_ui_gtk_vfo_on_top;

///////////////
static GPtrArray *input_history = NULL;
static unsigned int history_index = -1;
GtkWidget *chat_textview = NULL;
GtkWidget *chat_entry = NULL;
GtkTextBuffer *text_buffer = NULL;

// Scroll to the end of a GtkTextView
gboolean ui_scroll_to_end(gpointer data) {
   if (!data) {
      Log(LOG_CRAZY, "ui.gtk", "ui_scroll_to_end: data == NULL");
      return FALSE;
   }

   GtkTextView *chat_textview = GTK_TEXT_VIEW(data);
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(chat_textview);
   GtkTextIter end;

   gtk_text_buffer_get_end_iter(buffer, &end);
   gtk_text_view_scroll_to_iter(chat_textview, &end, 0.0, TRUE, 0.0, 1.0);

   // remove the idle handler after it runs
   return FALSE;
}

static void on_send_button_clicked(GtkButton *button, gpointer entry) {
   const gchar *msg = gtk_entry_get_text( GTK_ENTRY(chat_entry) );

   if (!msg) {
      return;
   }
   parse_chat_input_gtk(button, entry);

   g_ptr_array_add( input_history, g_strdup(msg) );
   history_index = input_history->len;
   gtk_entry_set_text(GTK_ENTRY(chat_entry), "");
   gtk_widget_grab_focus( GTK_WIDGET(chat_entry) );
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


static GtkWidget *chatbox_vfo_init(void) {
   GtkWidget *vfo = create_vfo_box();

   bool vfo_docked = cfg_get_bool("ui.gtk.vfo-docked", true);

   if (!vfo_docked) {
      gui_window_t *vfo_win = create_vfo_window(vfo, 'A');
      gtk_container_add(GTK_CONTAINER(vfo_win->gtk_win), vfo);
      return NULL;
   }

   return vfo;
}

GtkWidget *create_chat_box(void) {
   bool is_rig_window = true;		// is this a rig?
   GtkWidget *chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

   if (!chat_box) { // XXX: throw OOM warning
      return NULL;
   }

   GtkWidget *vfo = NULL;

   // cfg:ui.gtk.vfo-on-top
   if (cfg_ui_gtk_vfo_on_top) {
      if ((vfo = chatbox_vfo_init())) {
         gtk_box_pack_start(GTK_BOX(chat_box), vfo, FALSE, FALSE, 0);
      }
   }

   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(scrolled, -1, 200);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);


   // Chat view
   chat_textview = gtk_text_view_new();
   gtk_widget_override_font(chat_textview, gui_font_find("monospace"));
   text_buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW(chat_textview) );
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

   // !cfg:ui.gtk.vfo-on-top
   if (!cfg_ui_gtk_vfo_on_top) {
      vfo = chatbox_vfo_init();
      if ((vfo = chatbox_vfo_init())) {
         gtk_box_pack_start(GTK_BOX(chat_box), vfo, FALSE, FALSE, 0);
      }
   }

   return chat_box;
}

bool chat_init(void) {
   status_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *status_tab_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(status_tab_label), "(<u>4</u>) &amp;localrig");
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), status_tab, status_tab_label);
   input_history = g_ptr_array_new_with_free_func(g_free);

   GtkWidget *chat_box = create_chat_box();
   gtk_box_pack_start(GTK_BOX(status_tab), chat_box, TRUE, TRUE, 0);

   return false;
}
