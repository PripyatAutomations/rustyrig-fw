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
#include <rrclient/connman.h>
#include <rrclient/media.h>
#include <rrclient/rooms.h>
#include <rrclient/vfo.h>
#include <rrclient/gtk/ui.speech.h>
#include <rrclient/gtk/gtk.core.h>
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

typedef struct {
   GArray *positions;
   char *snapshot;
   int pending_position;
   bool loading;
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
   GtkWidget *page;
   GtkWidget *view;
   GtkWidget *entry;
} GtkRoomTab;

static GHashTable *room_tabs = NULL;
static GtkRoomTab *status_room_tab = NULL;
static GtkWidget *status_send_button;

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
}
int next_chat_tab = 5;


static GtkWidget *room_vfo_box;
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
      gtk_chat_update_vfo_controls(tab);
   }
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
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(chat_textview);
   GtkTextIter end;

   gtk_text_buffer_get_end_iter(buffer, &end);
   gtk_text_view_scroll_to_iter(chat_textview, &end, 0.0, TRUE, 0.0, 1.0);

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
            int idx = c * rows + r;   // column-major so matches read down each column

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
      g_object_set_data_full(G_OBJECT(entry), "rr-format-state", state, gtk_chat_format_state_free);
   }

   return state;
}

static gboolean gtk_chat_position_is_token(GtkFormatState *state, guint position) {
   for (guint i = 0 ; i < state->positions->len ; i++) {
      guint token_position = g_array_index(state->positions, guint, i);

      if (token_position == position) {
         return TRUE;
      }

      if (token_position > position) {
         break;
      }
   }

   return FALSE;
}

