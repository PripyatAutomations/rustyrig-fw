#include <assert.h>
#include "rrclient/gtk/gtk.core.c"
#include "rrclient/gtk/gtk.chat.c"
static char test_audio_route = 'S';
char audio_room_route(const char *server, const char *room) { return test_audio_route; }
bool audio_set_room_route(const char *server, const char *room, char output) {
   assert(!strcmp(server, "route-server") && !strcmp(room, "#route-room"));
   test_audio_route = output;
   return true;
}
static void check_audio_selector(void) {
   if (!gtk_init_check(NULL, NULL)) { puts("SKIP: GTK audio selector requires a display"); return; }
   GtkRoomTab tab = {.server = "route-server", .room = "#route-room"};
   tab.audio_route = gtk_combo_box_text_new();
   g_object_ref_sink(tab.audio_route);
   const char *outputs[] = {"L", "R", "S", "M"};
   for (unsigned i = 0; i < 4; i++) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(tab.audio_route), outputs[i], outputs[i]);
   gtk_combo_box_set_active_id(GTK_COMBO_BOX(tab.audio_route), "S");
   g_signal_connect(tab.audio_route, "changed", G_CALLBACK(gtk_chat_audio_route), &tab);
   gtk_combo_box_set_active_id(GTK_COMBO_BOX(tab.audio_route), "L");
   assert(test_audio_route == 'L');
   room_tabs = g_hash_table_new(g_str_hash, g_str_equal);
   g_hash_table_insert(room_tabs, "route", &tab);
   test_audio_route = 'M';
   gtk_chat_audio_routes_changed(NULL, NULL, NULL, NULL);
   assert(!strcmp(gtk_combo_box_get_active_id(GTK_COMBO_BOX(tab.audio_route)), "M"));
   g_hash_table_destroy(room_tabs); room_tabs = NULL;
   gtk_widget_destroy(tab.audio_route); g_object_unref(tab.audio_route);
}

bool ui_print(const char *room, const char *format, ...) {
   return false;
}

static bool block_chat_input;
bool rrclient_chat_input_blocked(const char *text) {
   return block_chat_input;
}
GtkWidget *create_vfo_box(void) {
   GtkWidget *controls = gtk_label_new("Wide frequency, mode, codec and PTT controls");
   gtk_widget_set_size_request(controls, 1400, 60);

   return controls;
}
gui_window_t *create_vfo_window(GtkWidget *controls, char vfo) {
   assert(!"This test uses docked VFO controls");

   return NULL;
}
rrconn_t *rrclient_connection_find(const char *name) {
   return NULL;
}
char *client_chat_complete(const char *line, size_t *cursor, void **state) {
   return NULL;
}
void client_chat_completion_free(void *state) {
}
rrconn_t *rrclient_active_connection(void) {
   return NULL;
}

static char selected_server[128];
bool rrclient_connection_select(const char *name) {
   snprintf(selected_server, sizeof(selected_server), "%s", name);

   return true;
}

static char parsed_menu_command[128];
bool parse_chat_input_real(const char *message) {
   assert(!strcmp(gtk_chat_current_room(), "#origin"));
   assert(!strcmp(selected_server, "server"));
   snprintf(parsed_menu_command, sizeof(parsed_menu_command), "%s", message);

   return false;
}

static char joined_room[128], joined_server[128];
bool cmd_join(int argc, char **args) {
   assert(argc == 2);
   snprintf(joined_room, sizeof(joined_room), "%s", args[1]);
   snprintf(joined_server, sizeof(joined_server), "%s", selected_server);

   return false;
}
const char *rrclient_window_name(const char *room) {
   static char key[256];
   snprintf(key, sizeof(key), "%s:%s", selected_server, room);

   return key;
}
bool rrclient_connection_cycle_status(bool status_active) {
   return false;
}

time_t now;
bool dying, restarting;

static void drain_gtk_events(void) {
   while (g_main_context_pending(NULL)) {
      g_main_context_iteration(NULL, FALSE);
   }
}

static void type_entry_text(GtkWidget *entry, const char *text) {
   gint position = gtk_editable_get_position(GTK_EDITABLE(entry));
   gtk_editable_insert_text(GTK_EDITABLE(entry), text, -1, &position);
   gtk_editable_set_position(GTK_EDITABLE(entry), position);
   drain_gtk_events();
}

