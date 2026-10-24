//
// rrclient/gtk/gtk.chat.c: Chat stuff
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <rrclient/irc.stats.h>
#include <rrclient/gtk/gtk.tabs.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <librustyaxe/core.h>
#include <librustyaxe/maidenhead.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/cmd.h>
#include <rrclient/cmd.help.h>
#include <rrclient/ui.h>
#include <rrclient/connman.h>
#include <rrclient/media.direct.h>
#include <rrclient/sercom.h>
#include <rrclient/media.h>
#include <rrclient/rooms.h>
#include <rrclient/vfo.h>
#include <rrclient/objects.h>
#include <rrclient/gtk/ui.speech.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.floating-panel.h>
#include <rrclient/gtk/gtk.connect-button.h>
#include <rrclient/gtk/gtk.vfo-box.h>
#include <rrclient/userlist.h>
#include <rrclient/irc.whois.h>

extern dict *cfg;               // main.c
extern time_t now;              // main.c
extern GtkWidget *main_notebook, *status_tab, *rr_main_toolbar;
extern bool cfg_ui_gtk_vfo_on_top;

///////////////
static GPtrArray *input_history = NULL;
typedef struct {
   GPtrArray *local;
   int index;
   char *draft;
   bool shared;
} GtkInputHistory;

typedef struct {
   GArray *positions;
   char *snapshot;
   int pending_position;
   bool loading;
   GPtrArray *undo, *redo;
} GtkFormatState;

static void input_history_free(gpointer data) {
   GtkInputHistory *history = data;

   if (history->local) {
      g_ptr_array_unref(history->local);
   }
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

   if (!shared && !history->local) {
      history->local = g_ptr_array_new_with_free_func(g_free);
   }
   *lines = shared ? input_history : history->local;

   return history;
}

GtkWidget *chat_textview = NULL;
GtkWidget *chat_entry = NULL;
GtkTextBuffer *text_buffer = NULL;

typedef struct {
   char room[128];
   char server[512];
   bool is_query;
   GtkWidget *page;
   GtkWidget *view;
   GtkWidget *entry;
} GtkRoomTab;

static GHashTable *room_tabs = NULL;
static GtkRoomTab *status_room_tab = NULL;
static GtkWidget *status_send_button;
static gboolean status_network_tick(gpointer unused);
static gboolean typing_shutting_down;

bool gtk_chat_status_active(void) {
   if (!main_notebook || !status_tab) {
      return false;
   }

   return gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_notebook), gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook))) == status_tab;
}

static void gtk_chat_server_selected(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)data;
   (void)cptr;
   (void)user;

   if (status_send_button) {
      const char *name = rrclient_selected_server();
      char label[1024];
      snprintf(label, sizeof(label), name ? "Send|%s|" : "Send", name);
      gtk_button_set_label(GTK_BUTTON(status_send_button), label);
   }
   /* Update selected-server status immediately, not at the next sample. */
   status_network_tick(NULL);
}

int next_chat_tab = 5;


/* Tab cycling is navigation, not an instruction to retarget CAT/GPS. */
static bool navigation_only;
void gtk_chat_set_navigation_only(bool value) {
   navigation_only = value;
}

extern void vfo_controls_set_rx_only(bool rx_only);
static GtkWidget *room_vfo_box;
static RRFloatPanel *room_vfo_panel;
static void gtk_chat_update_vfo_controls(GtkRoomTab *tab);

static void gtk_chat_select_tab(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer user_data) {
   (void)notebook;
   (void)page_num;
   (void)user_data;
   GtkRoomTab *tab = page ? g_object_get_data(G_OBJECT(page), "rr-room-tab") : NULL;

   if (tab && tab->view && GTK_IS_TEXT_VIEW(tab->view)) {
      if (tab->server[0]) {
         rrclient_connection_select(tab->server);
      }
      chat_textview = tab->view;
      chat_entry = tab->entry;
      text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab->view));
      /* The shared user list follows the selected room, including when it is detached in its own window. */
      userlist_redraw_gtk();
      rrclient_media_room_selected(tab->room);
      /* IRC and ordinary chat tabs must not steal the last rig's CAT/GPS focus. */
      rrconn_t *rig_connection = rrclient_connection_find(tab->server);

      if (!navigation_only && rig_connection && rig_connection->is_ws) {
         rr_sercom_focus_room(tab->server, tab->room);
      }
      gtk_chat_update_vfo_controls(tab);
      // Retained streams send no new subscription reply when revisiting a tab.
      // Refresh after moving/creating the shared VFO widget, including its list.
      codec_pickers_refresh();
   }
}

static bool gtk_chat_copy_widgets(GtkWidget *view, GtkWidget *entry) {
   gint first, last;
   bool input_selected = entry && GTK_IS_EDITABLE(entry) && gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &first, &last);
   GtkTextBuffer *buffer = view && GTK_IS_TEXT_VIEW(view) ? gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)) : NULL;
   GtkTextIter start, end;
   bool transcript_selected = buffer && gtk_text_buffer_get_selection_bounds(buffer, &start, &end);

   /* GTK can retain selections in both widgets. Prefer the focused one. */
   if (input_selected && (!transcript_selected || !gtk_widget_has_focus(view))) {
      gtk_editable_copy_clipboard(GTK_EDITABLE(entry));

      return true;
   }

   if (transcript_selected) {
      gtk_text_buffer_copy_clipboard(buffer, gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));

      return true;
   }

   return false;
}

static gboolean gtk_chat_view_keypress(GtkWidget *view, GdkEventKey *event, gpointer user_data) {
   (void)user_data;

   if (!event || event->type != GDK_KEY_PRESS || (event->keyval != GDK_KEY_c && event->keyval != GDK_KEY_C)) {
      return FALSE;
   }
   GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask();

   if (modifiers != (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
      return FALSE;
   }

   return gtk_chat_copy_widgets(view, g_object_get_data(G_OBJECT(view), "rr-chat-entry"));
}

bool gtk_chat_copy_selection(void) {
   GtkWidget *view = chat_textview, *entry = chat_entry;

   if (main_notebook) {
      GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_notebook), gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook)));
      GtkRoomTab *tab = page ? g_object_get_data(G_OBJECT(page), "rr-room-tab") : NULL;

      if (tab) {
         view = tab->view;
         entry = tab->entry;
      }
   }

   return gtk_chat_copy_widgets(view, entry);
}

bool gtk_chat_room_widgets(const char *room, GtkTextBuffer **buffer, GtkWidget **view) {
   GtkRoomTab *tab = NULL;

   if (!room || !*room || !strcasecmp(room, "status")) {
      tab = status_room_tab;
   } else if (room_tabs) {
      tab = g_hash_table_lookup(room_tabs, rrclient_window_name(room));
   }

   if (!tab) {
      return false;
   }

   if (!tab->view || !GTK_IS_TEXT_VIEW(tab->view)) {
      return false;
   }

   if (buffer) {
      *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab->view));
   }

   if (view) {
      *view = tab->view;
   }

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
   /* Scrolling to the end iter can also pan horizontally to the end of a
    * long replay line, hiding the start of every chat line. Only scroll
    * vertically; horizontal chat position must stay at the left edge. */
   GtkAdjustment *vertical = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(chat_textview));
   GtkAdjustment *horizontal = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(chat_textview));
   if (vertical)
      gtk_adjustment_set_value(vertical, MAX(gtk_adjustment_get_lower(vertical),
         gtk_adjustment_get_upper(vertical) - gtk_adjustment_get_page_size(vertical)));
   if (horizontal)
      gtk_adjustment_set_value(horizontal, gtk_adjustment_get_lower(horizontal));

   // remove the idle handler after it runs
   return FALSE;
}

//////////

