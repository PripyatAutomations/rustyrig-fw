#include <assert.h>
#include "rrclient/gtk/gtk.userlist.c"
bool dying, restarting;
rrconn_t *ws_conn;
const char *server_name;
time_t now;
static rrconn_t *roster_connection;
struct rr_user *global_userlist;
static const char *room_vfos = "";
static bool room_tx_control;
static uint32_t room_vfo_mask;
bool rrclient_room_tx_control(const char *room) {
   (void)room;
   return room_tx_control;
}
uint32_t rrclient_room_vfo_mask(const char *room) {
   (void)room;
   return room_vfo_mask;
}
const char *rrclient_room_vfos(const char *room) {
   assert(!strcmp(room, "#rig"));

   return room_vfos;
}
rrconn_t *rrclient_connection_find(const char *name) {
   (void)name;

   return roster_connection;
}
void gtk_chat_query_add(const char *user) {
}
static void pump_layout(void) {
   for (int i = 0; i < 40; i++) {
      while (gtk_events_pending()) {
         gtk_main_iteration();
      }
      g_usleep(1000);
   }
}

static void test_late_roster_autosize(void) {
   dict_add(cfg, "ui.userlist.vfo.dock-width", "0");
   dict_add(cfg, "ui.userlist.vfo.columns", "0,1,2,3,4");
   room_vfos = "rig0.vfo_a rig0.vfo_b";
   GtkWidget *window = gtk_offscreen_window_new();
   GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
   GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *view = userlist_view_create();
   userlist_set_columns(view, "#rig");
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
   gtk_container_add(GTK_CONTAINER(scroll), view);
   gtk_box_pack_start(GTK_BOX(panel), scroll, TRUE, TRUE, 0);
   gtk_paned_pack1(GTK_PANED(paned), gtk_label_new("Chat"), TRUE, FALSE);
   gtk_paned_pack2(GTK_PANED(paned), panel, FALSE, TRUE);
   room_userlist_entry_t entry = {
      .panel = panel, .view = view, .dock_paned = GTK_PANED(paned),
      .docked = true, .auto_dock_width = true
   };
   g_object_set_data(G_OBJECT(view), "rr-room-roster", &entry);
   g_signal_connect(paned, "notify::position", G_CALLBACK(room_userlist_position), &entry);
   g_signal_connect(paned, "size-allocate", G_CALLBACK(room_userlist_allocate), &entry);
   gtk_container_add(GTK_CONTAINER(window), paned);
   gtk_window_set_default_size(GTK_WINDOW(window), 1000, 300);
   gtk_widget_show_all(window);
   userlist_fit_width(view);
   pump_layout();
   int initial = cfg_get_int("ui.userlist.vfo.dock-width", 0);
   GtkTreeIter row;
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view)));
   gtk_list_store_append(store, &row);
   gtk_list_store_set(store, &row, COL_USERNAME, "LONG_OPERATOR_CALLSIGN", -1);
   pump_layout();
   userlist_fit_width(view);
   pump_layout();
   assert(cfg_get_int("ui.userlist.vfo.dock-width", 0) > initial);
   assert(gtk_scrolled_window_get_min_content_width(GTK_SCROLLED_WINDOW(scroll)) == -1);
   GtkAdjustment *horizontal = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scroll));
   assert(gtk_adjustment_get_upper(horizontal) <= gtk_adjustment_get_page_size(horizontal) + 1);
   for (int i = 0; i < NUM_COLS; i++) {
      GtkTreeViewColumn *column = userlist_column(view, i);
      assert(gtk_tree_view_column_get_visible(column));
      GtkWidget *header = gtk_tree_view_column_get_button(column);
      int x, y;
      assert(gtk_widget_translate_coordinates(header, scroll, 0, 0, &x, &y));
      assert(x >= 0 && x + gtk_widget_get_allocated_width(header) <= gtk_widget_get_allocated_width(scroll));
   }
   GdkEventButton drag = { .window = gtk_paned_get_handle_window(GTK_PANED(paned)) };
   room_userlist_resize_started(paned, &drag, &entry);
   assert(!entry.auto_dock_width);
   room_userlist_set_width(&entry, 220);
   room_userlist_position(GTK_PANED(paned), NULL, &entry);
   pump_layout();
   int saved = cfg_get_int("ui.userlist.vfo.dock-width", 0);
   gtk_list_store_set(store, &row, COL_USERNAME, "AN_EVEN_LONGER_OPERATOR_CALLSIGN", -1);
   pump_layout();
   userlist_fit_width(view);
   pump_layout();
   assert(cfg_get_int("ui.userlist.vfo.dock-width", 0) == saved);
   /* A newly detached auto-sized window must fit the populated roster too. */
   GtkWidget *detached = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   g_object_ref(panel);
   gtk_container_remove(GTK_CONTAINER(paned), panel);
   gtk_container_add(GTK_CONTAINER(detached), panel);
   g_object_unref(panel);
   entry.docked = false;
   entry.window = detached;
   entry.auto_window_width = true;
   dict_add(cfg, "ui.userlist.vfo.width", "0");
   int detached_width = room_userlist_width(&entry, "width");
   assert(detached_width > 220);
   gtk_window_set_default_size(GTK_WINDOW(detached), detached_width, 300);
   gtk_widget_show_all(detached);
   pump_layout();
   userlist_fit_width(view);
   pump_layout();
   assert(cfg_get_int("ui.userlist.vfo.width", 0) > 220);
   horizontal = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scroll));
   assert(gtk_adjustment_get_upper(horizontal) <= gtk_adjustment_get_page_size(horizontal) + 1);
   GdkEventConfigure resized = { .width = 200, .height = 300 };
   room_userlist_configure(detached, &resized, &entry);
   assert(!entry.auto_window_width);
   userlist_fit_width(view);
   pump_layout();
   assert(cfg_get_int("ui.userlist.vfo.width", 0) == 200);
   g_object_set_data(G_OBJECT(view), "rr-room-roster", NULL);
   gtk_widget_destroy(detached);
   gtk_widget_destroy(window);
   puts("PASS: late roster users fit all rig headers; manual sizing disables automatic growth");
}