static GtkWidget *find_color_choice(GtkWidget *popover, guint color) {
   GtkWidget *grid = gtk_bin_get_child(GTK_BIN(popover));
   assert(GTK_IS_GRID(grid));
   GList *children = gtk_container_get_children(GTK_CONTAINER(grid));
   GtkWidget *found = NULL;
   assert(g_list_length(children) == 16);

   for (GList *item = children ; item ; item = item->next) {
      if (GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item->data), "rr-color-value")) == color) {
         found = GTK_WIDGET(item->data);
      }
   }

   g_list_free(children);
   assert(found);

   return found;
}

static void test_color_popup(void) {
   int argc = 0;
   char **argv = NULL;

   if (!gtk_init_check(&argc, &argv)) {
      puts("SKIP: GTK popup test requires a display");

      return;
   }

   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   GtkWidget *entry = gtk_entry_new();
   GtkWidget *view = gtk_text_view_new();
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
   gtk_text_buffer_set_text(buffer, "copy me", -1);
   GtkTextIter start, end;
   gtk_text_buffer_get_bounds(buffer, &start, &end);
   gtk_text_buffer_select_range(buffer, &start, &end);
   chat_textview = view;
   gtk_container_add(GTK_CONTAINER(window), entry);
   GtkWidget *popover = gtk_popover_new(entry);
   gtk_popover_set_modal(GTK_POPOVER(popover), FALSE);
   g_object_set_data(G_OBJECT(entry), "rr-color-popover", popover);
   g_signal_connect(entry, "changed", G_CALLBACK(gtk_chat_entry_changed), NULL);
   gtk_widget_show_all(window);
   gtk_widget_grab_focus(entry);
   drain_gtk_events();

   gtk_chat_insert_format(entry, 0x03);
   drain_gtk_events();
   g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "C");
   gint marker_selection_start, marker_selection_end;
   assert(!gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &marker_selection_start, &marker_selection_end));
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode")) == 1);
   GtkWidget *sample = gtk_bin_get_child(GTK_BIN(find_color_choice(popover, 4)));
   g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(sample)), ==, "[4]");
   gtk_editable_select_region(GTK_EDITABLE(entry), 0, 1);
   gtk_button_clicked(GTK_BUTTON(find_color_choice(popover, 4)));
   drain_gtk_events();
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "C4"));
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode")) == 3);
   sample = gtk_bin_get_child(GTK_BIN(find_color_choice(popover, 3)));
   g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(sample)), ==, "[4,3]");
   gint selection_start, selection_end;
   assert(!gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &selection_start, &selection_end));

   gtk_editable_select_region(GTK_EDITABLE(entry), 1, 2);
   gtk_button_clicked(GTK_BUTTON(find_color_choice(popover, 3)));
   drain_gtk_events();
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "C4,3"));
   assert(!gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &selection_start, &selection_end));
   type_entry_text(entry, "hello");
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "C4,3hello"));
   char *serialized = gtk_chat_serialize_entry(entry);
   assert(!strcmp(serialized, "\0034,3hello"));
   g_free(serialized);

   GdkEventKey copy_event = {
      0
   };
   copy_event.type = GDK_KEY_PRESS;
   copy_event.keyval = GDK_KEY_C;
   copy_event.state = GDK_CONTROL_MASK | GDK_SHIFT_MASK;
   assert(on_chat_entry_keypress(entry, &copy_event, NULL));
   GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
   char *copied = gtk_clipboard_wait_for_text(clipboard);
   assert(copied && !strcmp(copied, "copy me"));
   g_free(copied);
   GtkTextIter selected_start, selected_end;
   assert(gtk_text_buffer_get_selection_bounds(buffer, &selected_start, &selected_end));
   gtk_text_buffer_select_range(buffer, &selected_start, &selected_start);
   assert(!on_chat_entry_keypress(entry, &copy_event, NULL));
   gtk_entry_set_text(GTK_ENTRY(entry), "");
   gtk_editable_set_position(GTK_EDITABLE(entry), 0);
   copy_event.state = GDK_CONTROL_MASK;
   copy_event.keyval = GDK_KEY_c;
   assert(on_chat_entry_keypress(entry, &copy_event, NULL));
   serialized = gtk_chat_serialize_entry(entry);
   assert(!strcmp(serialized, "\003"));
   g_free(serialized);

   gtk_editable_delete_text(GTK_EDITABLE(entry), 0, -1);
   gint position = 0;
   gtk_editable_insert_text(GTK_EDITABLE(entry), "C", 1, &position);
   gtk_editable_set_position(GTK_EDITABLE(entry), position);
   serialized = gtk_chat_serialize_entry(entry);
   assert(!strcmp(serialized, "C"));
   g_free(serialized);

   gtk_editable_delete_text(GTK_EDITABLE(entry), 0, -1);
   drain_gtk_events();
   gtk_widget_grab_focus(entry);
   drain_gtk_events();
   gtk_chat_insert_format(entry, 0x03);
   drain_gtk_events();
   type_entry_text(entry, "1");
   g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "C1");
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode")) == 1);
   type_entry_text(entry, ",");
   g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "C1,");
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode")) == 2);
   type_entry_text(entry, "3");
   g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "C1,3");
   serialized = gtk_chat_serialize_entry(entry);
   assert(!strcmp(serialized, "\0031,3"));
   g_free(serialized);
   gtk_widget_destroy(window);
}

