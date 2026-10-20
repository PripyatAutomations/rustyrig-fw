/* Popup actions retain page identity across reorder, removal and confirmation. */
#include <assert.h>
#include <rrclient/gtk/gtk.tabs.c>
#include <rrclient/gtk/gtk.tabs.menu.c>

static GtkWidget *closed_page;
static unsigned closes;
static gboolean was_forced;
static void close_page(GtkWidget *page, gboolean force) {
   closes++;
   closed_page = page;
   was_forced = force;
}
static GtkWidget *menu_item(GtkWidget *menu, unsigned index) {
   GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
   GtkWidget *item = g_list_nth_data(children, index);
   g_list_free(children);

   return item;
}
static void dialog_destroyed(GtkWidget *dialog, gboolean *destroyed) {
   (void)dialog;
   *destroyed = TRUE;
}
static GtkDialog *confirmation(void) {
   GList *windows = gtk_window_list_toplevels();
   GtkDialog *found = NULL;

   for (GList *item = windows ; item ; item = item->next) {
      if (GTK_IS_MESSAGE_DIALOG(item->data)) {
         found = GTK_DIALOG(item->data);
      }
   }

   g_list_free(windows);
   assert(found);

   return found;
}
static GtkWidget *menu_for(GtkNotebook *notebook, GtkWidget *page) {
   return g_object_ref_sink(tab_close_menu_new(notebook, page));
}
static void free_menu(GtkWidget *menu) {
   gtk_widget_destroy(menu);
   g_object_unref(menu);
}
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   GtkNotebook *notebook = GTK_NOTEBOOK(gtk_notebook_new());
   gtk_container_add(GTK_CONTAINER(window), gtk_wrapping_tabs_new(notebook, GTK_POS_TOP));
   const char *names[] = {
      "Admin", "Config", "Status", "Client log", "Host log"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(names) ; i++) {
      GtkWidget *page = gtk_label_new(names[i]);
      gtk_notebook_append_page(notebook, page, gtk_label_new(names[i]));
      GtkWidget *menu = menu_for(notebook, page);
      assert(!gtk_widget_get_sensitive(menu_item(menu, 0)));
      assert(!gtk_widget_get_sensitive(menu_item(menu, 1)));
      free_menu(menu);
   }

   GtkWidget *first = gtk_label_new("first server room");
   GtkWidget *other = gtk_label_new("other server room");
   gtk_tabs_page_set_close_func(first, close_page);
   gtk_tabs_page_set_close_func(other, close_page);
   gtk_notebook_append_page(notebook, first, gtk_label_new("#same"));
   gtk_notebook_append_page(notebook, other, gtk_label_new("#same"));
   gtk_widget_show_all(window);
   gtk_notebook_set_current_page(notebook, gtk_notebook_page_num(notebook, other));
   gtk_tabs_page_activity(notebook, first, FALSE);
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(first), "rr-tab-unread")) == 1);
   gtk_tabs_page_activity(notebook, first, TRUE);
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(first), "rr-tab-unread")) == 2);
   gtk_notebook_set_current_page(notebook, gtk_notebook_page_num(notebook, first));
   assert(!g_object_get_data(G_OBJECT(first), "rr-tab-unread"));
   assert(!strcmp(gtk_label_get_text(GTK_LABEL(gtk_notebook_get_tab_label(notebook, first))), "#same"));
   GtkWidget *menu = menu_for(notebook, first);
   assert(gtk_widget_get_sensitive(menu_item(menu, 0)));
   gtk_notebook_reorder_child(notebook, first, 1);
   gtk_notebook_set_current_page(notebook, gtk_notebook_page_num(notebook, other));
   gtk_menu_item_activate(GTK_MENU_ITEM(menu_item(menu, 0)));
   assert(closes == 1 && closed_page == first && !was_forced);
   free_menu(menu);

   menu = menu_for(notebook, first);
   gtk_menu_item_activate(GTK_MENU_ITEM(menu_item(menu, 1)));
   assert(closes == 1);         /* A force click alone must never close anything. */
   gtk_dialog_response(confirmation(), GTK_RESPONSE_CANCEL);
   assert(closes == 1);
   free_menu(menu);

   menu = menu_for(notebook, first);
   gtk_menu_item_activate(GTK_MENU_ITEM(menu_item(menu, 1)));
   gtk_notebook_reorder_child(notebook, first, -1);
   gtk_dialog_response(confirmation(), GTK_RESPONSE_ACCEPT);
   assert(closes == 2 && closed_page == first && was_forced);
   free_menu(menu);

   menu = menu_for(notebook, first);
   gtk_menu_item_activate(GTK_MENU_ITEM(menu_item(menu, 1)));
   gtk_notebook_remove_page(notebook, gtk_notebook_page_num(notebook, first));
   gtk_dialog_response(confirmation(), GTK_RESPONSE_ACCEPT);
   assert(closes == 2);         /* PART/disconnect may have removed it while confirming. */
   gtk_menu_item_activate(GTK_MENU_ITEM(menu_item(menu, 0)));
   assert(closes == 2);
   free_menu(menu);
   menu = menu_for(notebook, other);
   gtk_menu_item_activate(GTK_MENU_ITEM(menu_item(menu, 1)));
   gboolean destroyed = FALSE;
   g_signal_connect(confirmation(), "destroy", G_CALLBACK(dialog_destroyed), &destroyed);
   gtk_widget_destroy(window);
   assert(destroyed && closes == 2);   /* Parent shutdown cancels pending actions. */
   free_menu(menu);

   /* Native side labels need event boxes because GtkLabel has no event window. */
   window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   notebook = GTK_NOTEBOOK(gtk_notebook_new());
   gtk_container_add(GTK_CONTAINER(window), gtk_wrapping_tabs_new(notebook, GTK_POS_LEFT));
   GtkWidget *page = gtk_label_new("side room");
   gtk_notebook_append_page(notebook, page, gtk_label_new("side"));
   GtkWidget *label = gtk_notebook_get_tab_label(notebook, page);
   assert(GTK_IS_EVENT_BOX(label));
   assert(g_object_get_data(G_OBJECT(label), "rr-tab-page") == page);
   gtk_widget_destroy(window);
   puts("PASS: protected tabs, clicked-page close, force cancel/confirm, reorder/removal safety and side labels");

   return 0;
}