static bool gtk_chat_do_completion(GtkEntry *entry) {
   const char *profile = g_object_get_data(G_OBJECT(entry), "rr-server-name");

   if (profile && *profile) {
      rrclient_connection_select(profile);
   }
   const char *line = gtk_entry_get_text(entry);
   int tui_cursor_pos = gtk_editable_get_position(GTK_EDITABLE(entry));

   if (!line || tui_cursor_pos <= 0) {
      return false;
   }
   size_t cursor = (size_t)(g_utf8_offset_to_pointer(line, tui_cursor_pos) - line);
   void *old_state = g_object_get_data(G_OBJECT(entry), "rr-nick-completion");
   void *state = old_state;
   char *completed = client_chat_complete(line, &cursor, &state);

   if (state != old_state) {
      /* The common helper already released the old state. */
      g_object_steal_data(G_OBJECT(entry), "rr-nick-completion");
      g_object_set_data_full(G_OBJECT(entry), "rr-nick-completion", state, client_chat_completion_free);
   }

   if (completed) {
      gtk_entry_set_text(entry, completed);
      gtk_editable_set_position(GTK_EDITABLE(entry), g_utf8_pointer_to_offset(completed, completed + cursor));
      free(completed);

      return true;
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

   for (int i = 1 ; i < nmatch ; i++) {
      size_t j = 0;

      while (j < plen && matches[i][j] && matches[0][j] == matches[i][j]) {
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
      gtk_editable_set_position(GTK_EDITABLE(entry), g_utf8_pointer_to_offset(new_line, new_line + start + replace_len));

      free(new_line);
      completion_free(matches);

      return true;
   }
   // Ambiguous: print candidates into the chat window
   // ui.theme.completion contains a decoded IRC color control prefix.
   const char *completion_color = cfg_get("ui.theme.completion");

   if (!completion_color || !*completion_color) {
      completion_color = "\00313";
   }
   // PARITY: TUI completion uses the same configured IRC color prefix.
   char color_tag[64];

   snprintf(color_tag, sizeof(color_tag), "%s", completion_color);

   // Multi-column layout across a few lines (column-major, like the TUI)
   {
      char labels[TUI_MAX_COMPLETIONS_SHOWN][512];
      int maxlen = 0;
      int nshown = nmatch > TUI_MAX_COMPLETIONS_SHOWN ? TUI_MAX_COMPLETIONS_SHOWN : nmatch;

      for (int i = 0 ; i < nshown ; i++) {
         completion_describe(gtk_entry_get_text(entry), matches[i], labels[i], sizeof(labels[i]));
         int l = (int)strlen(labels[i]);

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

      for (int r = 0 ; r < rows ; r++) {
         char line[1024];
         size_t pos = 0;

         pos += snprintf(line + pos, sizeof(line) - pos, "  ");

         for (int c = 0 ; c < cols ; c++) {
            int idx = c * rows + r; // column-major so matches read down each column

            if (idx >= nshown) {
               break;
            }
            pos += snprintf(line + pos, sizeof(line) - pos, "%-*s  ", maxlen, labels[idx]);

            if (pos >= sizeof(line) - 1) {
               break;
            }
         }

         char colored[1100];

         snprintf(colored, sizeof(colored), "%s%s\017", color_tag, line);
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

      snprintf(buf, sizeof(buf), "... and %d more\n", nmatch - TUI_MAX_COMPLETIONS_SHOWN);

      gtk_text_buffer_insert_at_cursor(text_buffer, buf, -1);
   }

   completion_free(matches);
   g_idle_add(ui_scroll_to_end, chat_textview);

   return false;
}

static void gtk_chat_format_state_free(gpointer data) {
   GtkFormatState *state = data;
   g_array_unref(state->positions);
   g_ptr_array_unref(state->undo);
   g_ptr_array_unref(state->redo);
   g_free(state->snapshot);
   g_free(state);
}

static GtkFormatState *gtk_chat_format_state(GtkWidget *entry) {
   GtkFormatState *state = g_object_get_data(G_OBJECT(entry), "rr-format-state");

   if (!state) {
      state = g_new0(GtkFormatState, 1);
      state->positions = g_array_new(FALSE, FALSE, sizeof(guint));
      state->snapshot = g_strdup("");
      state->pending_position = -1;
      state->undo = g_ptr_array_new_with_free_func(g_free);
      state->redo = g_ptr_array_new_with_free_func(g_free);
      g_object_set_data_full(G_OBJECT(entry), "rr-format-state", state, gtk_chat_format_state_free);
   }

   return state;
}

static gboolean gtk_chat_position_is_token(GtkFormatState *state, guint position) {
   guint low = 0, high = state->positions->len;
   while (low < high) {
      guint mid = low + (high - low) / 2;
      guint value = g_array_index(state->positions, guint, mid);

      if (value < position) {
         low = mid + 1;
      } else {
         high = mid;
      }
   }
   return low < state->positions->len && g_array_index(state->positions, guint, low) == position;
}

static void gtk_chat_apply_format_attributes(GtkWidget *entry, GtkFormatState *state) {
   PangoAttrList *attrs = pango_attr_list_new();

   const char *p = state->snapshot;
   guint previous = 0;

   for (guint i = 0 ; i < state->positions->len ; i++) {
      guint position = g_array_index(state->positions, guint, i);
      p = g_utf8_offset_to_pointer(p, position - previous);
      previous = position;

      if (!*p) {
         continue;
      }
      guint start = (guint) (p - state->snapshot);
      guint end = (guint) (g_utf8_next_char(p) - state->snapshot);
      PangoAttribute *foreground = pango_attr_foreground_new(0xffff, 0xffff, 0xffff);
      PangoAttribute *background = pango_attr_background_new(0x3030, 0x6060, 0xb0b0);
      PangoAttribute *weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
      foreground->start_index = background->start_index = weight->start_index = start;
      foreground->end_index = background->end_index = weight->end_index = end;
      pango_attr_list_insert(attrs, foreground);
      pango_attr_list_insert(attrs, background);
      pango_attr_list_insert(attrs, weight);
   }

   gtk_entry_set_attributes(GTK_ENTRY(entry), attrs);
   pango_attr_list_unref(attrs);
}

static void gtk_chat_color_choice(GtkButton *button, gpointer data);

static void gtk_chat_color_popover_fill(GtkWidget *entry, bool backgrounds, unsigned int foreground) {
   GtkWidget *popover = g_object_get_data(G_OBJECT(entry), "rr-color-popover");
   GtkWidget *old_grid = gtk_bin_get_child(GTK_BIN(popover));

   if (old_grid) {
      gtk_widget_destroy(old_grid);
   }
   GtkWidget *grid = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 3);

   for (unsigned int color = 0 ; color < 100 ; color++) {
      char sample[32];

      if (backgrounds) {
         snprintf(sample, sizeof(sample), "\003%u,%u[%u,%u]", foreground, color, foreground, color);
      } else {
         snprintf(sample, sizeof(sample), "\003%u[%u]", color, color);
      }
      char *markup = gtk_colorize_string(sample);
      GtkWidget *label = gtk_label_new(NULL);

      if (markup) {
         gtk_label_set_markup(GTK_LABEL(label), markup);
         g_free(markup);
      }
      GtkWidget *choice = gtk_button_new();
      gtk_container_add(GTK_CONTAINER(choice), label);
      const char *name = gtk_mirc_color_name(color);
      char *tip = backgrounds ? g_strdup_printf("%s on %s", gtk_mirc_color_name(foreground), name)
         : g_strdup_printf("%s (%u)", name, color);
      gtk_widget_set_tooltip_text(choice, tip);
      g_free(tip);
      g_object_set_data(G_OBJECT(choice), "rr-color-entry", entry);
      g_object_set_data(G_OBJECT(choice), "rr-color-value", GUINT_TO_POINTER(color));
      g_signal_connect(choice, "clicked", G_CALLBACK(gtk_chat_color_choice), entry);
      gtk_grid_attach(GTK_GRID(grid), choice, color % 10, color / 10, 1, 1);
   }

   gtk_container_add(GTK_CONTAINER(popover), grid);
   gtk_widget_show_all(popover);
   gtk_popover_popup(GTK_POPOVER(popover));
}

static char *gtk_chat_serialize_entry(GtkWidget *entry) {
   GtkFormatState *state = gtk_chat_format_state(entry);
   const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
   GString *serialized = g_string_new(NULL);
   guint position = 0;

   for (const char *p = text ; *p ; p = g_utf8_next_char(p), position++) {
      if (gtk_chat_position_is_token(state, position)) {
         guint8 control = gtk_formatting_control(g_utf8_get_char(p));

         if (control) {
            g_string_append_c(serialized, (char)control);
         } else {
            g_string_append_len(serialized, p, g_utf8_next_char(p) - p);
         }
      } else {
         g_string_append_len(serialized, p, g_utf8_next_char(p) - p);
      }
   }

   return g_string_free(serialized, FALSE);
}

static void gtk_chat_set_serialized_entry(GtkWidget *entry, const char *serialized) {
   GtkFormatState *state = gtk_chat_format_state(entry);
   GString *display = g_string_new(NULL);
   GArray *positions = g_array_new(FALSE, FALSE, sizeof(guint));
   guint position = 0;

   for (const char *p = serialized ; *p ; ) {
      const char *token = gtk_formatting_token((guint8) * p);

      if (token) {
         g_array_append_val(positions, position);
         g_string_append(display, token);
         position++;
         p++;
      } else {
         const char *next = g_utf8_next_char(p);
         g_string_append_len(display, p, next - p);
         position++;
         p = next;
      }
   }

   state->loading = true;
   gtk_entry_set_text(GTK_ENTRY(entry), display->str);
   state->loading = false;
   g_array_unref(state->positions);
   state->positions = positions;
   g_free(state->snapshot);
   state->snapshot = g_string_free(display, FALSE);
   gtk_chat_apply_format_attributes(entry, state);
   gtk_editable_set_position(GTK_EDITABLE(entry), -1);
}

static gint gtk_chat_uint_compare(gconstpointer left, gconstpointer right) {
   guint a = *(const guint *)left;
   guint b = *(const guint *)right;

   return (a > b) - (a < b);
}

static gboolean gtk_chat_update_color_popup(gpointer data) {
   GtkWidget *entry = GTK_WIDGET(data);
   GtkFormatState *state = gtk_chat_format_state(entry);

   if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-active"))) {
      return G_SOURCE_REMOVE;
   }

   const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
   int start = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-start")) - 1;
   int cursor = gtk_editable_get_position(GTK_EDITABLE(entry));
   int length = (int)g_utf8_strlen(text, -1);

   if (start < 0 || cursor <= start || start >= length) {
      goto hide_color_popup;
   }
   const char *p = g_utf8_offset_to_pointer(text, start);

   if (!gtk_chat_position_is_token(state, (guint) start) || gtk_formatting_control(g_utf8_get_char(p)) != 0x03) {
      goto hide_color_popup;
   }
   p = g_utf8_next_char(p);
   const char *end = g_utf8_offset_to_pointer(text, cursor);
   int foreground = -1;
   bool comma = false;

   for ( ; p < end ; p++) {
      if (*p == ',') {
         if (comma || foreground < 0) {
            goto hide_color_popup;
         }
         comma = true;
      } else if (g_ascii_isdigit(*p)) {
         if (!comma) {
            foreground = (foreground < 0 ? 0 : foreground) * 10 + (*p - '0');
         }
      } else {
         goto hide_color_popup;
      }
   }

   if (comma && (foreground < 0 || foreground >= 100)) {
      goto hide_color_popup;
   }
   int previous_mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode"));
   int previous_foreground = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-foreground")) - 1;
   bool force_backgrounds = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-force-backgrounds"));
   int mode = comma ? 2 : (force_backgrounds || (previous_mode == 3 && previous_foreground == foreground) ? 3 : 1);
   g_object_set_data(G_OBJECT(entry), "rr-color-insert-position", GINT_TO_POINTER(cursor + 1));
   g_object_set_data(G_OBJECT(entry), "rr-color-mode", GINT_TO_POINTER(mode));
   g_object_set_data(G_OBJECT(entry), "rr-color-foreground", GINT_TO_POINTER(foreground + 1));
   g_object_set_data(G_OBJECT(entry), "rr-color-force-backgrounds", NULL);

   if (previous_mode != mode || ((mode == 2 || mode == 3) && previous_foreground != foreground) || force_backgrounds) {
      gtk_chat_color_popover_fill(entry, mode != 1, foreground);
      gtk_widget_grab_focus(entry);
      gtk_editable_select_region(GTK_EDITABLE(entry), cursor, cursor);
      gtk_editable_set_position(GTK_EDITABLE(entry), cursor);
   }

   return G_SOURCE_REMOVE;

hide_color_popup:
   g_object_set_data(G_OBJECT(entry), "rr-color-active", NULL);
   gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(entry), "rr-color-popover")));

   return G_SOURCE_REMOVE;
}

/* Per-composer ephemeral presence. Draft text never leaves the client. Changes are throttled; the room owner will assign sequence and expiry. */
typedef struct {
   guint pause_source;
   gint64 last_sent;
   gboolean visible, paused;
} GtkTypingComposer;

static void gtk_typing_composer_free(gpointer data) {
   GtkTypingComposer *state = data;

   if (state->pause_source) {
      g_source_remove(state->pause_source);
   }
   g_free(state);
}

static GtkTypingComposer *gtk_typing_state(GtkWidget *entry) {
   GtkTypingComposer *state = g_object_get_data(G_OBJECT(entry), "rr-typing-composer");

   if (!state) {
      state = g_new0(GtkTypingComposer, 1);
      g_object_set_data_full(G_OBJECT(entry), "rr-typing-composer", state, gtk_typing_composer_free);
   }

   return state;
}

static gboolean gtk_typing_emit(GtkWidget *entry, const char *status) {
   const char *room = g_object_get_data(G_OBJECT(entry), "rr-room-name");
   const char *profile = g_object_get_data(G_OBJECT(entry), "rr-server-name");

   if (!room || (*room != '#' && *room != '&') || !profile || !*profile || rrclient_connection_state(profile) != 1) {
      return FALSE;
   }
   rrconn_t *connection = rrclient_connection_find(profile);

   if (!connection || !connection->is_ws) {
      return FALSE;
   }
   dict *d = dict_new();

   if (!d) {
      return FALSE;
   }
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "typing");
   dict_add(d, "talk.target", room);
   dict_add(d, "talk.typing", status);
   gboolean sent = ws_send_dict(NULL, connection, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return sent;
}

static gboolean gtk_typing_paused(gpointer user) {
   GtkWidget *entry = user;
   GtkTypingComposer *state = g_object_get_data(G_OBJECT(entry), "rr-typing-composer");

   if (!state) {
      return G_SOURCE_REMOVE;
   }
   state->pause_source = 0;

   if (state->visible && !state->paused) {
      if (gtk_typing_emit(entry, "paused")) {
         state->paused = TRUE;
         state->last_sent = g_get_monotonic_time();
      }
   }

   return G_SOURCE_REMOVE;
}

static void gtk_typing_cancel(GtkWidget *entry) {
   if (!entry || !GTK_IS_WIDGET(entry)) {
      return;
   }
   GtkTypingComposer *state = g_object_get_data(G_OBJECT(entry), "rr-typing-composer");

   if (state && state->pause_source) {
      g_source_remove(state->pause_source);
      state->pause_source = 0;
   }
}

void gtk_chat_typing_stop_timers(void) {
   typing_shutting_down = TRUE;

   if (status_room_tab) {
      gtk_typing_cancel(status_room_tab->entry);
   }

   if (room_tabs) {
      GHashTableIter iter;
      gpointer value;
      g_hash_table_iter_init(&iter, room_tabs);
      while (g_hash_table_iter_next(&iter, NULL, &value)) {
         GtkRoomTab *tab = value;

         if (tab) {
            gtk_typing_cancel(tab->entry);
         }
      }
   }
}

