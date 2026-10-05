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
#include <rrclient/media.h>
#include <rrclient/rooms.h>
#include <rrclient/vfo.h>
#include <rrclient/ui.speech.h>
#include <rrclient/gtk.core.h>
#include <rrclient/userlist.h>

extern dict *cfg;                // main.c
extern time_t now;               // main.c
extern GtkWidget *main_notebook,
                 *status_tab;
extern bool cfg_ui_gtk_vfo_on_top;

///////////////
static GPtrArray *input_history = NULL;
typedef struct {
   GPtrArray *local;
   int index;
   char *draft;
   bool shared;
} GtkInputHistory;

static void input_history_free(gpointer data) {
   GtkInputHistory *history = data;
   if (history->local) g_ptr_array_unref(history->local);
   g_free(history->draft);
   g_free(history);
}

static GtkInputHistory *entry_history(GtkWidget *entry, GPtrArray **lines) {
   GtkInputHistory *history = g_object_get_data(G_OBJECT(entry), "rr-input-history");
   bool shared = cfg_get_bool("ui.shared-input-history", true);
   if (!history) {
      history = g_new0(GtkInputHistory, 1);
      history->index = -1;
      g_object_set_data_full(G_OBJECT(entry), "rr-input-history", history, input_history_free);
   }
   if (history->shared != shared) {
      history->shared = shared;
      history->index = -1;
      g_clear_pointer(&history->draft, g_free);
   }
   if (!shared && !history->local) history->local = g_ptr_array_new_with_free_func(g_free);
   *lines = shared ? input_history : history->local;
   return history;
}
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
static GtkRoomTab *rig_room_tab = NULL;
int next_chat_tab = 5;

static gboolean gtk_chat_set_userlist_width(gpointer data) {
   GtkWidget *paned = GTK_WIDGET(data);
   if (!paned || !GTK_IS_PANED(paned)) return G_SOURCE_REMOVE;
   int width = cfg_get_int("ui.userlist-width", 220);
   int total = gtk_widget_get_allocated_width(paned);
   if (width < 120) width = 120;
   if (total > width) gtk_paned_set_position(GTK_PANED(paned), total - width);
   return G_SOURCE_REMOVE;
}

static GtkWidget *room_vfo_box;
static void gtk_chat_update_vfo_controls(GtkRoomTab *tab);

static void gtk_chat_select_tab(GtkNotebook *notebook, GtkWidget *page,
   guint page_num, gpointer user_data) {
   (void)notebook;
   (void)page_num;
   (void)user_data;
   GtkRoomTab *tab = page ? g_object_get_data(G_OBJECT(page), "rr-room-tab") : NULL;
   if (tab && tab->view && GTK_IS_TEXT_VIEW(tab->view)) {
      chat_textview = tab->view;
      chat_entry = tab->entry;
      text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab->view));
      /* The shared user list follows the selected room, including when it
       * is detached in its own window. */
      userlist_redraw_gtk();
      rrclient_media_room_selected(tab->room);
      gtk_chat_update_vfo_controls(tab);
   }
}

bool gtk_chat_room_widgets(const char *room, GtkTextBuffer **buffer, GtkWidget **view) {
   GtkRoomTab *tab = NULL;
   if (!room || !*room) tab = rig_room_tab;
   else if (rig_room_tab && !strcasecmp(rig_room_tab->room, room)) tab = rig_room_tab;
   else if (room_tabs) tab = g_hash_table_lookup(room_tabs, room);
   if (!tab) return false;
   if (!tab->view || !GTK_IS_TEXT_VIEW(tab->view)) return false;
   if (buffer) *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab->view));
   if (view) *view = tab->view;
   return true;
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
   gtk_widget_grab_focus(GTK_WIDGET(entry));
   parse_chat_input_gtk(button, entry);
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

   if (event->keyval != GDK_KEY_Up && event->keyval != GDK_KEY_Down) return FALSE;
   if (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK)) return FALSE;
   GPtrArray *lines;
   GtkInputHistory *history = entry_history(entry, &lines);
   if (!lines || !lines->len) return FALSE;

   if (event->keyval == GDK_KEY_Up) {
      if (history->index < 0) {
         g_free(history->draft);
         history->draft = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
         history->index = (int)lines->len - 1;
      } else if (history->index > 0) {
         history->index--;
      }
   } else {
      if (history->index < 0) return TRUE;
      if (++history->index >= (int)lines->len) history->index = -1;
   }
   const char *text = history->index < 0 ? (history->draft ? history->draft : "") :
      g_ptr_array_index(lines, history->index);
   gtk_entry_set_text(GTK_ENTRY(entry), text);
   gtk_editable_set_position(GTK_EDITABLE(entry), -1);
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

