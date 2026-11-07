#include <assert.h>
#include "rrclient/gtk/gtk.core.c"
#include "rrclient/gtk/gtk.chat-protocol.c"
#include "rrclient/gtk/gtk.chat.c"
#include "rrclient/gtk/gtk.chat.links.c"
#include "rrclient/gtk/gtk.chat.highlight.c"
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

static rrconn_t origin_connection, irc_origin_connection;
static bool checking_irc_input;
rrconn_t *ws_conn;
void userlist_remove_room_view(const char *room) {
   (void)room;
}
rrconn_t *rrclient_connection_find(const char *name) {
   return name && !strcmp(name, "efnet") ? &irc_origin_connection : name && !strcmp(name, "server") ? &origin_connection : NULL;
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
static const char *dispatched_room;
static unsigned dispatched;
void rrclient_input_dispatch(rrconn_t *connection, const char *room, void (*callback) (void *), void *data) {
   assert(connection == (checking_irc_input ? &irc_origin_connection : &origin_connection));
   assert(room && !strcmp(room, checking_irc_input ? "#HamRadio" : "#origin"));
   dispatched_room = room;
   dispatched++;
   callback(data);
   dispatched_room = NULL;
}
bool parse_chat_input_real(const char *message) {
   assert(!strcmp(dispatched_room ? dispatched_room : gtk_chat_current_room(), checking_irc_input ? "#HamRadio" : "#origin"));
   assert(!strcmp(selected_server, checking_irc_input ? "efnet" : "server"));
   snprintf(parsed_menu_command, sizeof(parsed_menu_command), "%s", message);

   return false;
}

static char joined_room[128], joined_server[128];
bool cmd_join(int argc, char **args) {
   assert(argc == 2);

   if (room_tabs && g_hash_table_lookup(room_tabs, rrclient_window_name(args[1]))) {
      return false;
   }
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
   assert(g_list_length(children) == 100);

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
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "C04"));
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode")) == 3);
   sample = gtk_bin_get_child(GTK_BIN(find_color_choice(popover, 3)));
   g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(sample)), ==, "[4,3]");
   gint selection_start, selection_end;
   assert(!gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &selection_start, &selection_end));

   gtk_editable_select_region(GTK_EDITABLE(entry), 1, 2);
   gtk_button_clicked(GTK_BUTTON(find_color_choice(popover, 3)));
   drain_gtk_events();
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "C04,03"));
   assert(!gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &selection_start, &selection_end));
   type_entry_text(entry, "hello");
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "C04,03hello"));
   char *serialized = gtk_chat_serialize_entry(entry);
   assert(!strcmp(serialized, "\00304,03hello"));
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
   gtk_chat_set_serialized_entry(entry, "");
   type_entry_text(entry, "\004123aBc,789DEFpasted");
   serialized = gtk_chat_serialize_entry(entry);
   assert(!strcmp(serialized, "\004123aBc,789DEFpasted"));
   g_free(serialized);
   gtk_chat_set_serialized_entry(entry, "\004aAbBcC,102030RGB\00398,99extended");
   serialized = gtk_chat_serialize_entry(entry);
   assert(!strcmp(serialized, "\004aAbBcC,102030RGB\00398,99extended"));
   g_free(serialized);
   assert(strstr(gtk_entry_get_text(GTK_ENTRY(entry)), "HaAbBcC"));
   gtk_chat_set_serialized_entry(entry, "");
   gtk_editable_set_position(GTK_EDITABLE(entry), 0);
   gtk_chat_insert_format(entry, 0x03);
   drain_gtk_events();
   gtk_button_clicked(GTK_BUTTON(find_color_choice(popover, 98)));
   drain_gtk_events();
   gtk_button_clicked(GTK_BUTTON(find_color_choice(popover, 99)));
   type_entry_text(entry, "123");
   serialized = gtk_chat_serialize_entry(entry);
   g_assert_cmpstr(serialized, ==, "\00398,99123");
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
   g_hash_table_insert(room_tabs, "server:#origin", &tab);
   gtk_notebook_set_tab_label(GTK_NOTEBOOK(main_notebook), page, gtk_label_new("#origin"));
   assert(!gtk_chat_execute_command("origin-key", "/whois BOB"));
   assert(!strcmp(parsed_menu_command, "/whois BOB"));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "unfinished draft"));
   assert(gtk_editable_get_position(GTK_EDITABLE(entry)) == 4);
   assert(!gtk_chat_execute_command("closed-key", "/whois BOB"));
   /* A still-focused entry must retain its destination after another page becomes current, including page moves across the wrapping strip. */
   GtkWidget *other_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkRoomTab other_origin = {
      .page = other_page
   };
   snprintf(other_origin.room, sizeof(other_origin.room), "#scratchmonkey");
   g_object_set_data(G_OBJECT(other_page), "rr-room-tab", &other_origin);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), other_page, gtk_label_new("other"));
   gtk_widget_show_all(other_page);
   gtk_notebook_reorder_child(GTK_NOTEBOOK(main_notebook), other_page, 0);
   gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), 0);
   g_object_set_data_full(G_OBJECT(entry), "rr-server-name", g_strdup("server"), g_free);
   g_object_set_data_full(G_OBJECT(entry), "rr-room-name", g_strdup("#origin"), g_free);
   GtkWidget *test_popover = gtk_popover_new(entry);
   g_object_set_data(G_OBJECT(entry), "rr-color-popover", test_popover);
   gtk_entry_set_text(GTK_ENTRY(entry), "send to origin");
   assert(!parse_chat_input_gtk(NULL, entry));
   assert(dispatched == 2 && !strcmp(parsed_menu_command, "send to origin"));
   assert(!strcmp(gtk_chat_current_room(), "#scratchmonkey"));
   gtk_entry_set_text(GTK_ENTRY(entry), "draft retained during incoming messages");
   gtk_chat_incoming("#origin", false, "preadminpost", "admin");
   assert(!strcmp(gtk_chat_current_room(), "#scratchmonkey"));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "draft retained during incoming messages"));
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(page), "rr-tab-unread")) == 1);
   gtk_chat_incoming("#origin", false, "Hi ADMIN: are you there?", "admin");
   assert(!strcmp(gtk_chat_current_room(), "#scratchmonkey"));
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(page), "rr-tab-unread")) == 2);
   gtk_chat_incoming("#origin", false, "ordinary follow-up", "admin");
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(page), "rr-tab-unread")) == 2);
   GtkWidget *visible_entry = chat_entry, *visible_view = chat_textview;
   gtk_chat_incoming("unseen-private", true, "hello", "admin");
   assert(!strcmp(gtk_chat_current_room(), "#scratchmonkey"));
   assert(chat_entry == visible_entry && chat_textview == visible_view);
   gpointer new_key, new_tab;
   assert(g_hash_table_lookup_extended(room_tabs, rrclient_window_name("unseen-private"), &new_key, &new_tab));
   GtkRoomTab *incoming_tab = new_tab;
   assert(incoming_tab->is_query && GPOINTER_TO_INT(g_object_get_data(G_OBJECT(incoming_tab->page), "rr-tab-unread")) == 1);
   gtk_notebook_remove_page(GTK_NOTEBOOK(main_notebook), gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), incoming_tab->page));
   g_hash_table_remove(room_tabs, new_key);
   g_free(new_key);
   g_free(new_tab);


   g_object_set_data(G_OBJECT(entry), "rr-room-name", NULL);
   g_object_set_data(G_OBJECT(entry), "rr-server-name", NULL);
   gtk_notebook_remove_page(GTK_NOTEBOOK(main_notebook), 0);
   gtk_entry_set_text(GTK_ENTRY(entry), "unfinished draft");
   gtk_editable_set_position(GTK_EDITABLE(entry), 4);
   gtk_editable_select_region(GTK_EDITABLE(entry), 4, 4);

   /* Context-menu closing must target the page's server/room, retaining drafts. */
   g_hash_table_insert(room_tabs, "server:#origin", &tab);
   gtk_chat_tab_close(page, FALSE);
   assert(!strcmp(parsed_menu_command, "/win close"));
   gtk_chat_tab_close(page, TRUE);
   assert(!strcmp(parsed_menu_command, "/win close -force"));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "unfinished draft"));
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
   GtkWidget *radio = gtk_scrolled_window_new(NULL, NULL);
   GtkWidget *radio_controls = gtk_label_new("Wide frequency, mode, codec and PTT controls");
   gtk_widget_set_size_request(radio_controls, 1400, 60);
   gtk_container_add(GTK_CONTAINER(radio), radio_controls);
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