static void test_transcript_copy(void) {
   if (!gtk_init_check(NULL, NULL)) {
      return;
   }
   GtkWidget *view = gtk_text_view_new();
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
   gtk_text_buffer_set_text(buffer, "copy this transcript", -1);
   GtkTextIter start, end;
   gtk_text_buffer_get_bounds(buffer, &start, &end);
   gtk_text_buffer_select_range(buffer, &start, &end);
   g_signal_connect(view, "key-press-event", G_CALLBACK(gtk_chat_view_keypress), NULL);
   GdkEventKey event = {
      .type = GDK_KEY_PRESS, .keyval = GDK_KEY_C,
      .state = GDK_CONTROL_MASK | GDK_SHIFT_MASK
   };
   gboolean handled = FALSE;
   g_signal_emit_by_name(view, "key-press-event", &event, &handled);
   assert(handled);
   gchar *copied = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
   assert(copied && !strcmp(copied, "copy this transcript"));
   g_free(copied);
   /* Global copy must follow the selected page even after another tab was built. */
   main_notebook = gtk_notebook_new();
   GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkRoomTab tab = {
      .view = view
   };
   g_object_set_data(G_OBJECT(page), "rr-room-tab", &tab);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), page, NULL);
   gtk_widget_show(page);
   chat_textview = gtk_text_view_new();
   assert(gtk_chat_copy_selection());
   copied = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
   assert(copied && !strcmp(copied, "copy this transcript"));
   g_free(copied);
   GtkWidget *entry = gtk_entry_new();
   GtkWidget *input_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_container_add(GTK_CONTAINER(input_window), entry);
   gtk_widget_show_all(input_window);
   drain_gtk_events();
   tab.entry = entry;
   gtk_entry_set_text(GTK_ENTRY(entry), "copy selected input");
   gtk_editable_select_region(GTK_EDITABLE(entry), 5, 13);
   assert(on_chat_entry_keypress(entry, &event, NULL));
   copied = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
   assert(copied && !strcmp(copied, "selected"));
   g_free(copied);
   gtk_editable_select_region(GTK_EDITABLE(entry), 0, 0);
   gtk_text_buffer_select_range(buffer, &start, &end);
   assert(on_chat_entry_keypress(entry, &event, NULL));
   copied = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
   assert(copied && !strcmp(copied, "copy this transcript"));
   g_free(copied);
   gtk_widget_destroy(input_window);
   gtk_widget_destroy(chat_textview);
   gtk_widget_destroy(main_notebook);
   gtk_widget_destroy(view);
   chat_textview = main_notebook = NULL;
}

