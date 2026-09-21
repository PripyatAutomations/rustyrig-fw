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

typedef struct {
   char room[128];
   GtkWidget *page;
   GtkWidget *view;
   GtkWidget *entry;
} GtkRoomTab;

static GHashTable *room_tabs = NULL;

static void gtk_chat_select_tab(GtkNotebook *notebook, GtkWidget *page,
   guint page_num, gpointer user_data) {
   (void)notebook;
   (void)page_num;
   (void)user_data;
   GtkRoomTab *tab = page ? g_object_get_data(G_OBJECT(page), "rr-room-tab") : NULL;
   if (tab) {
      chat_textview = tab->view;
      chat_entry = tab->entry;
      text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab->view));
   }
}

const char *gtk_chat_current_room(void) {
   if (!main_notebook) {
      return NULL;
   }
   gint page_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook));
   GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_notebook), page_num);
   GtkRoomTab *tab = page ? g_object_get_data(G_OBJECT(page), "rr-room-tab") : NULL;

   return tab && tab->room[0] ? tab->room : NULL;
}

// XXX: Move this to gtk.core.c
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
//////////

static bool gtk_chat_do_completion(GtkEntry *entry) {
   const char *line = gtk_entry_get_text(entry);
   int tui_cursor_pos = gtk_editable_get_position(GTK_EDITABLE(entry));

   if (!line || tui_cursor_pos <= 0) {
      return false;
   }

   // Find start of word before cursor
   tui_cursor_pos = (int)(g_utf8_offset_to_pointer(line, tui_cursor_pos) - line);
   int start = tui_cursor_pos;

   while (start > 0 && line[start - 1] != ' ') {
      start--;
   }

   int word_len = tui_cursor_pos - start;

   if (word_len < 0) {
      return false;
   }

   char word[TUI_INPUTLEN];

   if (word_len >= sizeof(word)) {
      return false;
   }

   memcpy(word, line + start, word_len);
   word[word_len] = '\0';

   char *prefix = g_strndup(line, tui_cursor_pos);
   char **matches = completion_collect(prefix, word);
   g_free(prefix);

   if (!matches || !matches[0]) {
      completion_free(matches);
      return false;
   }

   int nmatch = 0;
   while (matches[nmatch]) {
      nmatch++;
   }

   // Longest common prefix
   size_t plen = strlen(matches[0]);

   for (int i = 1; i < nmatch; i++) {
      size_t j = 0;

      while (j < plen &&
             matches[i][j] &&
             matches[0][j] == matches[i][j]) {
         j++;
      }

      plen = j;
   }

   // Single match or unambiguous prefix
   if (nmatch == 1 || plen > (size_t)word_len) {
      size_t replace_len = plen;
      bool add_space = (nmatch == 1);

      if (add_space) {
         replace_len++;
      }

      char *new_line = malloc(strlen(line) + replace_len - word_len + 1);

      if (!new_line) {
         completion_free(matches);
         return false;
      }

      memcpy(new_line, line, start);
      memcpy(new_line + start, matches[0], plen);

      if (add_space) {
         new_line[start + plen] = ' ';
      }

      strcpy(new_line + start + replace_len, line + tui_cursor_pos);

      gtk_entry_set_text(entry, new_line);
      gtk_editable_set_position(
         GTK_EDITABLE(entry),
         g_utf8_pointer_to_offset(new_line, new_line + start + replace_len)
      );

      free(new_line);
      completion_free(matches);
      return true;
   }

   // Ambiguous: print candidates into the chat window
   // ui.theme.completion is the color tag used to print candidates
   const char *completion_color = cfg_get("ui.theme.completion");

   if (!completion_color || !*completion_color) {
      completion_color = "bright-magenta";
   }

   // Config values may be braced ("{bright-magenta}", as they appear in
   // templates) or bare; strip braces so the tag resolves and no stray '}'
   // is printed (the TUI's theme_ansi_code() does the same). PARITY: tui.completion
   char color_tag[64];

   snprintf(color_tag, sizeof(color_tag), "%s", completion_color);
   size_t clen = strlen(color_tag);

   if (clen >= 2 && color_tag[0] == '{' && color_tag[clen - 1] == '}') {
      memmove(color_tag, color_tag + 1, clen - 2);
      color_tag[clen - 2] = '\0';
   }

   // Multi-column layout across a few lines (column-major, like the TUI)
   {
      int maxlen = 0;
      int nshown = nmatch > TUI_MAX_COMPLETIONS_SHOWN ? TUI_MAX_COMPLETIONS_SHOWN : nmatch;

      for (int i = 0; i < nshown; i++) {
         int l = (int)strlen(matches[i]);

         if (l > maxlen) {
            maxlen = l;
         }
      }

      // Each column is the entry plus two spaces of gutter; assume ~80 cols
      int cols = maxlen > 0 ? 80 / (maxlen + 2) : 1;

      if (cols < 1) {
         cols = 1;
      }

      int rows = (nshown + cols - 1) / cols;

      for (int r = 0; r < rows; r++) {
         char line[1024];
         size_t pos = 0;

         pos += snprintf(line + pos, sizeof(line) - pos, "  ");

         for (int c = 0; c < cols; c++) {
            int idx = c * rows + r;   // column-major so matches read down each column

            if (idx >= nshown) {
               break;
            }
            pos += snprintf(line + pos, sizeof(line) - pos, "%-*s  ", maxlen, matches[idx]);

            if (pos >= sizeof(line) - 1) {
               break;
            }
         }

         char colored[1100];

         snprintf(colored, sizeof(colored), "{%s}%s{reset}", color_tag, line);
         char *markup = gtk_colorize_string(colored);

         if (markup) {
            GtkTextIter iter;

            gtk_text_buffer_get_end_iter(text_buffer, &iter);
            gtk_text_buffer_insert_markup(text_buffer, &iter, markup, -1);
            g_free(markup);
         } else {
            gtk_text_buffer_insert_at_cursor(text_buffer, line, -1);
         }
         gtk_text_buffer_insert_at_cursor(text_buffer, "\n", -1);
      }
   }

   if (nmatch > TUI_MAX_COMPLETIONS_SHOWN) {
      char buf[64];

      snprintf(buf, sizeof(buf),
               "... and %d more\n",
               nmatch - TUI_MAX_COMPLETIONS_SHOWN);

      gtk_text_buffer_insert_at_cursor(text_buffer, buf, -1);
   }

   completion_free(matches);
   g_idle_add(ui_scroll_to_end, chat_textview);
   return false;
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
static gboolean on_chat_entry_keypress(GtkWidget *entry,
                                       GdkEventKey *event,
                                       gpointer user_data)
{
   if (!event || !entry) {
      return FALSE;
   }

   if (event->keyval == GDK_KEY_Tab) {
      gtk_chat_do_completion(GTK_ENTRY(entry));
      return TRUE;
   }

   if (event->keyval == GDK_KEY_Page_Up) {
      GtkAdjustment *adj =
         gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(chat_textview));

      gtk_adjustment_set_value(
         adj,
         gtk_adjustment_get_value(adj) -
         gtk_adjustment_get_page_increment(adj)
      );

      return TRUE;
   }

   if (event->keyval == GDK_KEY_Page_Down) {
      GtkAdjustment *adj =
         gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(chat_textview));

      gtk_adjustment_set_value(
         adj,
         gtk_adjustment_get_value(adj) +
         gtk_adjustment_get_page_increment(adj)
      );

      return TRUE;
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

static GtkWidget *create_chat_box_for_room(bool is_rig) {
   GtkWidget *chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

   if (!chat_box) { // XXX: throw OOM warning
      return NULL;
   }

   GtkWidget *vfo = NULL;

   // cfg:ui.gtk.vfo-on-top
   if (is_rig && cfg_ui_gtk_vfo_on_top) {
      if ((vfo = chatbox_vfo_init())) {
         gtk_box_pack_start(GTK_BOX(chat_box), vfo, FALSE, FALSE, 0);
      }
   }

   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(scrolled, -1, 200);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);


   // Chat view. Font (monospace) comes from the #chat-view CSS rule in [gtk-css]
   chat_textview = gtk_text_view_new();
   gtk_widget_set_name(chat_textview, "chat-view");
   text_buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW(chat_textview) );
   gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_textview), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chat_textview), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat_textview), GTK_WRAP_WORD_CHAR);
   gtk_container_add(GTK_CONTAINER(scrolled), chat_textview);
   gtk_box_pack_start(GTK_BOX(chat_box), scrolled, TRUE, TRUE, 0);

   // Chat INPUT
   chat_entry = gtk_entry_new();
   // Explicitly span the full width of the box; with fill=FALSE packing an
   // entry can end up right-aligned once other widgets (vfo box) are packed
   // around it.
   gtk_widget_set_halign(chat_entry, GTK_ALIGN_FILL);
   gtk_widget_set_hexpand(chat_entry, TRUE);
   // expand must be FALSE vertically, otherwise the box splits extra space
   // between the entry and the chat view, leaving a gap below the entry
   gtk_box_pack_start(GTK_BOX(chat_box), chat_entry, FALSE, FALSE, 0);
   g_signal_connect(chat_entry, "activate", G_CALLBACK(on_send_button_clicked), chat_entry);
   g_signal_connect(chat_entry, "key-press-event", G_CALLBACK(on_chat_entry_keypress), NULL);

   // SEND the command/message
   GtkWidget *button = gtk_button_new_with_label("Send (enter)");
   gtk_box_pack_start(GTK_BOX(chat_box), button, FALSE, FALSE, 0);
   g_signal_connect(button, "clicked", G_CALLBACK(on_send_button_clicked), chat_entry);

   // !cfg:ui.gtk.vfo-on-top
   if (is_rig && !cfg_ui_gtk_vfo_on_top) {
      if ((vfo = chatbox_vfo_init())) {
         gtk_box_pack_start(GTK_BOX(chat_box), vfo, FALSE, FALSE, 0);
      }
   }

   return chat_box;
}