int main(void) {
   if (!gtk_init_check(NULL, NULL)) {
      puts("SKIP: userlist width test requires a display");

      return 0;
   }
   GtkWidget *window = gtk_offscreen_window_new();
   rrconn_t client = {
      .fd = -1
   };
   rrconn_t selected_websocket = {
      .fd = -1, .is_ws = true
   };
   server_name = "irc-profile";
   roster_connection = &client;
   ws_conn = &selected_websocket; /* The active server differs from this room roster. */
   GtkWidget *view = userlist_view_create();
   userlist_set_columns(view, "#rig");
   assert(gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(view), COL_PRIV_ICON)));

   for (int i = COL_TALK_ICON ; i <= COL_ELMERNOOB_ICON ; i++) {
      assert(!gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(view), i)));
   }

   room_vfos = "rig0.vfo_a";
   userlist_set_columns(view, "#rig");
   assert(gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(view), COL_TALK_ICON)));
   assert(gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(view), COL_MUTE_ICON)));
   assert(gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(view), COL_ELMERNOOB_ICON)));
   irc_process_message(&client, ":server 005 tester RUSTYRIG :supported");
   userlist_set_columns(view, "#rig");

   for (int i = COL_TALK_ICON ; i <= COL_ELMERNOOB_ICON ; i++) {
      assert(gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(view), i)));
   }

   irc_process_message(&client, ":server 005 tester -RUSTYRIG :supported");
   userlist_set_columns(view, "#rig");
   assert(gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(view), COL_TALK_ICON)));
   client.is_ws = true;
   userlist_set_columns(view, "#rig");
   assert(gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(view), COL_TALK_ICON)));
   room_vfos = "";
   userlist_set_columns(view, "#rig");
   assert(!gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(view), COL_TALK_ICON)));
   assert(!gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(view), COL_MUTE_ICON)));
   /* Rig JOIN metadata must expose radio columns before bindings arrive. */
   room_tx_control = true;
   userlist_set_columns(view, "#rig");
   assert(gtk_tree_view_column_get_visible(userlist_column(view, COL_TALK_ICON)));
   assert(gtk_tree_view_column_get_visible(userlist_column(view, COL_MUTE_ICON)));
   assert(gtk_tree_view_column_get_visible(userlist_column(view, COL_ELMERNOOB_ICON)));
   room_tx_control = false;
   room_vfo_mask = 3;
   userlist_set_columns(view, "#rig");
   assert(gtk_tree_view_column_get_visible(userlist_column(view, COL_TALK_ICON)));
   assert(gtk_tree_view_column_get_visible(userlist_column(view, COL_MUTE_ICON)));
   room_vfo_mask = 0;
   userlist_set_columns(view, "#rig");
   assert(!gtk_tree_view_column_get_visible(userlist_column(view, COL_TALK_ICON)));
   irc_capabilities_clear(&client);
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view)));
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroll), view);
   GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
   GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
   GtkWidget *undock = gtk_button_new_with_label("Undock");
   gtk_box_pack_start(GTK_BOX(header), gtk_label_new("Users"), TRUE, TRUE, 2);
   gtk_box_pack_end(GTK_BOX(header), undock, FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(panel), header, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(panel), scroll, TRUE, TRUE, 0);
   GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   GtkWidget *frequency = gtk_label_new("145.123000 MHz");
   GtkWidget *ptt = gtk_button_new_with_label("PTT OFF");
   gtk_box_pack_start(GTK_BOX(controls), gtk_label_new("VFO A"), FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(controls), frequency, FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(controls), gtk_label_new("FM"), FALSE, FALSE, 2);
   gtk_box_pack_end(GTK_BOX(controls), ptt, FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(panel), controls, FALSE, FALSE, 2);
   gtk_paned_pack1(GTK_PANED(paned), gtk_label_new("Chat"), TRUE, FALSE);
   gtk_paned_pack2(GTK_PANED(paned), panel, FALSE, FALSE);
   gtk_container_add(GTK_CONTAINER(window), paned);
   gtk_window_set_default_size(GTK_WINDOW(window), 800, 240);
   GtkTreeIter iter;
   gtk_list_store_append(store, &iter);
   gtk_list_store_set(store, &iter, COL_USERNAME, "SHORT", -1);
   gtk_widget_show_all(window);
   while (gtk_events_pending()) {
      gtk_main_iteration();
   }
   userlist_fit_width_idle(view);
   while (gtk_events_pending()) {
      gtk_main_iteration();
   }
   gint narrow_width = 0, wide_width = 0;
   gtk_widget_get_preferred_width(scroll, &narrow_width, NULL);
   gtk_list_store_set(store, &iter, COL_USERNAME, "A_MUCH_LONGER_USERNAME", -1);
   while (gtk_events_pending()) {
      gtk_main_iteration();
   }
   userlist_fit_width_idle(view);
   while (gtk_events_pending()) {
      gtk_main_iteration();
   }
   gtk_widget_get_preferred_width(scroll, &wide_width, NULL);
   assert(narrow_width > 0 && wide_width > narrow_width);
   GtkPolicyType horizontal_policy, vertical_policy;
   gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(scroll), &horizontal_policy, &vertical_policy);
   assert(horizontal_policy == GTK_POLICY_NEVER);

   // Controls can grow after the initial allocation (CAT mode/frequency updates).
   gtk_label_set_text(GTK_LABEL(frequency), "145.123000 MHz (independent receiver)");
   gtk_list_store_set(store, &iter, COL_USERNAME, "A_USERNAME_LONG_ENOUGH_TO_REQUIRE_MORE_WIDTH_THAN_THE_VFO_AND_PTT_CONTROL_ROW", -1);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
   userlist_fit_width_idle(view);
   while (gtk_events_pending()) {
      gtk_main_iteration();
   }

   for (int i = 0 ; i < 50 ; i++) {
      while (gtk_events_pending()) {
         gtk_main_iteration();
      }
      g_usleep(1000);
   }

   GtkRequisition panel_natural, view_natural;
   gtk_widget_get_preferred_size(panel, NULL, &panel_natural);
   gtk_widget_get_preferred_size(view, NULL, &view_natural);
   assert(gtk_widget_get_allocated_width(panel) >= panel_natural.width);
   assert(gtk_widget_get_allocated_width(view) >= view_natural.width);
   GtkWidget *buttons[] = {
      undock, ptt
   };

   for (size_t i = 0 ; i < G_N_ELEMENTS(buttons) ; i++) {
      gint x, y;
      assert(gtk_widget_translate_coordinates(buttons[i], panel, 0, 0, &x, &y));
      assert(x >= 0 && x + gtk_widget_get_allocated_width(buttons[i]) <= gtk_widget_get_allocated_width(panel));
   }

   /* A message from another server must not change this roster's columns. */
   room_vfos = "rig0.vfo_a";
   client.is_ws = true;
   ws_conn = &selected_websocket;
   userlist_redraw_view(view, "#rig");
   assert(!gtk_tree_view_column_get_visible(userlist_column(view, COL_TALK_ICON)));
   ws_conn = &client;
   userlist_redraw_view(view, "#rig");
   assert(gtk_tree_view_column_get_visible(userlist_column(view, COL_TALK_ICON)));
   room_vfos = "";
   cfg = dict_new();
   dict_add(cfg, "ui.userlist.room.columns", "4,1,0,3,2");
   userlist_set_columns(view, "#rig");
   assert(gtk_tree_view_get_column(GTK_TREE_VIEW(view), 0) == userlist_column(view, COL_ELMERNOOB_ICON));
   assert(!gtk_tree_view_column_get_visible(userlist_column(view, COL_TALK_ICON)));
   room_vfos = "rig0.vfo_a";
   userlist_set_columns(view, "#rig");
   assert(gtk_tree_view_column_get_visible(userlist_column(view, COL_TALK_ICON)));
   gtk_tree_view_move_column_after(GTK_TREE_VIEW(view), userlist_column(view, COL_USERNAME), NULL);
   assert(!strcmp(cfg_get("ui.userlist.vfo.columns"), "1,4,0,3,2"));
   room_vfos = "";
   userlist_set_columns(view, "#rig");
   assert(gtk_tree_view_get_column(GTK_TREE_VIEW(view), 0) == userlist_column(view, COL_ELMERNOOB_ICON));
   gtk_widget_destroy(window);
   test_late_roster_autosize();
   puts("PASS: userlist fits usernames, scrollbar chrome and growing Undock/PTT controls");

   return 0;
}