static void test_incoming_chat_preserves_draft(void) {
   if (!gtk_init_check(NULL, NULL)) {
      return;
   }
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   main_notebook = gtk_notebook_new();
   GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *entry = gtk_entry_new();
   GtkWidget *view = gtk_text_view_new();
   gtk_box_pack_start(GTK_BOX(page), view, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(page), entry, FALSE, FALSE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), page, NULL);
   gtk_container_add(GTK_CONTAINER(window), main_notebook);
   gtk_widget_show_all(window);
   gtk_widget_grab_focus(entry);
   drain_gtk_events();
   gtk_entry_set_text(GTK_ENTRY(entry), "unfinished draft");
   block_chat_input = true;
   assert(parse_chat_input_gtk(NULL, entry));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "unfinished draft"));
   assert(!g_object_get_data(G_OBJECT(entry), "rr-input-history"));
   block_chat_input = false;
   gtk_editable_set_position(GTK_EDITABLE(entry), 4);
   GtkRoomTab tab = {
      .page = page, .entry = entry, .view = view
   };
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));

   for (int i = 0 ; i < 3 ; i++) {
      /* The incoming message path ensures the existing tab, then appends. */
      gtk_chat_select_existing_tab(&tab);
      gtk_text_buffer_insert_at_cursor(buffer, "incoming message\n", -1);
      drain_gtk_events();
      assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "unfinished draft"));
      assert(gtk_editable_get_position(GTK_EDITABLE(entry)) == 4);
      gint first, last;
      assert(!gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &first, &last));
   }

   snprintf(tab.room, sizeof(tab.room), "#origin");
   snprintf(tab.server, sizeof(tab.server), "server");
   g_object_set_data(G_OBJECT(page), "rr-room-tab", &tab);
   room_tabs = g_hash_table_new(g_str_hash, g_str_equal);
   g_hash_table_insert(room_tabs, "origin-key", &tab);
   assert(!gtk_chat_execute_command("origin-key", "/whois BOB"));
   assert(!strcmp(parsed_menu_command, "/whois BOB"));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "unfinished draft"));
   assert(gtk_editable_get_position(GTK_EDITABLE(entry)) == 4);
   assert(!gtk_chat_execute_command("closed-key", "/whois BOB"));
   g_hash_table_destroy(room_tabs);
   room_tabs = NULL;
   puts("PASS: roster commands reach the normal parser with room/server context and preserve the draft");
   type_entry_text(entry, "X");
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "unfiXnished draft"));
   gtk_editable_select_region(GTK_EDITABLE(entry), 2, 6);
   gtk_chat_select_existing_tab(&tab);
   gint first, last;
   assert(gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &first, &last));
   assert(first == 2 && last == 6);
   gtk_widget_destroy(window);
   main_notebook = NULL;
}

static void test_radio_overflow_preserves_roster(void) {
   if (!gtk_init_check(NULL, NULL)) {
      return;
   }
   GtkWidget *window = gtk_offscreen_window_new();
   GtkWidget *outer = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(outer), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(outer), 320);
   GtkWidget *notebook = gtk_notebook_new();
   GtkWidget *room = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *radio = chatbox_vfo_init();
   gtk_box_pack_start(GTK_BOX(room), radio, FALSE, FALSE, 0);
   GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
   GtkWidget *roster = gtk_label_new("Privs Username TX Mute Role");
   gtk_widget_set_size_request(roster, 300, 200);
   gtk_paned_pack1(GTK_PANED(paned), gtk_text_view_new(), TRUE, FALSE);
   gtk_paned_pack2(GTK_PANED(paned), roster, FALSE, TRUE);
   gtk_box_pack_start(GTK_BOX(room), paned, TRUE, TRUE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), room, gtk_label_new("#rig"));
   gtk_container_add(GTK_CONTAINER(outer), notebook);
   gtk_container_add(GTK_CONTAINER(window), outer);
   gtk_window_set_default_size(GTK_WINDOW(window), 1000, 400);
   gtk_widget_show_all(window);

   for (int i = 0 ; i < 40 ; i++) {
      drain_gtk_events();
      g_usleep(1000);
   }

   GtkAdjustment *whole_page = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(outer));
   assert(gtk_adjustment_get_upper(whole_page) <= gtk_adjustment_get_page_size(whole_page) + 1);
   int x, y;
   assert(gtk_widget_translate_coordinates(roster, window, 0, 0, &x, &y));
   assert(x >= 0 && x + gtk_widget_get_allocated_width(roster) <= gtk_widget_get_allocated_width(window));
   assert(GTK_IS_SCROLLED_WINDOW(radio));
   GtkAdjustment *radio_scroll = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(radio));
   assert(gtk_adjustment_get_upper(radio_scroll) > gtk_adjustment_get_page_size(radio_scroll));
   gtk_widget_destroy(window);
   puts("PASS: wide VFO controls scroll locally and keep the rig roster inside the main viewport");
}