static void gtk_typing_entry_changed(GtkWidget *entry) {
   if (typing_shutting_down) {
      return;
   }
   GtkTypingComposer *state = gtk_typing_state(entry);
   const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
   gboolean composing = cfg_get_bool("ui.chat.typing", true) && text && *text && *text != '/';

   if (state->pause_source) {
      g_source_remove(state->pause_source);
      state->pause_source = 0;
   }

   if (!composing) {
      if (state->visible) {
         gtk_typing_emit(entry, "idle");
      }
      state->visible = state->paused = FALSE;
      state->last_sent = 0;

      return;
   }
   const gint64 at = g_get_monotonic_time();

   if (!state->visible) {
      if (gtk_typing_emit(entry, "started")) {
         state->visible = TRUE;
         state->last_sent = at;
      }
   } else if (state->paused || at - state->last_sent >= 2 * G_USEC_PER_SEC) {
      if (gtk_typing_emit(entry, "active")) {
         state->paused = FALSE;
         state->last_sent = at;
      }
   }

   if (state->visible) {
      state->pause_source = g_timeout_add(3000, gtk_typing_paused, entry);
   }
}

static void gtk_chat_entry_changed(GtkEditable *editable, gpointer data) {
   (void)data;
   GtkWidget *entry = GTK_WIDGET(editable);
   GtkFormatState *state = gtk_chat_format_state(entry);
   const char *text = gtk_entry_get_text(GTK_ENTRY(entry));

   if (!state->loading) {
      /* Serialize the previous snapshot, whose token positions still match. */
      GString *prior = g_string_new(NULL);
      guint token = 0, offset = 0;

      for (const char *p = state->snapshot ; *p ; p = g_utf8_next_char(p), offset++) {
         bool formatted = token < state->positions->len && g_array_index(state->positions, guint, token) == offset;
         guint8 control = formatted ? gtk_formatting_control(g_utf8_get_char(p)) : 0;

         if (formatted) {
            token++;
         }

         if (control) {
            g_string_append_c(prior, control);
         } else {
            g_string_append_len(prior, p, g_utf8_next_char(p) - p);
         }
      }

      if (state->undo->len >= 100) {
         g_ptr_array_remove_index(state->undo, 0);
      }
      g_ptr_array_add(state->undo, g_string_free(prior, FALSE));
      g_ptr_array_set_size(state->redo, 0);
      const char *old = state->snapshot;
      const char *old_end = old + strlen(old);
      const char *new_end = text + strlen(text);
      const char *a = old, *b = text;
      guint prefix = 0;
      while (a < old_end && b < new_end && g_utf8_get_char(a) == g_utf8_get_char(b)) {
         a = g_utf8_next_char(a);
         b = g_utf8_next_char(b);
         prefix++;
      }
      const char *old_suffix = old_end, *new_suffix = new_end;
      guint suffix = 0;
      while (old_suffix > a && new_suffix > b) {
         const char *prev_old = g_utf8_find_prev_char(old, old_suffix);
         const char *prev_new = g_utf8_find_prev_char(text, new_suffix);

         if (!prev_old || !prev_new || g_utf8_get_char(prev_old) != g_utf8_get_char(prev_new)) {
            break;
         }
         old_suffix = prev_old;
         new_suffix = prev_new;
         suffix++;
      }
      guint old_count = (guint) g_utf8_strlen(old, -1);
      guint new_count = (guint) g_utf8_strlen(text, -1);
      guint old_after = old_count - suffix;
      gint delta = (gint) new_count - (gint) old_count;

      guint kept = 0;

      for (guint i = 0 ; i < state->positions->len ; i++) {
         guint position = g_array_index(state->positions, guint, i);

         if (position >= old_after) {
            position = (guint) ((gint) position + delta);
         } else if (position >= prefix) {
            continue;
         }
         g_array_index(state->positions, guint, kept++) = position;
      }

      g_array_set_size(state->positions, kept);

      if (state->pending_position >= 0) {
         guint pending = (guint) state->pending_position;
         g_array_append_val(state->positions, pending);
         state->pending_position = -1;
         g_array_sort(state->positions, (GCompareFunc) gtk_chat_uint_compare);
      }
      g_free(state->snapshot);
      state->snapshot = g_strdup(text);
   }
   gtk_chat_apply_format_attributes(entry, state);

   if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-active"))) {
      g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, gtk_chat_update_color_popup, g_object_ref(entry), g_object_unref);
   }

   if (!state->loading) {
      gtk_typing_entry_changed(entry);
   }
}

static void gtk_chat_color_choice(GtkButton *button, gpointer data) {
   GtkWidget *entry = GTK_WIDGET(data);
   unsigned int color = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "rr-color-value"));
   int mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode"));
   char number[5];
   snprintf(number, sizeof(number), mode == 3 ? ",%02u" : "%02u", color);
   gint position = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-insert-position")) - 1;

   if (position < 0) {
      position = gtk_editable_get_position(GTK_EDITABLE(entry));
   }
   gtk_editable_select_region(GTK_EDITABLE(entry), position, position);
   gtk_editable_insert_text(GTK_EDITABLE(entry), number, -1, &position);
   gtk_editable_set_position(GTK_EDITABLE(entry), position);

   if (mode == 1) {
      g_object_set_data(G_OBJECT(entry), "rr-color-mode", GINT_TO_POINTER(3));
      g_object_set_data(G_OBJECT(entry), "rr-color-foreground", GUINT_TO_POINTER(color + 1));
      g_object_set_data(G_OBJECT(entry), "rr-color-force-backgrounds", GINT_TO_POINTER(1));
   } else if (mode == 2 || mode == 3) {
      g_object_set_data(G_OBJECT(entry), "rr-color-active", NULL);
      gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(entry), "rr-color-popover")));
   }
   gtk_widget_grab_focus(entry);
   gtk_editable_select_region(GTK_EDITABLE(entry), position, position);
   gtk_editable_set_position(GTK_EDITABLE(entry), position);
}

static void gtk_chat_insert_format(GtkWidget *entry, guint8 control) {
   const char *token = gtk_formatting_token(control);

   if (!token) {
      return;
   }
   gint position = gtk_editable_get_position(GTK_EDITABLE(entry));
   gtk_editable_select_region(GTK_EDITABLE(entry), position, position);
   GtkFormatState *state = gtk_chat_format_state(entry);
   state->pending_position = position;

   if (control == 0x03) {
      g_object_set_data(G_OBJECT(entry), "rr-color-active", GINT_TO_POINTER(1));
      g_object_set_data(G_OBJECT(entry), "rr-color-start", GINT_TO_POINTER(position + 1));
   }
   gtk_editable_insert_text(GTK_EDITABLE(entry), token, -1, &position);
   gtk_editable_set_position(GTK_EDITABLE(entry), position);

   if (control == 0x03) {
      gtk_chat_color_popover_fill(entry, false, 0);
      gtk_widget_grab_focus(entry);
   }
   gtk_editable_select_region(GTK_EDITABLE(entry), position, position);
   gtk_editable_set_position(GTK_EDITABLE(entry), position);
}

static void on_send_button_clicked(GtkButton *button, gpointer entry) {
   gtk_widget_grab_focus(GTK_WIDGET(entry));
   parse_chat_input_gtk(button, entry);
}

// Here we support input history for the chat/control window entry input
static gboolean on_chat_entry_keypress(GtkWidget *entry, GdkEventKey *event, gpointer user_data) {
   if (!event || !entry) {
      return FALSE;
   }

   GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask();

   if ((modifiers & GDK_CONTROL_MASK) && !(modifiers & GDK_MOD1_MASK) &&
      (event->keyval == GDK_KEY_z || event->keyval == GDK_KEY_Z || event->keyval == GDK_KEY_y || event->keyval == GDK_KEY_Y)) {
      GtkFormatState *state = gtk_chat_format_state(entry);
      bool redo = (modifiers & GDK_SHIFT_MASK) || event->keyval == GDK_KEY_y || event->keyval == GDK_KEY_Y;
      GPtrArray *from = redo ? state->redo : state->undo;
      GPtrArray *to = redo ? state->undo : state->redo;

      if (from->len) {
         g_ptr_array_add(to, gtk_chat_serialize_entry(entry));
         char *text = g_ptr_array_steal_index(from, from->len - 1);
         gtk_chat_set_serialized_entry(entry, text);
         g_free(text);
      }

      return TRUE;
   }

   if (event->type == GDK_KEY_PRESS && (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C) &&
      (modifiers & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && !(modifiers & GDK_MOD1_MASK)) {
      return gtk_chat_copy_selection();
   }

   if ((modifiers & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && !(modifiers & GDK_MOD1_MASK)) {
      guint8 style_code = 0;

      switch (event->keyval) {
         case GDK_KEY_u:
         case GDK_KEY_U: {
            style_code = 0x1f;
            break;
         }
         case GDK_KEY_s:
         case GDK_KEY_S: {
            style_code = 0x1e;
            break;
         }
         case GDK_KEY_m:
         case GDK_KEY_M: {
            style_code = 0x11;
            break;
         }
      }

      if (style_code) {
         gtk_chat_insert_format(entry, style_code);

         return TRUE;
      }
   }

   if ((modifiers & GDK_CONTROL_MASK) && !(modifiers & (GDK_MOD1_MASK | GDK_SHIFT_MASK))) {
      guint8 control_code = 0;

      switch (event->keyval) {
         case GDK_KEY_b:
         case GDK_KEY_B: {
            control_code = 0x02;
            break;              // Bold
         }
         case GDK_KEY_c:
         case GDK_KEY_C: {
            control_code = 0x03;
            break;              // Color
         }
         case GDK_KEY_i:
         case GDK_KEY_I: {
            control_code = 0x1d;
            break;              // Italic
         }
         case GDK_KEY_o:
         case GDK_KEY_O: {
            control_code = 0x0f;
            break;              // Reset
         }
         case GDK_KEY_u:
         case GDK_KEY_U: {
            gtk_entry_set_text(GTK_ENTRY(entry), "");

            return TRUE;        // Ctrl-U clears input, matching the TUI
         }
         case GDK_KEY_r:
         case GDK_KEY_R: {
            control_code = 0x16;
            break;              // Reverse
         }
      }

      if (control_code) {
         gtk_chat_insert_format(entry, control_code);

         return TRUE;
      }
   }

   if (event->keyval == GDK_KEY_Escape && GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-active"))) {
      g_object_set_data(G_OBJECT(entry), "rr-color-active", NULL);
      gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(entry), "rr-color-popover")));

      return TRUE;
   }

   if (event->keyval == GDK_KEY_Tab && (modifiers & GDK_CONTROL_MASK) && !(modifiers & (GDK_MOD1_MASK | GDK_SHIFT_MASK))) {
      return rrclient_connection_cycle_status(gtk_chat_status_active());
   }

   if (event->keyval == GDK_KEY_Tab) {
      gtk_chat_do_completion(GTK_ENTRY(entry));

      return TRUE;
   }

   if (event->keyval == GDK_KEY_Page_Up) {
      GtkAdjustment *adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(chat_textview));

      gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) - gtk_adjustment_get_page_increment(adj));

      return TRUE;
   }

   if (event->keyval == GDK_KEY_Page_Down) {
      GtkAdjustment *adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(chat_textview));

      gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_increment(adj));

      return TRUE;
   }

   return gtk_chat_history_keypress(entry, event);
}