// A single control box follows the selected rig room.  The site lobby and
// query tabs never own radio widgets; creating one per rig would overwrite
// the GTK module's shared frequency/PTT widget pointers.
static void gtk_chat_update_vfo_controls(GtkRoomTab *tab) {
   bool has_vfos = tab && tab->room[0] && rrclient_room_tx_control(tab->room);
   if (!has_vfos) {
      if (room_vfo_box && cfg_get_bool("ui.gtk.vfo-docked", true)) gtk_widget_hide(room_vfo_box);
      return;
   }
   GtkWidget *box = g_object_get_data(G_OBJECT(tab->page), "rr-chat-box");
   if (!box) return;
   if (!room_vfo_box) {
      room_vfo_box = chatbox_vfo_init();
      if (!room_vfo_box) return;
      g_object_add_weak_pointer(G_OBJECT(room_vfo_box), (gpointer *)&room_vfo_box);
   }
   if (!cfg_get_bool("ui.gtk.vfo-docked", true)) return;
   GtkWidget *parent = gtk_widget_get_parent(room_vfo_box);
   if (parent != box) {
      g_object_ref_sink(room_vfo_box);
      if (parent) gtk_container_remove(GTK_CONTAINER(parent), room_vfo_box);
      gtk_box_pack_start(GTK_BOX(box), room_vfo_box, FALSE, FALSE, 0);
      if (cfg_ui_gtk_vfo_on_top) gtk_box_reorder_child(GTK_BOX(box), room_vfo_box, 0);
      g_object_unref(room_vfo_box);
   }
   gtk_widget_show_all(room_vfo_box);
   vfo_update_ui();
}

void gtk_chat_room_vfos_changed(const char *room) {
   userlist_room_vfos_changed(room);
   gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook));
   GtkWidget *widget = gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_notebook), page);
   GtkRoomTab *tab = widget ? g_object_get_data(G_OBJECT(widget), "rr-room-tab") : NULL;
   gtk_chat_update_vfo_controls(tab);
}