static rrclient_ui_chat_link_t link_request;
static unsigned link_requests;
static void capture_room_link(const rr_event_message_t *message, void *user) {
   assert(message->length == sizeof(link_request));
   memcpy(&link_request, message->payload, sizeof(link_request));
   link_requests++;
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
   gtk_chat_links_attach(view);
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
   chat_link_event(view, &event, NULL);
   g_hash_table_destroy(room_tabs);
   room_tabs = NULL;
   assert(link_requests == 1 && !strcmp(link_request.room, "#remote-rig0"));
   assert(!strcmp(link_request.server, "origin-server"));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "unfinished draft"));
   /* Existing-room selection is a core decision; GTK sends the same owned request. */
   GtkRoomTab tab = {
      .page = page, .entry = entry, .view = view
   };
   room_tabs = g_hash_table_new(g_str_hash, g_str_equal);
   g_hash_table_insert(room_tabs, "origin-server:#remote-rig0", &tab);
   joined_room[0] = 0;
   chat_link_event(view, &event, NULL);
   assert(link_requests == 2);
   g_hash_table_destroy(room_tabs);
   room_tabs = NULL;
   /* Selecting text suppresses activation, even over a link. */
   GtkTextIter a, b;
   gtk_text_buffer_get_bounds(buffer, &a, &b);
   gtk_text_buffer_select_range(buffer, &a, &b);
   chat_link_event(view, &event, NULL);
   assert(link_requests == 2);
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