/* Window shortcuts run before GtkWindow forwards keys to the focused entry. */
bool gtk_chat_history_keypress(GtkWidget *widget, GdkEventKey *event) {
   if (!widget || !event || event->type != GDK_KEY_PRESS) {
      return false;
   }
   bool previous = event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_KP_Up;
   bool next = event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_KP_Down;

   if ((!previous && !next) || (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK))) {
      return false;
   }
   GtkWidget *window = gtk_widget_get_toplevel(widget);
   GtkWidget *entry = GTK_IS_WINDOW(window) ? gtk_window_get_focus(GTK_WINDOW(window)) : NULL;

   if (!entry || !GTK_IS_ENTRY(entry) || !g_object_get_data(G_OBJECT(entry), "rr-chat-input")) {
      return false;
   }
   GPtrArray *lines;
   GtkInputHistory *history = entry_history(entry, &lines);

   if (!lines || !lines->len) {
      return FALSE;
   }

   if (previous) {
      if (history->index < 0) {
         g_free(history->draft);
         history->draft = gtk_chat_serialize_entry(entry);
         history->index = (int)lines->len - 1;
      } else if (history->index > 0) {
         history->index--;
      }
   } else {
      if (history->index < 0) {
         return TRUE;
      }

      if (++history->index >= (int)lines->len) {
         history->index = -1;
      }
   }
   const char *text = history->index < 0 ? (history->draft ? history->draft : "") : g_ptr_array_index(lines, history->index);
   gtk_chat_set_serialized_entry(entry, text);

   return TRUE;
}


/* The room VFO title operates only on VFOs mapped to the room displayed
 * by the floating rig controls. The server's selection acknowledgement
 * remains authoritative for the active VFO. */
static char room_vfo_title_room[128];

static void room_vfo_title_select(char vfo) {
   const char *room = room_vfo_title_room;
   if (!*room || !ws_conn || rrclient_connection_state(rrclient_selected_server()) != 1)
      return;
   char command[16];
   g_snprintf(command, sizeof(command), "!vfo %c", vfo);
   dict *request = dict_new();
   if (!request) return;
   dict_add(request, "msg.type", "talk");
   dict_add(request, "talk.cmd", "msg");
   dict_add(request, "talk.msg_type", "pub");
   dict_add(request, "talk.target", room);
   dict_add(request, "talk.data", command);
   ws_send_dict(NULL, ws_conn, request, WEBSOCKET_OP_TEXT);
   dict_free(request);
}

static void room_vfo_title_menu_activate(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   char vfo = (char)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "rr-vfo-id"));
   if (vfo >= 'A' && vfo <= 'Z') room_vfo_title_select(vfo);
}

static gboolean room_vfo_title_clicked(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)widget;
   (void)unused;
   if (!event || event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_PRIMARY)
      return FALSE;
   if (!room_vfo_title_room[0]) return FALSE;
   uint32_t mask = rrclient_room_vfo_mask(room_vfo_title_room);
   /* Older servers may advertise bindings before the mask is available. */
   if (!mask) {
      const char *bindings = rrclient_room_vfos(room_vfo_title_room);
      char *copy = g_strdup(bindings ? bindings : "");
      char *save = NULL;
      for (char *part = strtok_r(copy, " \t", &save); part; part = strtok_r(NULL, " \t", &save)) {
         const char *dot = strrchr(part, '.');
         if (dot && !g_ascii_strncasecmp(dot + 1, "vfo_", 4) && dot[5]) {
            char letter = g_ascii_toupper(dot[5]);
            if (letter >= 'A' && letter <= 'Z')
               mask |= UINT32_C(1) << (letter - 'A');
         }
      }
      g_free(copy);
   }
   unsigned count = 0;
   char only[2] = { 0, 0 };
   for (unsigned i = 0; i < 26; i++) {
      if (mask & (UINT32_C(1) << i)) {
         if (count < 2) only[count] = (char)('A' + i);
         count++;
      }
   }
   if (count <= 1) return count == 1;
   if (count == 2) {
      char active = rrclient_room_active_vfo(room_vfo_title_room);
      room_vfo_title_select(active == only[0] ? only[1] : only[0]);
      return TRUE;
   }
   GtkWidget *menu = gtk_menu_new();
   for (unsigned i = 0; i < 26; i++) {
      if (!(mask & (UINT32_C(1) << i))) continue;
      char name[16];
      g_snprintf(name, sizeof(name), "VFO %c", 'A' + i);
      GtkWidget *item = gtk_menu_item_new_with_label(name);
      g_object_set_data(G_OBJECT(item), "rr-vfo-id", GINT_TO_POINTER('A' + i));
      g_signal_connect(item, "activate", G_CALLBACK(room_vfo_title_menu_activate), NULL);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   }
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
   return TRUE;
}

/* One in-client overlay panel follows the selected rig; it is not a toplevel window and cannot be docked into an individual chat tab. */
static void room_vfo_show(const char *name) {
   if (!room_vfo_panel) {
      GtkWidget *root = gtk_bin_get_child(GTK_BIN(main_window));

      if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) {
         return;
      }
      room_vfo_panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook), "rig-vfo", "VFO");

      if (!room_vfo_panel) {
         return;
      }
      rr_floating_panel_set_closable(room_vfo_panel, FALSE);
      rr_floating_panel_set_title_click(room_vfo_panel, G_CALLBACK(room_vfo_title_clicked), NULL);
      room_vfo_box = create_vfo_box();
      g_object_add_weak_pointer(G_OBJECT(room_vfo_box), (gpointer *) &room_vfo_box);
      gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(room_vfo_panel)), room_vfo_box, FALSE, FALSE, 0);
   }
   g_strlcpy(room_vfo_title_room, name ? name : "", sizeof(room_vfo_title_room));
   char *title = g_strdup_printf("%s — VFO", name && *name ? name : "Rig");
   rr_floating_panel_set_title(room_vfo_panel, title);
   g_free(title);
   rr_floating_panel_show(room_vfo_panel);
}

// A single control box follows the selected rig room.  The site lobby and
// query tabs never own radio widgets; creating one per rig would overwrite
// the GTK module's shared frequency/PTT widget pointers.
static void gtk_chat_update_vfo_controls(GtkRoomTab *tab) {
   bool has_vfos = tab && tab->room[0] && (rrclient_room_tx_control(tab->room) || (rrclient_room_rx_tunable(tab->room) && rrclient_room_vfos(tab->room)[0]));

   if (!has_vfos) {
      if (room_vfo_panel) {
         rr_floating_panel_hide(room_vfo_panel);
      }

      return;
   }
   room_vfo_show(tab->room);
   vfo_controls_set_rx_only(!rrclient_room_tx_control(tab->room));
   vfo_update_ui();
}

void gtk_chat_room_vfos_changed(const char *room) {
   userlist_room_vfos_changed(room);
   gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook));
   GtkWidget *widget = gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_notebook), page);
   GtkRoomTab *tab = widget ? g_object_get_data(G_OBJECT(widget), "rr-room-tab") : NULL;
   gtk_chat_update_vfo_controls(tab);
}




static void gtk_chat_resources_clicked(GtkButton *button, gpointer data) {
   (void)button;
   (void)data;
   event_emit("client.resources.open", NULL, NULL);
}

/* Chat is a wrapped viewport: no replay line may pan its left edge away. */
static void chat_horizontal_keep_left(GtkAdjustment *adjustment, gpointer unused) {
   (void)unused;
   double left = gtk_adjustment_get_lower(adjustment);
   if (gtk_adjustment_get_value(adjustment) != left)
      gtk_adjustment_set_value(adjustment, left);
}

/* GtkTextView can report a content-driven natural width before its initial
 * allocation, even with GTK_WRAP_CHAR. A long replay line then widens the
 * GtkNotebook page and both ends of the room layout are clipped. This
 * subclass provides a narrow width requisition while still allowing GTK to
 * allocate the full viewport and wrap its text normally. No resize feedback
 * callbacks and no modification to the displayed chat data. */
typedef struct { GtkTextView parent; } RRWrappingChatView;
typedef struct { GtkTextViewClass parent; } RRWrappingChatViewClass;
G_DEFINE_TYPE(RRWrappingChatView, rr_wrapping_chat_view, GTK_TYPE_TEXT_VIEW)

static void rr_wrapping_chat_view_preferred_width(GtkWidget *widget, gint *minimum, gint *natural) {
   (void)widget;
   *minimum = 1;
   *natural = 1;
}

static void rr_wrapping_chat_view_class_init(RRWrappingChatViewClass *klass) {
   GTK_WIDGET_CLASS(klass)->get_preferred_width = rr_wrapping_chat_view_preferred_width;
}

static void rr_wrapping_chat_view_init(RRWrappingChatView *self) {
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(self), GTK_WRAP_CHAR);
}

static GtkWidget *create_chat_box_for_room(bool is_rig, const char *room, bool is_query) {
   GtkWidget *chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   /* A replay line must never establish the notebook page minimum width. */
   gtk_widget_set_size_request(chat_box, 1, -1);

   if (!chat_box) {
      // XXX: throw OOM warning

      return NULL;
   }

   if (!room || !*room) {
      GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
      GtkWidget *resources = gtk_button_new_with_label("Resource Explorer");
      gtk_widget_set_tooltip_text(resources, "Browse and manage all server resources (Alt-O)");
      g_signal_connect(resources, "clicked", G_CALLBACK(gtk_chat_resources_clicked), NULL);
      gtk_box_pack_start(GTK_BOX(tools), rr_connect_button_new(), FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(tools), resources, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(rr_main_toolbar), tools, FALSE, FALSE, 0);
   }
   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(scrolled, -1, 200);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   /* Text wraps at the viewport allocation, not at the text view's natural width. Keep a small usable chat minimum while the roster grows. */
   gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scrolled), 160);
   gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scrolled), FALSE);
   g_signal_connect(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolled)),
      "value-changed", G_CALLBACK(chat_horizontal_keep_left), NULL);


   // Chat view. Font (monospace) comes from the #chat-view CSS rule in [gtk-css]
   chat_textview = g_object_new(rr_wrapping_chat_view_get_type(), NULL);
   gtk_widget_set_name(chat_textview, "chat-view");
   gtk_widget_add_events(chat_textview, GDK_POINTER_MOTION_MASK);
   g_signal_connect(chat_textview, "key-press-event", G_CALLBACK(gtk_chat_view_keypress), NULL);
   text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_textview));
   g_object_set_data_full(G_OBJECT(chat_textview), "rr-server-name", room ? g_strdup(server_name) : NULL, g_free);
   gtk_chat_links_attach(chat_textview);
   gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_textview), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chat_textview), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat_textview), GTK_WRAP_CHAR);
   /* The size request is a minimum, not an allocation: GTK still fills the
    * scrolled viewport and wraps text there. Never derive this from text. */
   gtk_widget_set_size_request(chat_textview, 1, -1);
   gtk_widget_set_hexpand(chat_textview, TRUE);
   gtk_widget_set_halign(chat_textview, GTK_ALIGN_FILL);
   gtk_container_add(GTK_CONTAINER(scrolled), chat_textview);

   /* Every room owns its detachable roster, including authoritative rooms on different servers. Private query tabs stay chat-only. */
   if (is_rig || (!is_query && room && *room)) {
      GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
      gtk_box_pack_start(GTK_BOX(chat_box), paned, TRUE, TRUE, 0);
      /* Both children must be shrinkable: otherwise the chat view's preferred width
       * clamps the handle and prevents the roster from expanding again. */
      gtk_paned_pack1(GTK_PANED(paned), scrolled, TRUE, TRUE);

      userlist_dock_room_into(GTK_PANED(paned), room);
   } else {
      gtk_box_pack_start(GTK_BOX(chat_box), scrolled, TRUE, TRUE, 0);
   }

   // Chat INPUT
   chat_entry = gtk_entry_new();
   g_object_set_data(G_OBJECT(chat_entry), "rr-chat-input", GINT_TO_POINTER(1));
   g_object_set_data_full(G_OBJECT(chat_entry), "rr-room-name", g_strdup(room ? room : ""), g_free);
   g_object_set_data(G_OBJECT(chat_textview), "rr-chat-entry", chat_entry);
   GtkWidget *color_popover = gtk_popover_new(chat_entry);
   gtk_popover_set_position(GTK_POPOVER(color_popover), GTK_POS_TOP);
   gtk_popover_set_modal(GTK_POPOVER(color_popover), FALSE);
   g_object_set_data(G_OBJECT(chat_entry), "rr-color-popover", color_popover);
   g_signal_connect(chat_entry, "changed", G_CALLBACK(gtk_chat_entry_changed), NULL);
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
   /* GtkEntry handles Return/space itself, so register the global shortcut handler directly on each chat input as well as the main window. */
   gui_hotkey_register(chat_entry);

   // SEND the command/message
   GtkWidget *button = gtk_button_new_with_label("Send");
   /* The status send button includes the selected server name; truncate its
    * display rather than letting a long name expand the entire notebook. */
   GtkWidget *send_label = gtk_bin_get_child(GTK_BIN(button));
   if (GTK_IS_LABEL(send_label)) {
      gtk_label_set_ellipsize(GTK_LABEL(send_label), PANGO_ELLIPSIZE_END);
      gtk_label_set_max_width_chars(GTK_LABEL(send_label), 32);
   }
   gtk_box_pack_start(GTK_BOX(chat_box), button, FALSE, FALSE, 0);
   g_signal_connect(button, "clicked", G_CALLBACK(on_send_button_clicked), chat_entry);

   if (!room || !*room) {
      status_send_button = button;
      gtk_chat_server_selected(NULL, NULL, NULL, NULL);
   }

   return chat_box;
}