GtkWidget *create_chat_box(void) {
   return create_chat_box_for_room(true);
}

void gtk_chat_room_add(const char *room) {
   if (!room || !*room || !main_notebook ||
       strcasecmp(room, "&localrig") == 0 ||
       strcasecmp(room, ws_authoritative_room()) == 0) {
      return;
   }
   if (!room_tabs) {
      room_tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   }
   if (g_hash_table_lookup(room_tabs, room)) {
      return;
   }
   GtkRoomTab *tab = g_new0(GtkRoomTab, 1);
   snprintf(tab->room, sizeof(tab->room), "%s", room);
   tab->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *box = create_chat_box_for_room(false);
   tab->view = chat_textview;
   tab->entry = chat_entry;
   gtk_box_pack_start(GTK_BOX(tab->page), box, TRUE, TRUE, 0);
   g_object_set_data(G_OBJECT(tab->page), "rr-room-tab", tab);
   GtkWidget *label = gtk_label_new(room);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), tab->page, label);
   g_hash_table_insert(room_tabs, g_strdup(room), tab);
   gtk_widget_show_all(tab->page);
   // Creating a side tab temporarily updates the legacy global widget
   // pointers. Restore them to whichever page the operator is viewing.
   gint active = gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook));
   GtkWidget *active_page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_notebook), active);
   gtk_chat_select_tab(GTK_NOTEBOOK(main_notebook), active_page, active, NULL);
}