/* Optional widget/runtime dependencies are not exercised by this fixture. */
GtkWidget *rr_connect_button_new(void) {
   return gtk_button_new();
}
GtkWidget *rr_gtk_spectrum_panel_new(const char *server, const char *room) {
   return gtk_label_new(room);
}
bool rrclient_room_part(const char *room) {
   return false;
}
void rrclient_media_room_parted(const char *room) {
}
bool rrclient_connection_traffic(const char *profile, uint64_t *rx, uint64_t *tx) {
   return false;
}
bool rrclient_direct_traffic(rrconn_t *client, uint64_t *rx, uint64_t *tx) {
   return false;
}
void userlist_redraw_gtk(void) {
}
void userlist_room_state_changed(const char *room) {
}
void ptt_button_refresh(void) {
}

const char *login_user;
uint32_t rrclient_room_vfo_mask(const char *room) {
   return 0;
}
const char *rrclient_room_vfos(const char *room) {
   return "";
}
char rrclient_room_active_vfo(const char *room) {
   return 'A';
}
void rrclient_room_set_active_vfo(const char *room, char vfo) {
}
bool rrclient_room_tx_control(const char *room) {
   return false;
}
bool rrclient_room_rx_tunable(const char *room) {
   return false;
}
const char *rrclient_media_vfo_uuid(const char *room, char vfo) {
   return NULL;
}
const dict *rrclient_object_property(const char *uuid, const char *name) {
   return NULL;
}
bool rrclient_local_bridge_state_vfo(char vfo, uint64_t *hz, const char **mode) {
   return false;
}
void userlist_room_vfos_changed(const char *room) {
}
void vfo_controls_set_local_sdr(bool local) {
}
void vfo_controls_set_rx_only(bool rx_only) {
}
bool vfo_update_ui(void) {
   return false;
}
GType gtk_freq_entry_get_type(void) {
   return G_TYPE_OBJECT;
}
bool gtk_freq_entry_is_editing(GtkFreqEntry *fe) {
   return false;
}
unsigned long gtk_freq_entry_get_frequency(GtkFreqEntry *fe) {
   return 0;
}
void gtk_freq_entry_set_frequency(GtkFreqEntry *fe, unsigned long hz) {
}
void modebox_update_state(const char *mode, int width) {
}
GtkWidget *rr_gtk_rf_spectrum_new(void) {
   return gtk_label_new("");
}
RRFloatPanel *rr_floating_panel_new(GtkOverlay *overlay, GtkNotebook *tabs, const char *id, const char *title) {
   return NULL;
}
GtkWidget *rr_floating_panel_body(RRFloatPanel *panel) {
   return NULL;
}
void rr_floating_panel_show(RRFloatPanel *panel) {
}
void rr_floating_panel_hide(RRFloatPanel *panel) {
}
void rr_floating_panel_set_closable(RRFloatPanel *panel, gboolean closable) {
}
void rr_floating_panel_set_title(RRFloatPanel *panel, const char *title) {
}
void rr_floating_panel_set_title_click(RRFloatPanel *panel, GCallback callback, gpointer data) {
}