GtkWidget *create_chat_box(void) {
   return create_chat_box_for_room(true, ws_authoritative_room(), false);
}

static void gtk_chat_select_existing_tab(GtkRoomTab *tab) {
   gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), tab->page);

   /* Incoming chat ensures the tab exists on every message. Re-grabbing GtkEntry focus selects the draft again, so the next key replaces it. */
   if (page < 0 || page == gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook))) {
      return;
   }
   gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), page);

   if (tab->entry && GTK_IS_WIDGET(tab->entry)) {
      gtk_widget_grab_focus(tab->entry);
   }
}

/* Menu actions use the same command path as Enter, without serializing, clearing, or recording the user's draft in the input widget. */
bool gtk_chat_execute_command(const char *window, const char *command) {
   GtkRoomTab *tab = window && room_tabs ? g_hash_table_lookup(room_tabs, window) : NULL;

   if (window && !tab) {
      return false;
   }

   if (tab) {
      rrclient_connection_select(tab->server);
      gtk_chat_select_existing_tab(tab);
   }

   return parse_chat_input_real(command);
}

/* Resolve this page's current server/room, then reuse common close semantics. */
static void gtk_chat_tab_close(GtkWidget *page, gboolean force) {
   GtkRoomTab *tab = g_object_get_data(G_OBJECT(page), "rr-room-tab");

   if (!tab || tab == status_room_tab || !rrclient_connection_select(tab->server)) {
      return;
   }
   char *window = g_strdup(rrclient_window_name(tab->room));

   if (window) {
      gtk_chat_execute_command(window, force ? "/win close -force" : "/win close");
   }
   g_free(window);
}

static void gtk_chat_tab_add(const char *room, bool is_query, bool select) {
   if (!room || !*room || !main_notebook) {
      return;
   }

   if (!room_tabs) {
      room_tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   }
   GtkRoomTab *existing = g_hash_table_lookup(room_tabs, rrclient_window_name(room));

   if (existing) {
      if (is_query && !existing->is_query) {
         existing->is_query = true;
         userlist_remove_room_view(existing->room);
      }

      if (!select || !rrclient_present_context()) {
         return;
      }
      gtk_chat_select_existing_tab(existing);

      return;
   }
   GtkWidget *active_view = chat_textview, *active_entry = chat_entry;
   GtkTextBuffer *active_buffer = text_buffer;
   GtkRoomTab *tab = g_new0(GtkRoomTab, 1);
   tab->is_query = is_query;
   snprintf(tab->room, sizeof(tab->room), "%s", room);
   snprintf(tab->server, sizeof(tab->server), "%s", server_name ? server_name : "");
   tab->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *box = create_chat_box_for_room(!is_query && !strcasecmp(room, ws_authoritative_room()), room, is_query);
   g_object_set_data(G_OBJECT(tab->page), "rr-chat-box", box);
   tab->view = chat_textview;
   tab->entry = chat_entry;
   g_object_set_data_full(G_OBJECT(tab->entry), "rr-server-name", g_strdup(tab->server), g_free);
   gtk_box_pack_start(GTK_BOX(tab->page), box, TRUE, TRUE, 0);
   g_object_set_data(G_OBJECT(tab->page), "rr-room-tab", tab);
   g_object_set_data_full(G_OBJECT(tab->page), "rr-server-name", g_strdup(tab->server), g_free);
   gtk_tabs_page_set_close_func(tab->page, gtk_chat_tab_close);
   GtkWidget *label = gtk_label_new(room);
   gtk_widget_show_all(label);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), tab->page, label);
   g_hash_table_insert(room_tabs, g_strdup(rrclient_window_name(room)), tab);
   gtk_widget_show_all(tab->page);

   if (is_query) {
      rrclient_irc_query_opened(ws_conn, room);
   }

   if (!select) {
      chat_textview = active_view;
      chat_entry = active_entry;
      text_buffer = active_buffer;

      return;
   }

   // Background servers create tabs without stealing the active conversation.
   if (server_name && rrclient_selected_server() && strcmp(server_name, rrclient_selected_server())) {
      return;
   }
   // A successful JOIN changes the active conversation. Select the new tab
   // so the join notice and subsequent input are directed to this room.
   gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), tab->page);

   if (page >= 0) {
      gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), page);
   }

   if (tab->entry && GTK_IS_WIDGET(tab->entry)) {
      gtk_widget_grab_focus(tab->entry);
   }
}

/* Nickname boundaries avoid highlighting a name embedded in a longer nickname. */
static bool gtk_chat_nick_character(unsigned char c) {
   return g_ascii_isalnum(c) || (c && strchr("_-[]\\`^{}|", c));
}

/* Incoming traffic ensures a tab exists while preserving selection and draft focus. */
void gtk_chat_incoming(const char *room, bool query, const char *text, const char *nickname) {
   bool irc_query = ws_conn && !ws_conn->is_ws && room && room[0] != '#' && room[0] != '&';
   gtk_chat_tab_add(room, query || irc_query, false);
   GtkRoomTab *tab = room && room_tabs ? g_hash_table_lookup(room_tabs, rrclient_window_name(room)) : NULL;
   bool mention = false;
   size_t length = nickname ? strlen(nickname) : 0;

   for (const char *p = text ; p && *p && length ; p++) {
      if ((p == text || !gtk_chat_nick_character((unsigned char)p[-1])) &&
         !g_ascii_strncasecmp(p, nickname, length) && !gtk_chat_nick_character((unsigned char)p[length])) {
         mention = true;
         break;
      }
   }

   if (tab) {
      GtkTextBuffer *buffer = NULL;

      if (gtk_chat_room_widgets(room, &buffer, NULL) && buffer) {
         g_object_set_data(G_OBJECT(buffer), "rr-highlight-prefix", GINT_TO_POINTER(mention));
      }
   }

   if (tab && text) {
      if (mention) {
         rrclient_irc_stats_highlight(ws_conn, room);
      }
      gtk_tabs_page_activity(GTK_NOTEBOOK(main_notebook), tab->page, mention);
   }
}

void gtk_chat_room_add(const char *room) {
   /* IRC NOTICE/ACTION and diagnostic paths can ensure a nick tab through room_add. Only actual channels own rosters on an IRC connection. */
   bool query = ws_conn && !ws_conn->is_ws && room && room[0] != '#' && room[0] != '&';
   gtk_chat_tab_add(room, query, true);
}

void gtk_chat_query_add(const char *user) {
   gtk_chat_tab_add(user, true, true);
}

void gtk_chat_show_status(void) {
   if (!main_notebook || !status_tab) {
      return;
   }
   gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), status_tab);

   if (page >= 0) {
      gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), page);
   }

   if (status_room_tab && status_room_tab->entry && GTK_IS_WIDGET(status_room_tab->entry)) {
      gtk_widget_grab_focus(status_room_tab->entry);
   }
}

void gtk_chat_room_remove(const char *room) {
   if (!room_tabs || !room) {
      return;
   }
   GtkRoomTab *tab = g_hash_table_lookup(room_tabs, rrclient_window_name(room));

   if (!tab) {
      return;
   }
   gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), tab->page);
   userlist_remove_room_view(room);

   if (page >= 0) {
      gtk_notebook_remove_page(GTK_NOTEBOOK(main_notebook), page);
   }
   g_hash_table_remove(room_tabs, rrclient_window_name(room));
}

void gtk_chat_room_set_topic(const char *room, const char *topic) {
   if (!room || !*room) {
      return;
   }
   GtkTextBuffer *buffer = NULL;
   GtkWidget *view = NULL;

   if (!gtk_chat_room_widgets(room, &buffer, &view) || !buffer) {
      return;
   }

   /* Common events.c renders the notice, including setter and timestamp. */
   if (view) {
      gtk_widget_set_tooltip_text(view, topic && *topic ? topic : NULL);
   }
}

void gtk_chat_set_authoritative_room(const char *room) {
   if (!room || !*room || !main_notebook) {
      return;
   }
   /* Keep status independent of server rooms, including across reconnects. */
   gtk_chat_tab_add(room, false, true);
}

/* Locator menu entries hold a room tab and VFO letter, never a borrowed
 * connection-context pointer. Selecting the tab is the authoritative path
 * for switching connection, audio, CAT and GPS focus. */
typedef struct {
   GtkRoomTab *tab;
   char vfo;
} locator_choice_t;

static void locator_choice_activate(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   locator_choice_t *choice = g_object_get_data(G_OBJECT(item), "rr-locator-choice");
   if (!choice || !choice->tab || !choice->tab->page ||
       gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), choice->tab->page) < 0)
      return;
   GtkRoomTab *tab = choice->tab;
   gtk_chat_select_existing_tab(tab);
   /* A selected tab is not guaranteed to have switched connection contexts
    * (it may already be current), so choose the correct profile explicitly. */
   if (tab->server[0]) rrclient_connection_select(tab->server);
   rrclient_media_room_selected(tab->room);
   char command[16];
   g_snprintf(command, sizeof(command), "!vfo %c", choice->vfo);
   dict *request = dict_new();
   if (!request) return;
   dict_add(request, "msg.type", "talk");
   dict_add(request, "talk.cmd", "msg");
   dict_add(request, "talk.msg_type", "pub");
   dict_add(request, "talk.target", tab->room);
   dict_add(request, "talk.data", command);
   ws_send_dict(NULL, ws_conn, request, WEBSOCKET_OP_TEXT);
   dict_free(request);
}

typedef struct {
   GtkWidget *menu;
   GtkRoomTab *tab;
   unsigned count;
} locator_menu_build_t;