void gtk_chat_room_remove(const char *room) {
   if (!room_tabs || !room) return;
   GtkRoomTab *tab = g_hash_table_lookup(room_tabs, room);
   if (!tab) return;
   gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), tab->page);
   if (page >= 0) gtk_notebook_remove_page(GTK_NOTEBOOK(main_notebook), page);
   g_hash_table_remove(room_tabs, room);
}

int next_chat_tab = 5;

bool chat_init(void) {
   room_tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   status_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *status_tab_label = gtk_label_new(NULL);
   char tab_desc[64];
   memset(tab_desc, 0, sizeof(tab_desc));
   snprintf(tab_desc, sizeof(tab_desc), "(<u>%d</u>) &amp;localrig", next_chat_tab);
   gtk_label_set_markup(GTK_LABEL(status_tab_label), tab_desc);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), status_tab, status_tab_label);
   input_history = g_ptr_array_new_with_free_func(g_free);

   GtkWidget *chat_box = create_chat_box();
   gtk_box_pack_start(GTK_BOX(status_tab), chat_box, TRUE, TRUE, 0);

   GtkRoomTab *rig_tab = g_new0(GtkRoomTab, 1);
   snprintf(rig_tab->room, sizeof(rig_tab->room), "%s", ws_authoritative_room());
   rig_tab->page = status_tab;
   rig_tab->view = chat_textview;
   rig_tab->entry = chat_entry;
   g_object_set_data(G_OBJECT(status_tab), "rr-room-tab", rig_tab);
   g_signal_connect(main_notebook, "switch-page", G_CALLBACK(gtk_chat_select_tab), NULL);

   return false;
}
