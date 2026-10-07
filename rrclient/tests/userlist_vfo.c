#include <assert.h>
#include "rrclient/gtk/gtk.userlist.c"
bool dying, restarting;
rrconn_t *ws_conn;
time_t now;
static GtkWidget *opened_menu;
void gtk_chat_query_add(const char *user) {}
void gtk_menu_popup(GtkMenu *menu, GtkWidget *parent_menu_shell, GtkWidget *parent_menu_item,
   GtkMenuPositionFunc position, gpointer data, guint button, guint32 time) {
   opened_menu = GTK_WIDGET(menu);
}
static char selected_room[128], command[32];
void rrclient_media_room_selected(const char *room) { snprintf(selected_room, sizeof(selected_room), "%s", room); }
bool ws_send_dict(rrconn_t *source, rrconn_t *client, dict *d, int opcode) {
   assert(!strcmp(dict_get((dict *)d, "talk.target", ""), "#site-rig0"));
   snprintf(command, sizeof(command), "%s", dict_get((dict *)d, "talk.data", ""));
   return true;
}
int main(void) {
   room_userlist_entry_t entry = { .docked = false };
   room_vfo_control_t control = { .vfo = 'B', .room = "#site-rig0", .entry = &entry };
   GdkEventButton click = { .type = GDK_BUTTON_RELEASE, .button = 1 };
   assert(!room_vfo_select_clicked(NULL, &click, &control));
   assert(!command[0]);
   entry.docked = true;
   click.button = 3;
   assert(!room_vfo_select_clicked(NULL, &click, &control));
   assert(!command[0]);
   click.button = 1;
   assert(room_vfo_select_clicked(NULL, &click, &control));
   assert(!strcmp(command, "!vfo B"));
   assert(!strcmp(selected_room, "#site-rig0"));
   if (gtk_init_check(NULL, NULL)) {
      GtkWidget *window = gtk_offscreen_window_new();
      GtkWidget *view = userlist_view_create();
      GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view)));
      GtkTreeIter iter;
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, COL_USERNAME, "ALICE", -1);
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, COL_USERNAME, "BOB", -1);
      gtk_container_add(GTK_CONTAINER(window), view);
      gtk_widget_show_all(window);
      for (int i = 0; i < 20; i++) {
         while (gtk_events_pending()) gtk_main_iteration();
         g_usleep(10000);
      }
      GtkTreePath *path = gtk_tree_path_new_from_indices(1, -1);
      GdkRectangle rect;
      gtk_tree_view_get_background_area(GTK_TREE_VIEW(view), path, NULL, &rect);
      int x, y;
      gtk_tree_view_convert_bin_window_to_widget_coords(GTK_TREE_VIEW(view), 5, rect.y + rect.height / 2, &x, &y);
      assert(userlist_touch_context(view, x, y, 0));
      assert(opened_menu);
      GList *items = gtk_container_get_children(GTK_CONTAINER(opened_menu));
      assert(g_list_length(items) == 6);
      for (GList *it = items; it; it = it->next) {
         const char *target = g_object_get_data(G_OBJECT(it->data), "rr-user-target");
         if (target) assert(!strcmp(target, "BOB"));
      }
      g_list_free(items);
      gtk_widget_destroy(opened_menu);
      gtk_tree_path_free(path);
      gtk_widget_destroy(window);
      puts("PASS: touch context coordinates select the tapped row and reuse all right-click actions");
   }
   puts("PASS: docked VFO rows use the shared selector; detached and right-click rows do not");
}