static void locator_menu_room_context(void *data) {
   locator_menu_build_t *build = data;
   const char *room = build->tab->room;
   const char *bindings = rrclient_room_vfos(room);
   uint32_t mask = rrclient_room_vfo_mask(room);
   if ((!bindings || !*bindings) && !mask) return;
   /* VFO mask is authoritative for the actual room mapping. */
   if (!mask && bindings) {
      char *copy = g_strdup(bindings);
      char *save = NULL;
      for (char *part = strtok_r(copy, " \t", &save); part; part = strtok_r(NULL, " \t", &save)) {
         const char *dot = strrchr(part, '.');
         if (dot && g_ascii_strncasecmp(dot + 1, "vfo_", 4) == 0 && dot[5]) {
            char vfo = g_ascii_toupper(dot[5]);
            if (vfo >= 'A' && vfo <= 'Z') mask |= UINT32_C(1) << (vfo - 'A');
         }
      }
      g_free(copy);
   }
   for (unsigned i = 0; i < 26; ++i) {
      if (!(mask & (UINT32_C(1) << i))) continue;
      const char *short_room = room[0] == '#' ? room + 1 : room;
      char title[192];
      g_strlcpy(title, short_room, sizeof(title));
      char *rig = strstr(title, "-rig");
      if (rig) *rig = '.';
      char name[224];
      g_snprintf(name, sizeof(name), "%s.%c", title, 'A' + i);
      GtkWidget *item = gtk_menu_item_new_with_label(name);
      locator_choice_t *choice = g_new0(locator_choice_t, 1);
      choice->tab = build->tab;
      choice->vfo = 'A' + i;
      g_object_set_data_full(G_OBJECT(item), "rr-locator-choice", choice, g_free);
      g_signal_connect(item, "activate", G_CALLBACK(locator_choice_activate), NULL);
      gtk_menu_shell_append(GTK_MENU_SHELL(build->menu), item);
      build->count++;
   }
}

static gboolean vfo_locator_menu_click(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)widget;
   (void)unused;
   if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_PRIMARY) return FALSE;
   GtkWidget *menu = gtk_menu_new();
   locator_menu_build_t build = { .menu = menu };
   if (room_tabs) {
      GHashTableIter iter;
      gpointer value;
      g_hash_table_iter_init(&iter, room_tabs);
      while (g_hash_table_iter_next(&iter, NULL, &value)) {
         GtkRoomTab *tab = value;
         if (!tab || !tab->room[0] || !tab->server[0]) continue;
         rrconn_t *connection = rrclient_connection_find(tab->server);
         if (!connection || !connection->is_ws || rrclient_connection_state(tab->server) != 1) continue;
         build.tab = tab;
         rrclient_connection_dispatch(connection, locator_menu_room_context, &build);
      }
   }
   if (!build.count) {
      GtkWidget *empty = gtk_menu_item_new_with_label("No joined rig VFOs");
      gtk_widget_set_sensitive(empty, FALSE);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), empty);
   }
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
   return TRUE;
}

/* E-STOP is a separate top-level bound to the room/profile selected when
 * summoned. Changing tabs cannot redirect an already open emergency switch. */
static GtkWidget *estop_window, *estop_status;
static char estop_room[128], estop_profile[512];

static void estop_send_in_context(void *action_ptr) {
   const char *action = action_ptr;
   if (!ws_conn) return;
   dict *request = dict_new();
   if (!request) return;
   dict_add(request, "msg.type", "talk");
   dict_add(request, "talk.cmd", "admin");
   dict_add(request, "talk.action", action);
   dict_add(request, "talk.target", estop_room);
   ws_send_dict(NULL, ws_conn, request, WEBSOCKET_OP_TEXT);
   dict_free(request);
}

static void estop_dispatch(const char *action) {
   rrconn_t *connection = rrclient_connection_find(estop_profile);
   if (!connection || !connection->is_ws || rrclient_connection_state(estop_profile) != 1) {
      if (estop_status) gtk_label_set_text(GTK_LABEL(estop_status), "OFFLINE — NOT CONFIRMED");
      return;
   }
   if (estop_status) gtk_label_set_text(GTK_LABEL(estop_status), "Request sent — check server acknowledgment");
   rrclient_connection_dispatch(connection, estop_send_in_context, (void *)action);
}

static void estop_pressed(GtkButton *button, gpointer unused) {
   (void)button; (void)unused;
   estop_dispatch("estop-trip");
}

static void estop_request_event(const char *event, const char *action, rrconn_t *client, void *user) {
   (void)event; (void)client; (void)user;
   if (!action) return;
   if (!strcasecmp(action, "off")) {
      if (estop_window) gtk_widget_hide(estop_window);
      return;
   }
   if (!strcasecmp(action, "reset")) {
      if (!estop_room[0]) {
         ui_print(ui_active_window_name(), "Open /estop on for the target rig before resetting");
         return;
      }
      estop_dispatch("estop-reset");
      return;
   }
   if (strcasecmp(action, "on")) return;
   const char *room = rrclient_media_active_room();
   const char *profile = rrclient_selected_server();
   if (!room || !strstr(room, "-rig") || !profile || !*profile) {
      ui_print(ui_active_window_name(), "Select a rig room before /estop on");
      return;
   }
   if (estop_window && gtk_widget_get_visible(estop_window) &&
       (strcmp(estop_room, room) || strcmp(estop_profile, profile))) {
      ui_print(ui_active_window_name(), "Close the existing E-STOP window before selecting another rig");
      return;
   }
   g_strlcpy(estop_room, room, sizeof(estop_room));
   g_strlcpy(estop_profile, profile, sizeof(estop_profile));
   if (!estop_window) {
      estop_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_title(GTK_WINDOW(estop_window), "RustyRig Emergency Stop");
      gtk_window_set_decorated(GTK_WINDOW(estop_window), FALSE);
      gtk_window_set_keep_above(GTK_WINDOW(estop_window), TRUE);
      gtk_window_set_skip_taskbar_hint(GTK_WINDOW(estop_window), TRUE);
      gtk_window_set_resizable(GTK_WINDOW(estop_window), FALSE);
      gtk_window_set_type_hint(GTK_WINDOW(estop_window), GDK_WINDOW_TYPE_HINT_NORMAL);
      g_signal_connect(estop_window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
      GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 9);
      gtk_container_set_border_width(GTK_CONTAINER(container), 15);
      gtk_container_add(GTK_CONTAINER(estop_window), container);
      GtkWidget *grid_label = gtk_label_new("Grid unknown");
      GtkWidget *rig_label = gtk_label_new("");
      g_object_set_data(G_OBJECT(estop_window), "estop-grid", grid_label);
      g_object_set_data(G_OBJECT(estop_window), "estop-rig", rig_label);
      gtk_box_pack_start(GTK_BOX(container), grid_label, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(container), rig_label, FALSE, FALSE, 0);
      GtkWidget *button = gtk_button_new_with_label("ESTOP");
      gtk_widget_set_name(button, "rr-emergency-stop");
      gtk_widget_set_size_request(button, 200, 200);
      gtk_box_pack_start(GTK_BOX(container), button, FALSE, FALSE, 0);
      g_signal_connect(button, "clicked", G_CALLBACK(estop_pressed), NULL);
      estop_status = gtk_label_new("Ready — not halted");
      gtk_box_pack_start(GTK_BOX(container), estop_status, FALSE, FALSE, 0);
      GtkCssProvider *css = gtk_css_provider_new();
      gtk_css_provider_load_from_data(css,
         "#rr-emergency-stop { background: #ba101b; color: white; font-size: 34px; font-weight: bold; border: 5px solid #ff515b; border-radius: 105px; min-width: 190px; min-height: 190px; }"
         "#rr-emergency-stop:hover { background: #e01826; }", -1, NULL);
      gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(estop_window),
         GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
      g_object_unref(css);
   }
   char *display = g_strdup(estop_room[0] == '#' ? estop_room + 1 : estop_room);
   gtk_label_set_text(GTK_LABEL(g_object_get_data(G_OBJECT(estop_window), "estop-rig")), display);
   g_free(display);
   rr_location_t position;
   const char *coordinates = rrclient_media_rig_coordinates(estop_room);
   const char *grid = rr_location_resolve(coordinates, NULL, &position) ? position.locator : NULL;
   gtk_label_set_text(GTK_LABEL(g_object_get_data(G_OBJECT(estop_window), "estop-grid")),
      grid && *grid ? grid : "Grid unknown");
   gtk_label_set_text(GTK_LABEL(estop_status), "Ready — not halted");
   gtk_widget_show_all(estop_window);
   gtk_window_present(GTK_WINDOW(estop_window));
}

/* Separate, fixed-size always-on-top VFO locator. It is deliberately not a
 * child of the fullscreen client and exists only while a server is online. */
static GtkWidget *vfo_locator_window, *vfo_locator_frame, *vfo_locator_label, *vfo_locator_frequency, *vfo_locator_mode, *vfo_locator_ptt;
static char locator_tx_identity[256];
static gint64 locator_tx_started;
/* Drag the undecorated top-level using GTK's native window-manager move.
 * The handler runs on the event box covering all three locator rows. */
static gboolean vfo_locator_drag(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)widget;
   (void)unused;
   if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_PRIMARY ||
       !vfo_locator_window) return FALSE;
   gtk_window_begin_move_drag(GTK_WINDOW(vfo_locator_window), event->button,
      (gint)event->x_root, (gint)event->y_root, event->time);
   return TRUE;
}
/* The locator tracks a rig in its owning connection context, not whichever
 * chat/IRC tab happens to be displayed in the main notebook. */