static void test_room_links(void) {
   if (!gtk_init_check(NULL, NULL)) {
      return;
   }
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   main_notebook = gtk_notebook_new();
   GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *view = gtk_text_view_new();
   GtkWidget *entry = gtk_entry_new();
   gtk_box_pack_start(GTK_BOX(page), view, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(page), entry, FALSE, FALSE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), page, NULL);
   gtk_container_add(GTK_CONTAINER(window), main_notebook);
   gtk_widget_show_all(window);
   drain_gtk_events();
   gtk_entry_set_text(GTK_ENTRY(entry), "unfinished draft");
   g_object_set_data(G_OBJECT(view), "rr-server-name", "origin-server");
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
   g_signal_connect_after(buffer, "insert-text", G_CALLBACK(gtk_chat_link_inserted), NULL);
   gtk_text_buffer_insert_at_cursor(buffer, "é See #remote", -1);
   gtk_text_buffer_insert_at_cursor(buffer, "-rig0. next", -1);
   drain_gtk_events();
   GtkTextIter iter;
   gtk_text_buffer_get_iter_at_offset(buffer, &iter, 7);
   GdkRectangle rectangle;
   gtk_text_view_get_iter_location(GTK_TEXT_VIEW(view), &iter, &rectangle);
   gint x, y;
   gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_TEXT, rectangle.x + 1, rectangle.y + 1, &x, &y);
   GdkEvent event = {
      .button = {
         .type = GDK_BUTTON_RELEASE, .button = 1, .x = x, .y = y
      }
   };
   GtkRoomTab other_tab = {
      .page = page, .entry = entry, .view = view
   };
   room_tabs = g_hash_table_new(g_str_hash, g_str_equal);
   g_hash_table_insert(room_tabs, "other-server:#remote-rig0", &other_tab);
   gtk_chat_link_event(view, &event, NULL);
   g_hash_table_destroy(room_tabs);
   room_tabs = NULL;
   assert(!strcmp(joined_room, "#remote-rig0"));
   assert(!strcmp(joined_server, "origin-server"));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "unfinished draft"));
   /* Existing rooms switch tabs without sending a JOIN. */
   GtkRoomTab tab = {
      .page = page, .entry = entry, .view = view
   };
   room_tabs = g_hash_table_new(g_str_hash, g_str_equal);
   g_hash_table_insert(room_tabs, "origin-server:#remote-rig0", &tab);
   joined_room[0] = 0;
   gtk_chat_link_event(view, &event, NULL);
   assert(!joined_room[0]);
   g_hash_table_destroy(room_tabs);
   room_tabs = NULL;
   /* Selecting text suppresses activation, even over a link. */
   GtkTextIter a, b;
   gtk_text_buffer_get_bounds(buffer, &a, &b);
   gtk_text_buffer_select_range(buffer, &a, &b);
   gtk_chat_link_event(view, &event, NULL);
   assert(!joined_room[0]);
   gtk_widget_destroy(window);
   main_notebook = NULL;
   puts("PASS: GTK room links keep source-server context, select tabs, preserve drafts and allow text selection");
}

static void test_input_history_focus(void) {
   if (!gtk_init_check(NULL, NULL)) {
      puts("SKIP: GTK history test requires a display");

      return;
   }
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *entry = gtk_entry_new(), *other = gtk_entry_new();
   gtk_container_add(GTK_CONTAINER(window), box);
   gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(box), other, FALSE, FALSE, 0);
   g_object_set_data(G_OBJECT(entry), "rr-chat-input", GINT_TO_POINTER(1));
   gtk_widget_show_all(window);
   gtk_widget_grab_focus(entry);
   drain_gtk_events();
   GPtrArray *lines;
   GtkInputHistory *history = entry_history(entry, &lines);

   if (history->shared) {
      input_history = g_ptr_array_new_with_free_func(g_free);
      lines = input_history;
   }
   g_ptr_array_add(lines, g_strdup("older"));
   g_ptr_array_add(lines, g_strdup("newer"));
   gtk_entry_set_text(GTK_ENTRY(entry), "unfinished draft");
   GdkEventKey event = {
      .type = GDK_KEY_PRESS, .keyval = GDK_KEY_Up
   };
   assert(gtk_chat_history_keypress(window, &event));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "newer"));
   event.keyval = GDK_KEY_KP_Up;
   assert(gtk_chat_history_keypress(window, &event));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "older"));
   event.keyval = GDK_KEY_Down;
   assert(gtk_chat_history_keypress(window, &event));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "newer"));
   event.keyval = GDK_KEY_KP_Down;
   assert(gtk_chat_history_keypress(window, &event));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "unfinished draft"));
   event.keyval = GDK_KEY_Up;
   event.state = GDK_CONTROL_MASK;
   assert(!gtk_chat_history_keypress(window, &event));
   event.state = 0;
   gtk_widget_grab_focus(other);
   assert(!gtk_chat_history_keypress(window, &event));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "unfinished draft"));

   if (history->shared) {
      g_ptr_array_unref(input_history);
      input_history = NULL;
   }
   gtk_widget_destroy(window);
   puts("PASS: GTK focused chat history, keypad arrows and draft restoration; other entries keep arrows");
}