static GtkWidget *create_chat_box_for_room(bool is_rig, const char *room, bool is_query) {
   GtkWidget *chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

   if (!chat_box) { // XXX: throw OOM warning
      return NULL;
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

   /* Keep room user lists beside their chat. Private query tabs stay
    * chat-only; GtkPaned gives room tabs a draggable divider, and the
    * authoritative room additionally supports the detachable list window. */
   if (is_rig || (!is_query && room && *room)) {
      GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
      gtk_box_pack_start(GTK_BOX(chat_box), paned, TRUE, TRUE, 0);
      gtk_paned_pack1(GTK_PANED(paned), scrolled, TRUE, FALSE);
      g_idle_add(gtk_chat_set_userlist_width, paned);
      if (is_rig) userlist_dock_into(GTK_PANED(paned));
      else userlist_dock_room_into(GTK_PANED(paned), room);
   } else {
      gtk_box_pack_start(GTK_BOX(chat_box), scrolled, TRUE, TRUE, 0);
   }

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
   /* GtkEntry handles Return/space itself, so register the global shortcut
    * handler directly on each chat input as well as the main window. */
   gui_hotkey_register(chat_entry);

   // SEND the command/message
   GtkWidget *button = gtk_button_new_with_label("Send (enter)");
   gtk_box_pack_start(GTK_BOX(chat_box), button, FALSE, FALSE, 0);
   g_signal_connect(button, "clicked", G_CALLBACK(on_send_button_clicked), chat_entry);

   return chat_box;
}

GtkWidget *create_chat_box(void) {
   return create_chat_box_for_room(true, ws_authoritative_room(), false);
}

static void gtk_chat_tab_add(const char *room, bool is_query) {
   if (!room || !*room || !main_notebook ||
       (!is_query && strcasecmp(room, ws_authoritative_room()) == 0)) {
      return;
   }
   if (!room_tabs) {
      room_tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   }
   GtkRoomTab *existing = g_hash_table_lookup(room_tabs, room);
   if (existing) {
      gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), existing->page);
      if (page >= 0) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), page);
      }
      if (existing->entry && GTK_IS_WIDGET(existing->entry))
         gtk_widget_grab_focus(existing->entry);
      return;
   }
   GtkRoomTab *tab = g_new0(GtkRoomTab, 1);
   snprintf(tab->room, sizeof(tab->room), "%s", room);
   tab->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *box = create_chat_box_for_room(false, room, is_query);
   g_object_set_data(G_OBJECT(tab->page), "rr-chat-box", box);
   tab->view = chat_textview;
   tab->entry = chat_entry;
   gtk_box_pack_start(GTK_BOX(tab->page), box, TRUE, TRUE, 0);
   g_object_set_data(G_OBJECT(tab->page), "rr-room-tab", tab);
   GtkWidget *label = gtk_label_new(room);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), tab->page, label);
   g_hash_table_insert(room_tabs, g_strdup(room), tab);
   gtk_widget_show_all(tab->page);
   // A successful JOIN changes the active conversation. Select the new tab
   // so the join notice and subsequent input are directed to this room.
   gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), tab->page);
   if (page >= 0) {
      gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), page);
   }
   if (tab->entry && GTK_IS_WIDGET(tab->entry))
      gtk_widget_grab_focus(tab->entry);
}

void gtk_chat_room_add(const char *room) {
   gtk_chat_tab_add(room, false);
}

void gtk_chat_query_add(const char *user) {
   gtk_chat_tab_add(user, true);
}

void gtk_chat_show_status(void) {
   if (!main_notebook || !status_tab) return;
   gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), status_tab);
   if (page >= 0) {
      gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), page);
   }
   if (rig_room_tab && rig_room_tab->entry && GTK_IS_WIDGET(rig_room_tab->entry)) {
      gtk_widget_grab_focus(rig_room_tab->entry);
   }
}

void gtk_chat_room_remove(const char *room) {
   if (!room_tabs || !room) return;
   GtkRoomTab *tab = g_hash_table_lookup(room_tabs, room);
   if (!tab) return;
   gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), tab->page);
   userlist_remove_room_view(room);
   if (page >= 0) gtk_notebook_remove_page(GTK_NOTEBOOK(main_notebook), page);
   g_hash_table_remove(room_tabs, room);
}

void gtk_chat_room_set_topic(const char *room, const char *topic) {
   if (!room || !*room) return;
   GtkTextBuffer *buffer = NULL;
   GtkWidget *view = NULL;
   if (!gtk_chat_room_widgets(room, &buffer, &view) || !buffer) return;
   char line[640];
   snprintf(line, sizeof(line), "*** Topic for %s: %s", room,
      (topic && *topic) ? topic : "(none)");
   GtkTextIter end;
   gtk_text_buffer_get_end_iter(buffer, &end);
   gtk_text_buffer_insert(buffer, &end, line, -1);
   gtk_text_buffer_insert(buffer, &end, "\n", 1);
}