static char vfo_locator_profile[512];
static void vfo_locator_probe_room(void *result) {
   const char *room = rrclient_media_active_room();
   *(gboolean *)result = room && *room && rrclient_media_vfo_uuid(room, vfo_state_get_active());
}
static gboolean vfo_locator_tick_context(gpointer unused) {
   (void)unused;
   if (!main_notebook) return G_SOURCE_REMOVE;
   const char *profile = (const char *)unused;
   rrconn_t *connection = profile ? rrclient_connection_find(profile) : NULL;
   /* IRC and disconnected profiles have no rig/VFO state. Never present
    * cached data from another server as though it belonged to this one. */
   gboolean selected_online = connection && connection->is_ws &&
      rrclient_connection_state(profile) == 1;
   if (!selected_online) {
      locator_tx_identity[0] = '\0';
      locator_tx_started = 0;
      if (vfo_locator_window) gtk_widget_hide(vfo_locator_window);
      return G_SOURCE_CONTINUE;
   }
   if (!vfo_locator_window) {
      vfo_locator_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_title(GTK_WINDOW(vfo_locator_window), "RustyRig VFO");
      gtk_window_set_decorated(GTK_WINDOW(vfo_locator_window), FALSE);
      /* An independent normal top-level avoids window-manager utility/transient
       * grouping that can iconify it along with the main window. */
      gtk_window_set_type_hint(GTK_WINDOW(vfo_locator_window), GDK_WINDOW_TYPE_HINT_NORMAL);
      gtk_window_set_resizable(GTK_WINDOW(vfo_locator_window), FALSE);
      gtk_window_set_keep_above(GTK_WINDOW(vfo_locator_window), TRUE);
      gtk_window_set_skip_taskbar_hint(GTK_WINDOW(vfo_locator_window), TRUE);
      /* A native undecorated top-level, with the same lightweight border
       * treatment as in-window floating panels. The frame owns PTT styling. */
      vfo_locator_frame = gtk_event_box_new();
      gtk_widget_add_events(vfo_locator_frame, GDK_BUTTON_PRESS_MASK);
      g_signal_connect(vfo_locator_frame, "button-press-event",
         G_CALLBACK(vfo_locator_drag), NULL);
      gtk_widget_set_name(vfo_locator_frame, "rr-vfo-locator");
      GtkWidget *lines = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
      gtk_widget_set_margin_top(lines, 11);
      gtk_widget_set_margin_bottom(lines, 11);
      gtk_widget_set_margin_start(lines, 14);
      gtk_widget_set_margin_end(lines, 14);
      vfo_locator_label = gtk_label_new(NULL);
      vfo_locator_frequency = gtk_label_new(NULL);
      vfo_locator_mode = gtk_label_new(NULL);
      vfo_locator_ptt = gtk_label_new("PTT Off");
      gtk_label_set_xalign(GTK_LABEL(vfo_locator_label), 0);
      gtk_label_set_xalign(GTK_LABEL(vfo_locator_frequency), 0);
      gtk_label_set_xalign(GTK_LABEL(vfo_locator_mode), 0);
      gtk_label_set_xalign(GTK_LABEL(vfo_locator_ptt), 0);
      GtkWidget *locator_name_hit = gtk_event_box_new();
      gtk_widget_add_events(locator_name_hit, GDK_BUTTON_PRESS_MASK);
      gtk_widget_set_tooltip_text(locator_name_hit, "Select a joined rig and VFO");
      gtk_container_add(GTK_CONTAINER(locator_name_hit), vfo_locator_label);
      g_signal_connect(locator_name_hit, "button-press-event",
         G_CALLBACK(vfo_locator_menu_click), NULL);
      gtk_box_pack_start(GTK_BOX(lines), locator_name_hit, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(lines), vfo_locator_frequency, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(lines), vfo_locator_mode, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(lines), vfo_locator_ptt, FALSE, FALSE, 0);
      gtk_container_add(GTK_CONTAINER(vfo_locator_frame), lines);
      gtk_container_add(GTK_CONTAINER(vfo_locator_window), vfo_locator_frame);
      GtkCssProvider *provider = gtk_css_provider_new();
      gtk_css_provider_load_from_data(provider,
         "#rr-vfo-locator { background-color: #20252b; border: 1px solid #687480; border-radius: 7px; padding: 8px 12px; } "
         "#rr-vfo-locator label { color: #f0f2f4; } "
         "#rr-vfo-locator.rr-vfo-ptt { background-color: #b00020; border-color: #ff6677; } "
         "#rr-vfo-locator.rr-vfo-ptt label { color: white; }", -1, NULL);
      gtk_style_context_add_provider_for_screen(
         gtk_widget_get_screen(vfo_locator_window), GTK_STYLE_PROVIDER(provider),
         GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
      g_object_unref(provider);
      g_signal_connect(vfo_locator_window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
   }
   char vfo[2] = { vfo_state_get_active(), 0 };
   const char *room = rrclient_media_active_room();
   const char *grid = NULL;
   const char *vfo_uuid = rrclient_media_vfo_uuid(room, vfo[0]);
   if (vfo_uuid) {
      const dict *location = rrclient_object_property(vfo_uuid, "gridsquare");
      if (!location) location = rrclient_object_property(vfo_uuid, "grid");
      if (location && dict_get_bool((dict *)location, "property.known", false) &&
          dict_get_bool((dict *)location, "property.available", false))
         grid = dict_get((dict *)location, "property.value", NULL);
   }
   if (!grid || !*grid) grid = vfo_state_get(vfo, "cat.state.gridsquare", NULL);
   if (!grid || !*grid) grid = vfo_state_get(vfo, "cat.state.grid", NULL);
   /* Grid metadata may belong to the rig's GPS stream, not the VFO.
    * Use the same cached coordinates that Resource Explorer receives. */
   rr_location_t gps_location;
   if ((!grid || !*grid) &&
       rr_location_resolve(rrclient_media_rig_coordinates(room), NULL, &gps_location))
      grid = gps_location.locator;
   char path[256];
   if (room && *room) {
      /* Rig rooms are named #site-rigN. Do not invent a rig when the
       * active room is an ordinary chat or unscoped receiver room. */
      const char *rig = strstr(room, "-rig");
      if (rig && rig[4] >= '0' && rig[4] <= '9') {
         g_snprintf(path, sizeof(path), "%.*s.%s.%c",
            (int)(rig - room - (room[0] == '#')), room + (room[0] == '#'),
            rig + 1, vfo[0]);
      } else {
         g_snprintf(path, sizeof(path), "%s.%c", room, vfo[0]);
      }
   } else {
      g_snprintf(path, sizeof(path), "%s.%c", profile, vfo[0]);
   }
   char label[360];
   g_snprintf(label, sizeof(label), "%s  /  %s", path, grid && *grid ? grid : "—");
   gtk_label_set_text(GTK_LABEL(vfo_locator_label), label);
   const char *mode = selected_online ? vfo_state_get(vfo, "cat.state.mode", NULL) : NULL;
   long width = selected_online ? vfo_state_get_long(vfo, "cat.state.width", 0) : 0;
   long frequency = selected_online ? vfo_state_get_long(vfo, "cat.state.freq", 0) : 0;
   /* CAT PTT is a hardware-wide observation on some single-RX rigs. It can
    * be cached under whichever VFO was active when polled, so it is not
    * sufficient evidence that this room/VFO is transmitting. */
   gboolean ptt = FALSE;
   char formatted[64], freq_line[96], mode_line[96];
   if (frequency > 0) {
      /* Hz -> kHz with exactly three decimals and fixed comma grouping. */
      unsigned long khz = (unsigned long)(frequency / 1000);
      unsigned long hz = (unsigned long)(frequency % 1000);
      char digits[40];
      g_snprintf(digits, sizeof(digits), "%lu", khz);
      size_t len = strlen(digits), offset = 0;
      for (size_t i = 0; i < len && offset + 2 < sizeof(formatted); ++i) {
         if (i && (len - i) % 3 == 0) formatted[offset++] = ',';
         formatted[offset++] = digits[i];
      }
      formatted[offset] = '\0';
      g_snprintf(freq_line, sizeof(freq_line), "Frequency: %s.%03lu kHz", formatted, hz);
   } else {
      g_strlcpy(freq_line, "Frequency: —", sizeof(freq_line));
   }
   g_snprintf(mode_line, sizeof(mode_line), "Mode: %s   Width: %s",
      mode && *mode ? mode : "—", width > 0 ? "" : "—");
   if (width > 0)
      g_snprintf(mode_line, sizeof(mode_line), "Mode: %s   Width: %ld Hz", mode && *mode ? mode : "—", width);
   gtk_label_set_text(GTK_LABEL(vfo_locator_frequency), freq_line);
   gtk_label_set_text(GTK_LABEL(vfo_locator_mode), mode_line);
   /* This elapsed time begins when this client observes the TX holder;
    * reconnecting during a transmission does not reconstruct server time. */
   const struct rr_user *holder = NULL;
   if (selected_online) {
      for (const struct rr_user *u = global_userlist; u; u = u->next) {
         if (!u->is_ptt || u->ptt_vfo != vfo[0]) continue;
         if (!room || !*room || !u->ptt_room[0] || strcasecmp(u->ptt_room, room)) continue;
         holder = u;
         break;
      }
   }
   /* CAT PTT echoes carry cat.user independently of the user directory.
    * The userlist can lag behind the PTT state, especially across relays.
    * Prefer its room-scoped match, but fall back to the CAT holder identity. */
   /* The room/VFO-qualified TX holder is authoritative for the locator.
    * Do not reuse a stale CAT user/PTT flag from another VFO. */
   ptt = holder != NULL;
   const char *holder_name = holder ? holder->name : NULL;
   if (holder_name && !*holder_name) holder_name = NULL;
   char holder_id[256] = "";
   if (ptt && holder_name)
      g_snprintf(holder_id, sizeof(holder_id), "%s/%s/%c", profile,
         holder_name, vfo[0]);
   gint64 current_us = g_get_monotonic_time();
   if (strcmp(holder_id, locator_tx_identity)) {
      g_strlcpy(locator_tx_identity, holder_id, sizeof(locator_tx_identity));
      locator_tx_started = ptt && holder_name ? current_us : 0;
   }
   /* Requested RF output setpoint from the owner, not Hamlib's RX
    * strength or a measured forward-power meter reading. */
   const char *watts_text = selected_online ?
      vfo_state_get(vfo, "cat.state.tx-power-watts", NULL) : NULL;
   double watts = watts_text ? g_ascii_strtod(watts_text, NULL) : 0;
   char watt_label[64] = "";
   if (watts > 0 && isfinite(watts)) {
      if (watts < 1.0)
         g_snprintf(watt_label, sizeof(watt_label), "  %.3g mW", watts * 1000.0);
      else
         g_snprintf(watt_label, sizeof(watt_label), "  %.3g W", watts);
   }
   char ptt_line[256];
   if (ptt && holder_name) {
      unsigned long elapsed = locator_tx_started && current_us >= locator_tx_started ?
         (unsigned long)((current_us - locator_tx_started) / G_USEC_PER_SEC) : 0;
      g_snprintf(ptt_line, sizeof(ptt_line), "TX: %s (%lu s)%s", holder_name, elapsed, watt_label);
   } else if (ptt) {
      g_snprintf(ptt_line, sizeof(ptt_line), "PTT On (holder unknown)%s", watt_label);
   } else {
      g_strlcpy(ptt_line, "PTT Off", sizeof(ptt_line));
   }
   gtk_label_set_text(GTK_LABEL(vfo_locator_ptt), ptt_line);
   ptt = ptt || holder != NULL;
   GtkStyleContext *style = gtk_widget_get_style_context(vfo_locator_frame);
   if (ptt) gtk_style_context_add_class(style, "rr-vfo-ptt");
   else gtk_style_context_remove_class(style, "rr-vfo-ptt");
   gtk_widget_show_all(vfo_locator_window);
   return G_SOURCE_CONTINUE;
}

static void vfo_locator_render_context(void *unused) {
   vfo_locator_tick_context(vfo_locator_profile);
}
static gboolean vfo_locator_tick(gpointer unused) {
   (void)unused;
   if (!main_notebook) return G_SOURCE_REMOVE;
   const char *selected = rrclient_selected_server();
   const char *candidate = NULL;
   /* Prefer the currently selected rig, then continue tracking the last
    * rig while the user visits IRC/status tabs on another connection. */
   for (unsigned pass = 0; pass < 3 && !candidate; ++pass) {
      for (unsigned i = 0; ; ++i) {
         const char *profile = pass == 0 ? selected :
            pass == 1 ? vfo_locator_profile : rrclient_connection_iter(i);
         if (!profile || !*profile) break;
         rrconn_t *connection = rrclient_connection_find(profile);
         if (connection && connection->is_ws && rrclient_connection_state(profile) == 1) {
            gboolean rig_active = FALSE;
            rrclient_connection_dispatch(connection, vfo_locator_probe_room, &rig_active);
            if (rig_active) {
               candidate = profile;
               break;
            }
         }
         if (pass != 2) break;
      }
   }
   if (!candidate) {
      vfo_locator_profile[0] = '\0';
      locator_tx_identity[0] = '\0';
      locator_tx_started = 0;
      if (vfo_locator_window) gtk_widget_hide(vfo_locator_window);
      return G_SOURCE_CONTINUE;
   }
   g_strlcpy(vfo_locator_profile, candidate, sizeof(vfo_locator_profile));
   rrclient_connection_dispatch(rrclient_connection_find(candidate),
      vfo_locator_render_context, NULL);
   return G_SOURCE_CONTINUE;
}

static GtkWidget *shared_tab_context;
static void shared_tab_context_changed(GtkNotebook *notebook, GtkWidget *page,
   guint page_num, gpointer unused) {
   (void)page_num;
   (void)unused;
   if (!shared_tab_context || !page) return;
   GtkWidget *title = gtk_notebook_get_tab_label(notebook, page);
   const char *name = GTK_IS_LABEL(title) ? gtk_label_get_text(GTK_LABEL(title)) : NULL;
   gtk_label_set_text(GTK_LABEL(shared_tab_context), name && *name ? name : "Status");
}
/* Compact per-selected-server transport rates. Counts wire RX/TX for both
 * IRC and WebSocket; the selection is not tied to the current chat tab. */
static GtkWidget *status_network_label, *status_server_label;
static gboolean status_network_tick(gpointer unused) {
   (void)unused;
   if (!status_network_label || !status_server_label) return G_SOURCE_REMOVE;
   static uint64_t previous_rx, previous_tx, previous_rtp_rx, previous_rtp_tx;
   static gint64 previous_time;
   static char previous_server[512];
   const char *server = rrclient_selected_server();
   gboolean online = server && rrclient_connection_state(server) == 1;
   uint64_t rx = 0, tx = 0, rtp_rx = 0, rtp_tx = 0;
   gboolean known = server && rrclient_connection_traffic(server, &rx, &tx);
   rrconn_t *connection = server ? rrclient_connection_find(server) : NULL;
   gboolean rtp_active = connection && rrclient_direct_traffic(connection, &rtp_rx, &rtp_tx);
   gtk_label_set_text(GTK_LABEL(status_server_label), server && *server ? server : "No server");
   gtk_widget_set_tooltip_text(status_server_label, server && *server ? server : "No server selected");
   gint64 now_us = g_get_monotonic_time();
   if (!known || !online) {
      gtk_label_set_text(GTK_LABEL(status_network_label), "RX —  TX —");
      previous_time = 0;
      previous_server[0] = '\0';
      return G_SOURCE_CONTINUE;
   }
   if (!previous_time || strcmp(previous_server, server) ||
       now_us <= previous_time || rx < previous_rx || tx < previous_tx ||
       rtp_rx < previous_rtp_rx || rtp_tx < previous_rtp_tx) {
      g_strlcpy(previous_server, server, sizeof(previous_server));
      previous_rx = rx;
      previous_tx = tx;
      previous_rtp_rx = rtp_rx;
      previous_rtp_tx = rtp_tx;
      previous_time = now_us;
      gtk_label_set_text(GTK_LABEL(status_network_label),
         rtp_active ? "RX 0.0 (0.0)  TX 0.0 (0.0) kb/s" : "RX 0.0  TX 0.0 kb/s");
      return G_SOURCE_CONTINUE;
   }
   double scale = 8.0 * 1000000.0 / (1000.0 * (now_us - previous_time));
   char label[160];
   if (rtp_active) {
      g_snprintf(label, sizeof(label), "RX %.1f (%.1f)  TX %.1f (%.1f) kb/s",
         (rx - previous_rx) * scale, (rtp_rx - previous_rtp_rx) * scale,
         (tx - previous_tx) * scale, (rtp_tx - previous_rtp_tx) * scale);
   } else {
      g_snprintf(label, sizeof(label), "RX %.1f  TX %.1f kb/s",
         (rx - previous_rx) * scale, (tx - previous_tx) * scale);
   }
   gtk_label_set_text(GTK_LABEL(status_network_label), label);
   previous_rx = rx;
   previous_tx = tx;
   previous_rtp_rx = rtp_rx;
   previous_rtp_tx = rtp_tx;
   previous_time = now_us;
   return G_SOURCE_CONTINUE;
}
bool chat_init(void) {
   typing_shutting_down = FALSE;
   extern bool rr_module_token_add(rr_event_token_t token);
   rr_module_token_add(event_on_token("client.server.selected", gtk_chat_server_selected, NULL));
   rr_module_token_add(event_on_token("client.estop", estop_request_event, NULL));
   room_tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   status_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *status_tab_label = gtk_label_new(NULL);
   char tab_desc[64];
   memset(tab_desc, 0, sizeof(tab_desc));
   snprintf(tab_desc, sizeof(tab_desc), "(<u>%d</u>) status", next_chat_tab);
   gtk_label_set_markup(GTK_LABEL(status_tab_label), tab_desc);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), status_tab, status_tab_label);
   input_history = g_ptr_array_new_with_free_func(g_free);
   g_object_set_data_full(G_OBJECT(main_notebook), "rr-shared-input-history", input_history, (GDestroyNotify) g_ptr_array_unref);

   status_room_tab = g_new0(GtkRoomTab, 1);
   status_room_tab->page = status_tab;
   g_object_set_data(G_OBJECT(status_tab), "rr-room-tab", status_room_tab);
   /* Status keeps its own command input and output buffer after login. The authoritative lobby is created as a separate room tab. */
   GtkWidget *status_box = create_chat_box_for_room(false, NULL, false);

   if (status_box) {
      gtk_box_pack_start(GTK_BOX(status_tab), status_box, TRUE, TRUE, 0);
      status_room_tab->view = chat_textview;
      status_room_tab->entry = chat_entry;
   }
   shared_tab_context = gtk_label_new("Status");
   gtk_widget_set_tooltip_text(shared_tab_context, "Current tab; reserved for tab-specific status");
   gtk_label_set_ellipsize(GTK_LABEL(shared_tab_context), PANGO_ELLIPSIZE_END);
   gtk_label_set_max_width_chars(GTK_LABEL(shared_tab_context), 12);
   gtk_widget_set_hexpand(shared_tab_context, FALSE);
   gtk_box_pack_start(GTK_BOX(rr_main_toolbar), shared_tab_context, FALSE, FALSE, 5);
   g_signal_connect(main_notebook, "switch-page", G_CALLBACK(shared_tab_context_changed), NULL);
   GtkWidget *network_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   status_server_label = gtk_label_new("No server");
   gtk_label_set_ellipsize(GTK_LABEL(status_server_label), PANGO_ELLIPSIZE_MIDDLE);
   gtk_label_set_max_width_chars(GTK_LABEL(status_server_label), 12);
   gtk_label_set_width_chars(GTK_LABEL(status_server_label), 0);
   /* Server identity belongs to the normal toolbar; keep the overlay's
    * right-hand edge exclusively for RX/TX so the TX value cannot be
    * pushed out of the visible window by the server name. */
   gtk_box_pack_start(GTK_BOX(rr_main_toolbar), status_server_label, FALSE, FALSE, 2);
   status_network_label = gtk_label_new("RX —  TX —");
   gtk_widget_set_tooltip_text(status_network_label,
      "Selected server RX/TX kilobits/s sampled every second. Parenthesized values are RTP media rates when active; primary values are IRC/WebSocket traffic.");
   gtk_widget_set_halign(status_network_label, GTK_ALIGN_START);
   gtk_box_pack_start(GTK_BOX(network_row), status_network_label, FALSE, FALSE, 0);
   gtk_widget_set_halign(status_network_label, GTK_ALIGN_END);
   /* Keep the counters visible: shorten the server name and the optional
    * room title first, never chop off the TX rate at the right edge. */
   /* This label lives on an overlay; its natural width must not enlarge
    * the overlay beyond the visible window. Keep the rightmost TX figures
    * readable when RTP adds parenthesized rate figures. */
   gtk_label_set_ellipsize(GTK_LABEL(status_network_label), PANGO_ELLIPSIZE_START);
   gtk_label_set_max_width_chars(GTK_LABEL(status_network_label), 33);
   gtk_label_set_xalign(GTK_LABEL(status_network_label), 1.0);
   gtk_widget_set_hexpand(network_row, FALSE);
   gtk_widget_set_margin_end(network_row, 2);
   /* The entire notebook lives inside a non-horizontally-scrollable viewport.
    * A wide notebook page can therefore push the toolbar's end past the
    * visible window. Anchor network status to the window-sized overlay
    * instead, not the potentially oversized scrolled content. */
   GtkWidget *window_overlay = gtk_bin_get_child(GTK_BIN(main_window));
   gtk_widget_set_halign(network_row, GTK_ALIGN_END);
   gtk_widget_set_valign(network_row, GTK_ALIGN_START);
   gtk_widget_set_margin_top(network_row, 3);
   gtk_widget_set_margin_end(network_row, 2);
   if (GTK_IS_OVERLAY(window_overlay)) {
      gtk_overlay_add_overlay(GTK_OVERLAY(window_overlay), network_row);
      gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(window_overlay), network_row, TRUE);
      gtk_widget_show_all(network_row);
   } else {
      gtk_box_pack_end(GTK_BOX(rr_main_toolbar), network_row, FALSE, FALSE, 0);
   }
   g_object_add_weak_pointer(G_OBJECT(status_network_label), (gpointer *) &status_network_label);
   g_timeout_add_seconds(1, status_network_tick, NULL);
   status_network_tick(NULL);
   g_timeout_add_seconds(1, vfo_locator_tick, NULL);
   vfo_locator_tick(NULL);
   g_signal_connect_after(main_notebook, "switch-page", G_CALLBACK(gtk_chat_select_tab), NULL);
   userlist_redraw_gtk();

   return false;
}