int main(void) {
   assert(gtk_formatting_control('B') == 0x02);
   assert(gtk_formatting_control('C') == 0x03);
   assert(gtk_formatting_control('U') == 0x1f);
   assert(gtk_formatting_control('X') == 0);
   assert(!strcmp(gtk_formatting_token(0x02), "B"));
   char *literal = gtk_colorize_string("{red} stays literal");
   assert(!strcmp(literal, "{red} stays literal"));
   g_free(literal);
   char *text = gtk_colorize_string("\00304Radio and discovery\017");
   assert(!strcmp(text, "<span foreground=\"#ff0000\">Radio and discovery</span>"));
   g_free(text);
   text = gtk_colorize_string("\0033,1green on black\017");
   assert(!strcmp(text, "<span foreground=\"#009300\" background=\"#000000\">green on black</span>"));
   g_free(text);
   text = gtk_colorize_string("\0031,3hello\003");
   assert(!strcmp(text, "<span foreground=\"#000000\" background=\"#009300\">hello</span>"));
   g_free(text);
   text = gtk_colorize_string("\0036,1magenta on black\017");
   assert(strstr(text, "foreground=\"#9c009c\" background=\"#000000\""));
   g_free(text);
   text = gtk_colorize_string("\0031,6black on magenta\017");
   assert(strstr(text, "foreground=\"#000000\" background=\"#9c009c\""));
   g_free(text);
   text = gtk_colorize_string("\00302A\00312B\00306C\00313D\00310E\00311F\00300G\00315H");
   assert(strstr(text, "foreground=\"#00007f\">A</span><span foreground=\"#0000fc\">B"));
   assert(strstr(text, "foreground=\"#9c009c\">C</span><span foreground=\"#ff00ff\">D"));
   assert(strstr(text, "foreground=\"#009393\">E</span><span foreground=\"#00ffff\">F"));
   assert(strstr(text, "foreground=\"#ffffff\">G</span><span foreground=\"#d2d2d2\">H"));
   g_free(text);
   text = gtk_colorize_string("\00304123,45\017");
   assert(strstr(text, ">123,45</span>"));
   g_free(text);
   text = gtk_colorize_string("\00304<&>\017");
   assert(strstr(text, "&lt;&amp;&gt;"));
   g_free(text);
   text = gtk_colorize_string("\002bold\002 and \037underlined\037");
   assert(strstr(text, "<b>bold</b>"));
   assert(strstr(text, "<u>underlined</u>"));
   g_free(text);
   text = gtk_colorize_string("\035italic\035 \036strike\036 \021mono\021");
   assert(strstr(text, "<i>italic</i>"));
   assert(strstr(text, "<s>strike</s>"));
   assert(strstr(text, "<tt>mono</tt>"));
   g_free(text);
   text = gtk_colorize_string("\0034red \026reversed\026");
   assert(strstr(text, "foreground=\"black\" background=\"#ff0000\""));
   g_free(text);
   text = gtk_colorize_string("\026reverse\017plain");
   assert(!strcmp(text, "<span foreground=\"black\" background=\"white\">reverse</span>plain"));
   g_free(text);
   check_audio_selector();
   test_radio_overflow_preserves_roster();
   test_color_popup();
   test_transcript_copy();
   test_incoming_chat_preserves_draft();
   test_room_links();
   test_input_history_focus();
   puts("PASS: GTK mIRC red headings, numeric text, reset and markup escaping");
}
