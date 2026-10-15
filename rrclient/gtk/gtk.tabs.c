/* GTK-only tab presentation; the notebook remains the page/shortcut owner. */
#include <rrclient/gtk/gtk.tabs.h>

static void tab_set_selected(GtkWidget *button, gboolean selected) {
   gulong handler = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(button), "rr-tab-click-handler"));
   /* GTK emits clicked when set_active changes a toggle. */
   g_signal_handler_block(button, handler);
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), selected);
   g_signal_handler_unblock(button, handler);
}

static void tab_clicked(GtkButton *button, GtkNotebook *notebook) {
   GtkWidget *page = g_object_get_data(G_OBJECT(button), "rr-tab-page");
   int index = gtk_notebook_page_num(notebook, page);
   if (index >= 0) {
      gtk_notebook_set_current_page(notebook, index);
      /* Clicking the selected toggle must leave it selected, too. */
      tab_set_selected(GTK_WIDGET(button), TRUE);
   }
}

static void tabs_selected(GtkNotebook *notebook, GtkWidget *page, guint number, GtkWidget *flow) {
   (void)notebook;
   (void)number;
   GList *children = gtk_container_get_children(GTK_CONTAINER(flow));
   for (GList *item = children; item; item = item->next) {
      GtkWidget *button = gtk_bin_get_child(GTK_BIN(item->data));
      tab_set_selected(button, g_object_get_data(G_OBJECT(button), "rr-tab-page") == page);
   }
   g_list_free(children);
}

static void tabs_rebuild(GtkNotebook *notebook, GtkWidget *page, guint number, GtkWidget *flow) {
   (void)page;
   (void)number;
   GList *children = gtk_container_get_children(GTK_CONTAINER(flow));
   for (GList *item = children; item; item = item->next) {
      gtk_widget_destroy(item->data);
   }
   g_list_free(children);
   for (int i = 0; i < gtk_notebook_get_n_pages(notebook); i++) {
      GtkWidget *child = gtk_notebook_get_nth_page(notebook, i);
      GtkWidget *source = gtk_notebook_get_tab_label(notebook, child);
      GtkWidget *button = gtk_toggle_button_new();
      GtkWidget *label = gtk_label_new(NULL);
      if (GTK_IS_LABEL(source)) {
         /* Labels include unread markup and server/room renames. Bind instead
          * of polling or rebuilding the strip for ordinary label changes. */
         g_object_bind_property(source, "use-markup", label, "use-markup", G_BINDING_SYNC_CREATE);
         g_object_bind_property(source, "label", label, "label", G_BINDING_SYNC_CREATE);
      } else {
         gtk_label_set_text(GTK_LABEL(label), gtk_notebook_get_tab_label_text(notebook, child));
      }
      gtk_container_add(GTK_CONTAINER(button), label);
      gtk_widget_set_focus_on_click(button, FALSE);
      gtk_style_context_add_class(gtk_widget_get_style_context(button), "tab");
      g_object_set_data(G_OBJECT(button), "rr-tab-page", child);
      gulong handler = g_signal_connect_object(button, "clicked", G_CALLBACK(tab_clicked), notebook, 0);
      g_object_set_data(G_OBJECT(button), "rr-tab-click-handler", GSIZE_TO_POINTER(handler));
      gtk_container_add(GTK_CONTAINER(flow), button);
      gtk_widget_set_can_focus(gtk_widget_get_parent(button), FALSE);
   }
   tabs_selected(notebook, gtk_notebook_get_nth_page(notebook, gtk_notebook_get_current_page(notebook)), 0, flow);
   gtk_widget_show_all(flow);
}

GtkWidget *gtk_wrapping_tabs_new(GtkNotebook *notebook, GtkPositionType position) {
   gtk_notebook_set_tab_pos(notebook, position);
   if (position == GTK_POS_LEFT || position == GTK_POS_RIGHT) {
      return GTK_WIDGET(notebook);
   }
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *flow = gtk_flow_box_new();
   gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
   gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 1);
   gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 1000);
   gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 0);
   gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 0);
   gtk_widget_set_valign(flow, GTK_ALIGN_START);
   gtk_notebook_set_show_tabs(notebook, FALSE);
   if (position == GTK_POS_TOP) {
      gtk_box_pack_start(GTK_BOX(box), flow, FALSE, FALSE, 0);
   }
   gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(notebook), TRUE, TRUE, 0);
   if (position == GTK_POS_BOTTOM) {
      gtk_box_pack_start(GTK_BOX(box), flow, FALSE, FALSE, 0);
   }
   g_signal_connect_object(notebook, "page-added", G_CALLBACK(tabs_rebuild), flow, 0);
   g_signal_connect_object(notebook, "page-removed", G_CALLBACK(tabs_rebuild), flow, 0);
   g_signal_connect_object(notebook, "page-reordered", G_CALLBACK(tabs_rebuild), flow, 0);
   g_signal_connect_object(notebook, "switch-page", G_CALLBACK(tabs_selected), flow, G_CONNECT_AFTER);
   tabs_rebuild(notebook, NULL, 0, flow);
   return box;
}