static void gtk_chat_parse_origin(void *message) {
   parse_chat_input_real(message);
}

struct gtk_chat_input_check {
   const char *text;
   bool blocked;
};
static void gtk_chat_check_origin(void *data) {
   struct gtk_chat_input_check *check = data;
   check->blocked = rrclient_chat_input_blocked(check->text);
}

bool parse_chat_input_gtk(GtkButton *button, gpointer entry) {
   const char *profile = g_object_get_data(G_OBJECT(entry), "rr-server-name");

   if (profile && *profile) {
      rrclient_connection_select(profile);
   }
   const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
   const char *entry_room = g_object_get_data(G_OBJECT(entry), "rr-room-name");
   rrconn_t *connection = profile && *profile ? rrclient_connection_find(profile) : rrclient_active_connection();
   struct gtk_chat_input_check check = {
      .text = text
   };

   if (entry_room) {
      rrclient_input_dispatch(connection, *entry_room ? entry_room : NULL, gtk_chat_check_origin, &check);
   } else {
      gtk_chat_check_origin(&check);
   }

   if (check.blocked) {
      ui_print(entry_room ? entry_room : gtk_chat_current_room(), "Room is OFFLINE; draft retained");

      return true;
   }

   if (text && *text) {
      // Copy before clearing/dispatch: commands may change tabs or destroy
      // the originating entry. Never retain GtkEntry's borrowed text pointer.
      char *message = gtk_chat_serialize_entry(GTK_WIDGET(entry));
      GPtrArray *lines;
      GtkInputHistory *history = entry_history(GTK_WIDGET(entry), &lines);

      // PARITY: browser chat_history_add() in js/webui.chat.completion.js;
      // TUI history is owned by librustyaxe/tui.keys.c. Shared is the default.
      if (lines && (!lines->len || strcmp(g_ptr_array_index(lines, lines->len - 1), text))) {
         if (lines->len >= HISTORY_LINES) {
            g_ptr_array_remove_index(lines, 0);
         }
         g_ptr_array_add(lines, g_strdup(message));
      }
      history->index = -1;
      g_clear_pointer(&history->draft, g_free);
      gtk_entry_set_text(GTK_ENTRY(entry), "");
      GtkFormatState *format = gtk_chat_format_state(GTK_WIDGET(entry));
      g_ptr_array_set_size(format->undo, 0);
      g_ptr_array_set_size(format->redo, 0);
      g_object_set_data(G_OBJECT(entry), "rr-color-active", NULL);
      gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(entry), "rr-color-popover")));
      /* The entry owns the destination. Focus, incoming traffic and notebook reordering may change the selected page while its callback runs. */
      char *room = g_strdup(entry_room);

      if (entry_room) {
         rrclient_input_dispatch(connection, room && *room ? room : NULL, gtk_chat_parse_origin, message);
      } else {
         parse_chat_input_real(message);
      }
      g_free(room);
      g_free(message);
   }

   return false;
}