static void gtk_chat_apply_format_attributes(GtkWidget *entry, GtkFormatState *state) {
   PangoAttrList *attrs = pango_attr_list_new();

   for (guint i = 0 ; i < state->positions->len ; i++) {
      guint position = g_array_index(state->positions, guint, i);
      const char *p = g_utf8_offset_to_pointer(state->snapshot, position);

      if (!*p) {
         continue;
      }
      guint start = (guint)(p - state->snapshot);
      guint end = (guint)(g_utf8_next_char(p) - state->snapshot);
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

   for (unsigned int color = 0 ; color < 16 ; color++) {
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
      char *tip = backgrounds
            ? g_strdup_printf("%s on %s", gtk_mirc_color_name(foreground), name)
            : g_strdup_printf("%s (%u)", name, color);
      gtk_widget_set_tooltip_text(choice, tip);
      g_free(tip);
      g_object_set_data(G_OBJECT(choice), "rr-color-entry", entry);
      g_object_set_data(G_OBJECT(choice), "rr-color-value", GUINT_TO_POINTER(color));
      g_signal_connect(choice, "clicked", G_CALLBACK(gtk_chat_color_choice), entry);
      gtk_grid_attach(GTK_GRID(grid), choice, color % 4, color / 4, 1, 1);
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

   if (!gtk_chat_position_is_token(state, (guint)start) ||
      gtk_formatting_control(g_utf8_get_char(p)) != 0x03) {
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

   if (comma && (foreground < 0 || foreground >= 16)) {
      goto hide_color_popup;
   }
   int previous_mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode"));
   int previous_foreground = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-foreground")) - 1;
   bool force_backgrounds = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-force-backgrounds"));
   int mode = comma ? 2 :
      (force_backgrounds || (previous_mode == 3 && previous_foreground == foreground) ? 3 : 1);
   g_object_set_data(G_OBJECT(entry), "rr-color-insert-position", GINT_TO_POINTER(cursor + 1));
   g_object_set_data(G_OBJECT(entry), "rr-color-mode", GINT_TO_POINTER(mode));
   g_object_set_data(G_OBJECT(entry), "rr-color-foreground", GINT_TO_POINTER(foreground + 1));
   g_object_set_data(G_OBJECT(entry), "rr-color-force-backgrounds", NULL);

   if (previous_mode != mode || ((mode == 2 || mode == 3) && previous_foreground != foreground) ||
      force_backgrounds) {
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

static void gtk_chat_entry_changed(GtkEditable *editable, gpointer data) {
   (void)data;
   GtkWidget *entry = GTK_WIDGET(editable);
   GtkFormatState *state = gtk_chat_format_state(entry);
   const char *text = gtk_entry_get_text(GTK_ENTRY(entry));

   if (!state->loading) {
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
      guint old_count = (guint)g_utf8_strlen(old, -1);
      guint new_count = (guint)g_utf8_strlen(text, -1);
      guint old_after = old_count - suffix;
      gint delta = (gint)new_count - (gint)old_count;

      for (guint i = 0 ; i < state->positions->len ; ) {
         guint *position = &g_array_index(state->positions, guint, i);

         if (*position >= old_after) {
            *position = (guint)((gint) * position + delta);
            i++;
         } else if (*position >= prefix) {
            g_array_remove_index(state->positions, i);
         } else {
            i++;
         }
      }

      if (state->pending_position >= 0) {
         guint pending = (guint)state->pending_position;
         g_array_append_val(state->positions, pending);
         state->pending_position = -1;
         g_array_sort(state->positions, (GCompareFunc)gtk_chat_uint_compare);
      }
      g_free(state->snapshot);
      state->snapshot = g_strdup(text);
   }
   gtk_chat_apply_format_attributes(entry, state);

   if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-active"))) {
      g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, gtk_chat_update_color_popup, g_object_ref(entry), g_object_unref);
   }
}

static void gtk_chat_color_choice(GtkButton *button, gpointer data) {
   GtkWidget *entry = GTK_WIDGET(data);
   unsigned int color = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "rr-color-value"));
   int mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode"));
   char number[5];
   snprintf(number, sizeof(number), mode == 3 ? ",%u" : "%u", color);
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
static gboolean on_chat_entry_keypress(GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
   if (!event || !entry) {
      return FALSE;
   }

   GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask();

   if ((modifiers & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) ==
      (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && !(modifiers & GDK_MOD1_MASK)) {
      guint8 style_code = 0;

      switch (event->keyval) {
         case GDK_KEY_u: case GDK_KEY_U: {
            style_code = 0x1f;
            break;
         }
         case GDK_KEY_s: case GDK_KEY_S: {
            style_code = 0x1e;
            break;
         }
         case GDK_KEY_m: case GDK_KEY_M: {
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
         case GDK_KEY_b: case GDK_KEY_B: {
            control_code = 0x02;
            break;                                                   // Bold
         }
         case GDK_KEY_c: case GDK_KEY_C: {
            control_code = 0x03;
            break;                                                   // Color
         }
         case GDK_KEY_i: case GDK_KEY_I: {
            control_code = 0x1d;
            break;                                                   // Italic
         }
         case GDK_KEY_o: case GDK_KEY_O: {
            control_code = 0x0f;
            break;                                                   // Reset
         }
         case GDK_KEY_u: case GDK_KEY_U: {
            gtk_entry_set_text(GTK_ENTRY(entry), "");

            return TRUE; // Ctrl-U clears input, matching the TUI
         }
         case GDK_KEY_r: case GDK_KEY_R: {
            control_code = 0x16;
            break;                                                   // Reverse
         }
      }

      if (control_code) {
         gtk_chat_insert_format(entry, control_code);

         return TRUE;
      }
   }

   if (event->keyval == GDK_KEY_Escape &&
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-active"))) {
      g_object_set_data(G_OBJECT(entry), "rr-color-active", NULL);
      gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(entry), "rr-color-popover")));

      return TRUE;
   }

   if (event->keyval == GDK_KEY_Tab && (modifiers & GDK_CONTROL_MASK) &&
      !(modifiers & (GDK_MOD1_MASK | GDK_SHIFT_MASK))) {
      return rrclient_connection_cycle_status(gtk_chat_status_active());
   }

   if (event->keyval == GDK_KEY_Tab) {
      gtk_chat_do_completion(GTK_ENTRY(entry));

      return TRUE;
   }

   if (event->keyval == GDK_KEY_Page_Up) {
      GtkAdjustment *adj =
         gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(chat_textview));

      gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) -
         gtk_adjustment_get_page_increment(adj));

      return TRUE;
   }

   if (event->keyval == GDK_KEY_Page_Down) {
      GtkAdjustment *adj =
         gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(chat_textview));

      gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) +
         gtk_adjustment_get_page_increment(adj));

      return TRUE;
   }

   if (event->keyval != GDK_KEY_Up && event->keyval != GDK_KEY_Down) {
      return FALSE;
   }

   if (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK)) {
      return FALSE;
   }
   GPtrArray *lines;
   GtkInputHistory *history = entry_history(entry, &lines);

   if (!lines || !lines->len) {
      return FALSE;
   }

   if (event->keyval == GDK_KEY_Up) {
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
   const char *text = history->index < 0 ? (history->draft ? history->draft : "") :
      g_ptr_array_index(lines, history->index);
   gtk_chat_set_serialized_entry(entry, text);

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
      if (room_vfo_box && cfg_get_bool("ui.gtk.vfo-docked", true)) {
         gtk_widget_hide(room_vfo_box);
      }

      return;
   }
   GtkWidget *box = g_object_get_data(G_OBJECT(tab->page), "rr-chat-box");

   if (!box) {
      return;
   }

   if (!room_vfo_box) {
      room_vfo_box = chatbox_vfo_init();

      if (!room_vfo_box) {
         return;
      }
      g_object_add_weak_pointer(G_OBJECT(room_vfo_box), (gpointer *)&room_vfo_box);
   }

   if (!cfg_get_bool("ui.gtk.vfo-docked", true)) {
      return;
   }
   GtkWidget *parent = gtk_widget_get_parent(room_vfo_box);

   if (parent != box) {
      g_object_ref_sink(room_vfo_box);

      if (parent) {
         gtk_container_remove(GTK_CONTAINER(parent), room_vfo_box);
      }
      gtk_box_pack_start(GTK_BOX(box), room_vfo_box, FALSE, FALSE, 0);

      if (cfg_ui_gtk_vfo_on_top) {
         gtk_box_reorder_child(GTK_BOX(box), room_vfo_box, 0);
      }
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

   if (!chat_box) {
      // XXX: throw OOM warning

      return NULL;
   }

   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(scrolled, -1, 200);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);


   // Chat view. Font (monospace) comes from the #chat-view CSS rule in [gtk-css]
   chat_textview = gtk_text_view_new();
   gtk_widget_set_name(chat_textview, "chat-view");
   text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_textview) );
   gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_textview), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chat_textview), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat_textview), GTK_WRAP_WORD_CHAR);
   gtk_container_add(GTK_CONTAINER(scrolled), chat_textview);

   /* Keep room user lists beside their chat. Private query tabs stay chat-only; GtkPaned gives room tabs a draggable divider, and the authoritative room
    * additionally supports the detachable list window. */
   if (is_rig || (!is_query && room && *room)) {
      GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
      gtk_box_pack_start(GTK_BOX(chat_box), paned, TRUE, TRUE, 0);
      gtk_paned_pack1(GTK_PANED(paned), scrolled, TRUE, FALSE);

      if (is_rig) {
         userlist_dock_into(GTK_PANED(paned));
      } else {
         userlist_dock_room_into(GTK_PANED(paned), room);
      }
   } else {
      gtk_box_pack_start(GTK_BOX(chat_box), scrolled, TRUE, TRUE, 0);
   }

   // Chat INPUT
   chat_entry = gtk_entry_new();
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