static unsigned roster_docks;
static char roster_profile[128];
void userlist_dock_room_into(GtkPaned *paned, const char *room) {
   roster_docks++;
   snprintf(roster_profile, sizeof(roster_profile), "%s", gtk_chat_profile() ? gtk_chat_profile() : "");
}
bool gui_hotkey_register(GtkWidget *widget) {
   return false;
}
const char *rrclient_selected_server(void) {
   return "server";
}
static void test_query_has_no_roster(void) {
   if (!gtk_init_check(NULL, NULL)) {
      return;
   }
   unsigned before = roster_docks;
   GtkWidget *query = create_chat_box_for_room(false, "alice", true);
   g_object_ref_sink(query);
   assert(roster_docks == before);
   GList *children = gtk_container_get_children(GTK_CONTAINER(query));
   assert(GTK_IS_SCROLLED_WINDOW(children->data));
   g_list_free(children);
   gtk_widget_destroy(query);
   g_object_unref(query);
   GtkWidget *channel = create_chat_box_for_room(false, "#hamradio", false);
   g_object_ref_sink(channel);
   assert(roster_docks == before + 1);
   gtk_widget_destroy(channel);
   g_object_unref(channel);
   puts("PASS: query widgets have no roster while IRC channels keep theirs");
}

/* Reproduce core producing text before GTK receives JOIN, then selecting a different network before GTK drains either queue. */
static void queued_chat_text(const char *room, const char *format, ...) {
   va_list args;
   va_start(args, format);
   frontend_gtk_vprint(room, format, args);
   va_end(args);
}
static gpointer produce_irc_chat(gpointer unused) {
   queued_chat_text("#HamRadio", "joined #HamRadio");
   frontend_gtk_chat_protocol_capture("join", "{\"talk.room\":\"#HamRadio\",\"talk.user\":\"rustyape\"}", ws_conn, NULL);
   frontend_gtk_chat_protocol_capture("room.topic", "{\"talk.room\":\"#HamRadio\",\"talk.topic\":\"Radio topic\"}", ws_conn, NULL);
   queued_chat_text("#HamRadio", "Topic: Radio topic");
   frontend_gtk_chat_protocol_capture("talk.msg",
      "{\"talk.target\":\"#HamRadio\",\"talk.from\":\"alice\",\"talk.msg_type\":\"pub\",\"talk.data\":\"hello rustyape\"}", ws_conn, NULL);
   queued_chat_text("#HamRadio", "hello rustyape");
   queued_chat_text(NULL, "End of MOTD");

   return NULL;
}
static void test_queued_irc_identity(void) {
   assert(gtk_init_check(NULL, NULL));
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   main_notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(window), main_notebook);
   GtkRoomTab status = {
      .page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), .view = gtk_text_view_new()
   };
   gtk_box_pack_start(GTK_BOX(status.page), status.view, TRUE, TRUE, 0);
   status_room_tab = &status;
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), status.page, gtk_label_new("STATUS"));
   gtk_widget_show_all(window);
   frontend_gtk_owner = g_thread_self();
   frontend_gtk_event_context = g_main_context_ref_thread_default();
   const char *old_server = server_name, *old_user = login_user;
   server_name = "efnet";
   login_user = "rustyape";
   snprintf(selected_server, sizeof(selected_server), "efnet");
   rrconn_t irc = {
      .is_ws = false
   };
   ws_conn = &irc;
   GThread *producer = g_thread_new("irc-test", produce_irc_chat, NULL);
   g_thread_join(producer);
   /* Neither connection pointers nor these compatibility globals may be used when rendering the now-queued messages. */
   server_name = "server";
   login_user = "different-nickname";
   snprintf(selected_server, sizeof(selected_server), "server");
   ws_conn = NULL;
   gtk_chat_room_add("#HamRadio");
   GtkRoomTab *other = g_hash_table_lookup(room_tabs, "server:#HamRadio");
   assert(other);
   gtk_entry_set_text(GTK_ENTRY(other->entry), "other network draft");
   int selected_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook));
   drain_gtk_events();
   GtkRoomTab *tab = g_hash_table_lookup(room_tabs, "efnet:#HamRadio");
   assert(tab && !tab->is_query && !strcmp(tab->server, "efnet"));
   assert(tab != other && g_hash_table_lookup(room_tabs, "server:#HamRadio") == other);
   assert(!strcmp(roster_profile, "efnet"));
   assert(!strcmp(g_object_get_data(G_OBJECT(tab->entry), "rr-server-name"), "efnet"));
   assert(!strcmp(g_object_get_data(G_OBJECT(tab->page), "rr-tab-topic"), "Radio topic"));
   GtkTextIter start, end;
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab->view));
   gtk_text_buffer_get_bounds(buffer, &start, &end);
   char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
   assert(strstr(text, "joined #HamRadio") && strstr(text, "Topic: Radio topic") && strstr(text, "hello rustyape"));
   assert(!strstr(text, "End of MOTD"));
   g_free(text);
   buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(status.view));
   gtk_text_buffer_get_bounds(buffer, &start, &end);
   text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
   assert(strstr(text, "End of MOTD") && !strstr(text, "#HamRadio") && !strstr(text, "hello rustyape"));
   g_free(text);
   assert(gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook)) == selected_page);
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(other->entry)), "other network draft"));
   buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(other->view));
   assert(gtk_text_buffer_get_char_count(buffer) == 0);
   checking_irc_input = true;
   gtk_entry_set_text(GTK_ENTRY(tab->entry), "send to efnet");
   assert(!parse_chat_input_gtk(NULL, tab->entry));
   assert(!strcmp(parsed_menu_command, "send to efnet"));
   checking_irc_input = false;
   gtk_widget_destroy(window);
   g_hash_table_destroy(room_tabs);
   room_tabs = NULL;
   status_room_tab = NULL;
   main_notebook = chat_entry = chat_textview = NULL;
   g_main_context_unref(frontend_gtk_event_context);
   frontend_gtk_event_context = NULL;
   server_name = old_server;
   login_user = old_user;
   puts("PASS: queued IRC JOIN, topic, roster and text retain network identity; STATUS keeps server notices");
}