void gtk_chat_set_authoritative_room(const char *room) {
   if (!room || !*room || !rig_room_tab || !rig_room_tab->page) {
      return;
   }
   snprintf(rig_room_tab->room, sizeof(rig_room_tab->room), "%s", room);
   /* The status page is deliberately created before authentication, but it
    * must not contain a rig room, VFO controls, or a user list until the
    * server tells us which authoritative room this connection joined. */
   if (!g_object_get_data(G_OBJECT(rig_room_tab->page), "rr-rig-room-built")) {
      GList *children = gtk_container_get_children(GTK_CONTAINER(rig_room_tab->page));
      for (GList *it = children; it; it = it->next) {
         gtk_widget_destroy(GTK_WIDGET(it->data));
      }
      g_list_free(children);

      GtkWidget *chat_box = create_chat_box_for_room(true, room, false);
      if (!chat_box) {
         return;
      }

      gtk_box_pack_start(GTK_BOX(rig_room_tab->page), chat_box, TRUE, TRUE, 0);
      g_object_set_data(G_OBJECT(rig_room_tab->page), "rr-chat-box", chat_box);
      rig_room_tab->view = chat_textview;
      rig_room_tab->entry = chat_entry;
      g_object_set_data(G_OBJECT(rig_room_tab->page), "rr-rig-room-built", GINT_TO_POINTER(1));
      gtk_widget_show_all(rig_room_tab->page);
   }

   /* Authentication/join processing can finish after the initial window
    * focus grab.  Select the now-authoritative rig tab and restore focus to
    * its input once its widgets exist. */
   gint rig_page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), rig_room_tab->page);

   if (rig_page >= 0) {
      gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), rig_page);
   }

   if (rig_room_tab->entry && GTK_IS_WIDGET(rig_room_tab->entry)) {
      gtk_widget_grab_focus(rig_room_tab->entry);
   }

   GtkWidget *label = gtk_notebook_get_tab_label(GTK_NOTEBOOK(main_notebook), rig_room_tab->page);
   if (label && GTK_IS_LABEL(label)) {
      char text[160];
      snprintf(text, sizeof(text), "(%d) %s", next_chat_tab, room);
      gtk_label_set_text(GTK_LABEL(label), text);
   }
}

bool chat_init(void) {
   room_tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   status_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *status_tab_label = gtk_label_new(NULL);
   char tab_desc[64];
   memset(tab_desc, 0, sizeof(tab_desc));
   snprintf(tab_desc, sizeof(tab_desc), "(<u>%d</u>) status", next_chat_tab);
   gtk_label_set_markup(GTK_LABEL(status_tab_label), tab_desc);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), status_tab, status_tab_label);
   input_history = g_ptr_array_new_with_free_func(g_free);
   g_object_set_data_full(G_OBJECT(main_notebook), "rr-shared-input-history", input_history, (GDestroyNotify)g_ptr_array_unref);

   rig_room_tab = g_new0(GtkRoomTab, 1);
   rig_room_tab->page = status_tab;
   g_object_set_data(G_OBJECT(status_tab), "rr-room-tab", rig_room_tab);
   /* Keep a usable status page before authentication.  If connection setup
    * fails, errors must have a GTK text buffer to land in; after the server
    * announces the authoritative room, gtk_chat_set_authoritative_room()
    * replaces this placeholder with the rig room controls. */
   GtkWidget *status_box = create_chat_box_for_room(false, NULL, false);
   if (status_box) {
      gtk_box_pack_start(GTK_BOX(status_tab), status_box, TRUE, TRUE, 0);
      rig_room_tab->view = chat_textview;
      rig_room_tab->entry = chat_entry;
   }
   g_signal_connect(main_notebook, "switch-page", G_CALLBACK(gtk_chat_select_tab), NULL);
   userlist_redraw_gtk();
   return false;
}

bool parse_chat_input_gtk(GtkButton *button, gpointer entry) {
   const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));

   if (text && *text) {
      // Copy before clearing/dispatch: commands may change tabs or destroy
      // the originating entry. Never retain GtkEntry's borrowed text pointer.
      char *message = g_strdup(text);
      GPtrArray *lines;
      GtkInputHistory *history = entry_history(GTK_WIDGET(entry), &lines);
      // PARITY: browser chat_history_add() in js/webui.chat.completion.js;
      // TUI history is owned by librustyaxe/tui.keys.c. Shared is the default.
      if (lines && (!lines->len || strcmp(g_ptr_array_index(lines, lines->len - 1), message))) {
         if (lines->len >= HISTORY_LINES) g_ptr_array_remove_index(lines, 0);
         g_ptr_array_add(lines, g_strdup(message));
      }
      history->index = -1;
      g_clear_pointer(&history->draft, g_free);
      gtk_entry_set_text(GTK_ENTRY(entry), "");
      parse_chat_input_real(message);
      g_free(message);
   }
   return false;
}
