#include <assert.h>
#include <string.h>
#include <rrclient/gtk/gtk.tabs.c>

static void settle(void) {
   for (int i = 0; i < 30; i++) {
      while (gtk_events_pending()) gtk_main_iteration();
      g_usleep(10000);
   }
}

static GtkWidget *button_at(GtkWidget *flow, int index) {
   return gtk_bin_get_child(GTK_BIN(gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(flow), index)));
}

int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   for (int top = 0; top < 2; top++) {
      GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      GtkNotebook *notebook = GTK_NOTEBOOK(gtk_notebook_new());
      GtkWidget *box = gtk_wrapping_tabs_new(notebook, top ? GTK_POS_TOP : GTK_POS_BOTTOM);
      GList *children = gtk_container_get_children(GTK_CONTAINER(box));
      GtkWidget *flow = top ? children->data : children->next->data;
      g_list_free(children);
      /* Match the real window's zoom viewport, including auto scrollbars. */
      GtkWidget *viewport = gtk_scrolled_window_new(NULL, NULL);
      gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(viewport), 320);
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(viewport), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
      gtk_container_add(GTK_CONTAINER(viewport), box);
      gtk_container_add(GTK_CONTAINER(window), viewport);
      gtk_window_set_default_size(GTK_WINDOW(window), 380, 350);
      for (int i = 0; i < 12; i++) {
         char title[32];
         g_snprintf(title, sizeof(title), "(%d) Receiver", i + 1);
         gtk_notebook_append_page(notebook, gtk_label_new("content"), gtk_label_new(title));
      }
      gtk_widget_show_all(window);
      settle();
      int narrow_height = gtk_widget_get_allocated_height(flow);
      GtkAllocation first, last;
      gtk_widget_get_allocation(GTK_WIDGET(gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(flow), 0)), &first);
      gtk_widget_get_allocation(GTK_WIDGET(gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(flow), 11)), &last);
      assert(last.y > first.y); /* actual multi-row layout */
      gtk_window_resize(GTK_WINDOW(window), 1000, 350);
      settle();
      assert(gtk_widget_get_allocated_height(flow) < narrow_height);
      gtk_button_clicked(GTK_BUTTON(button_at(flow, 8)));
      assert(gtk_notebook_get_current_page(notebook) == 8);
      gtk_button_clicked(GTK_BUTTON(button_at(flow, 8)));
      assert(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button_at(flow, 8))));
      gtk_notebook_set_current_page(notebook, 2);
      assert(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button_at(flow, 2))));
      assert(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button_at(flow, 8))));
      GtkWidget *page = gtk_notebook_get_nth_page(notebook, 2);
      gtk_label_set_markup(GTK_LABEL(gtk_notebook_get_tab_label(notebook, page)), "<b>Unread</b>");
      GtkWidget *label = gtk_bin_get_child(GTK_BIN(button_at(flow, 2)));
      assert(!strcmp(gtk_label_get_text(GTK_LABEL(label)), "Unread"));
      assert(gtk_label_get_use_markup(GTK_LABEL(label)));
      gtk_notebook_reorder_child(notebook, page, 0);
      gtk_button_clicked(GTK_BUTTON(button_at(flow, 0)));
      assert(gtk_notebook_get_current_page(notebook) == 0);
      gtk_notebook_remove_page(notebook, 0);
      assert(!gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(flow), 11));
      gtk_widget_destroy(window);
      settle();
   }
   GtkNotebook *side = GTK_NOTEBOOK(gtk_notebook_new());
   g_object_ref_sink(side);
   assert(gtk_wrapping_tabs_new(side, GTK_POS_LEFT) == GTK_WIDGET(side));
   assert(gtk_notebook_get_show_tabs(side));
   gtk_widget_destroy(GTK_WIDGET(side));
   g_object_unref(side);
   g_print("PASS: wrapping tabs resize, selection, labels, ordering and removal\n");
   return 0;
}