int main(int argc, char **argv) {
   event_init();
   event_on_message_token(RRCLIENT_UI_CHAT_LINK_EVENT, capture_room_link, NULL, NULL, NULL);

   if (argc == 2 && !strcmp(argv[1], "--irc-routing")) {
      test_queued_irc_identity();

      return 0;
   }
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
   text = gtk_colorize_string("\00316,98dark\004Ab12eFhex\00399default\003,tail");
   assert(strstr(text, "foreground=\"#470000\" background=\"#ffffff\">dark"));
   assert(strstr(text, "foreground=\"#ab12ef\" background=\"#ffffff\">hex"));
   assert(strstr(text, "<span background=\"#ffffff\">default</span>,tail"));
   g_free(text);
   text = gtk_colorize_string("\00412345,abcdef\004aabbcc,12345!");
   assert(!strcmp(text, "12345,abcdef<span foreground=\"#aabbcc\">,12345!</span>"));
   g_free(text);
   /* Crossing color/style boundaries must produce properly nested markup. */
   text = gtk_colorize_string("\002\00352,16bold\002plain\035\004AABBCCitalic\037u\035no-i\017<&>");
   PangoAttrList *attributes = NULL;
   char *plain = NULL;
   GError *error = NULL;
   assert(pango_parse_markup(text, -1, 0, &attributes, &plain, NULL, &error));
   assert(!error && !strcmp(plain, "boldplainitalicuno-i<&>"));
   pango_attr_list_unref(attributes);
   g_free(plain);
   g_free(text);
   test_radio_overflow_preserves_roster();
   test_color_popup();
   test_transcript_copy();
   test_incoming_chat_preserves_draft();
   test_room_links();
   test_input_history_focus();
   test_query_has_no_roster();
   test_queued_irc_identity();
   puts("PASS: GTK mIRC red headings, numeric text, reset and markup escaping");
}