static void gtk_chat_tab_add(const char *room, bool is_query) {
   if (!room || !*room || !main_notebook) {
      return;
   }

   if (!room_tabs) {
      room_tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   }
   GtkRoomTab *existing = g_hash_table_lookup(room_tabs, rrclient_window_name(room));

   if (existing) {
      if (!rrclient_present_context()) {
         return;
      }
      gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), existing->page);

      if (page >= 0) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), page);
      }

      if (existing->entry && GTK_IS_WIDGET(existing->entry)) {
         gtk_widget_grab_focus(existing->entry);
      }

      return;
   }
   GtkRoomTab *tab = g_new0(GtkRoomTab, 1);
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
   GtkWidget *label = gtk_label_new(room);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), tab->page, label);
   g_hash_table_insert(room_tabs, g_strdup(rrclient_window_name(room)), tab);
   gtk_widget_show_all(tab->page);

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

void gtk_chat_room_add(const char *room) {
   gtk_chat_tab_add(room, false);
}

void gtk_chat_query_add(const char *user) {
   gtk_chat_tab_add(user, true);
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
   char line[640];
   snprintf(line, sizeof(line), "*** Topic for %s: %s", room, (topic && *topic) ? topic : "(none)");
   GtkTextIter end;
   gtk_text_buffer_get_end_iter(buffer, &end);
   gtk_text_buffer_insert(buffer, &end, line, -1);
   gtk_text_buffer_insert(buffer, &end, "\n", 1);
}

void gtk_chat_set_authoritative_room(const char *room) {
   if (!room || !*room || !main_notebook) {
      return;
   }
   /* Keep status independent of server rooms, including across reconnects. */
   gtk_chat_tab_add(room, false);
}

bool chat_init(void) {
   extern bool rr_module_token_add(rr_event_token_t token);
   rr_module_token_add(event_on_token("client.server.selected", gtk_chat_server_selected, NULL));
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
   g_signal_connect(main_notebook, "switch-page", G_CALLBACK(gtk_chat_select_tab), NULL);
   userlist_redraw_gtk();

   return false;
}

bool parse_chat_input_gtk(GtkButton *button, gpointer entry) {
   const char *profile = g_object_get_data(G_OBJECT(entry), "rr-server-name");

   if (profile && *profile) {
      rrclient_connection_select(profile);
   }
   const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));

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
      g_object_set_data(G_OBJECT(entry), "rr-color-active", NULL);
      gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(entry), "rr-color-popover")));
      parse_chat_input_real(message);
      g_free(message);
   }

   return false;
}
