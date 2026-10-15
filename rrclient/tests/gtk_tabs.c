#include <assert.h>
#include <string.h>
#include <math.h>
#ifdef HAVE_XTEST
#include <gdk/gdkx.h>
#include <X11/extensions/XTest.h>
#endif
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

#ifdef HAVE_XTEST
static void drag_tab(GtkWidget *source, GtkWidget *target) {
   GtkWidget *window = gtk_widget_get_toplevel(source);
   Display *display = gdk_x11_display_get_xdisplay(gtk_widget_get_display(window));
   int root_x, root_y, from_x, from_y, to_x, to_y;
   gdk_window_get_origin(gtk_widget_get_window(window), &root_x, &root_y);
   gtk_widget_translate_coordinates(source, window, gtk_widget_get_allocated_width(source) / 2,
      gtk_widget_get_allocated_height(source) / 2, &from_x, &from_y);
   gtk_widget_translate_coordinates(target, window, gtk_widget_get_allocated_width(target) * 3 / 4,
      gtk_widget_get_allocated_height(target) / 2, &to_x, &to_y);
   XTestFakeMotionEvent(display, -1, root_x + from_x, root_y + from_y, 0);
   XTestFakeButtonEvent(display, 1, True, 0);
   XFlush(display);
   settle();
   for (int step = 1; step <= 8; step++) {
      XTestFakeMotionEvent(display, -1, root_x + from_x + (to_x - from_x) * step / 8,
         root_y + from_y + (to_y - from_y) * step / 8, 0);
      XFlush(display);
      settle();
   }
   XTestFakeButtonEvent(display, 1, False, 0);
   XFlush(display);
   settle();
}
#endif

static double channel_luminance(unsigned value) {
   double s = value / 255.0;
   return s <= 0.04045 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);
}
static void test_colors(GtkNotebook *notebook) {
   const char *names[] = {"libera", "localirc", "localhost", "server1", "server2", "server3", "server4"};
   for (unsigned i = 0 ; i < G_N_ELEMENTS(names) ; i++) {
      const tab_color_t *color = tab_server_color(notebook, names[i]);
      assert(color == tab_server_color(notebook, names[i]));
      double luminance = .2126 * channel_luminance(color->r) + .7152 * channel_luminance(color->g) + .0722 * channel_luminance(color->b);
      double contrast = color->light ? (luminance + .05) / .05 : 1.05 / (luminance + .05);
      assert(contrast >= 4.5);
      for (unsigned j = 0 ; j < i ; j++) {
         const tab_color_t *other = tab_server_color(notebook, names[j]);
         int dr = (int)color->r - (int)other->r, dg = (int)color->g - (int)other->g, db = (int)color->b - (int)other->b;
         assert(dr * dr + dg * dg + db * db >= 48 * 48);
      }
   }
}

int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   for (int top = 0; top < 2; top++) {
      GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      GtkNotebook *notebook = GTK_NOTEBOOK(gtk_notebook_new());
      test_colors(notebook);
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
         GtkWidget *page = gtk_label_new("content");
         g_object_set_data_full(G_OBJECT(page), "rr-server-name", g_strdup(i % 2 ? "libera" : "localirc"), g_free);
         gtk_notebook_append_page(notebook, page, gtk_label_new(title));
      }
      gtk_widget_show_all(window);
      settle();
      GtkStyleContext *style = gtk_widget_get_style_context(button_at(flow, 0));
      GdkRGBA foreground, *background = NULL;
      gtk_style_context_get_color(style, gtk_style_context_get_state(style), &foreground);
      gtk_style_context_get(style, gtk_style_context_get_state(style), "background-color", &background, NULL);
      const tab_color_t *applied = tab_server_color(notebook, "localirc");
      assert(background && fabs(background->red * 255 - applied->r) < 1);
      assert(fabs(background->green * 255 - applied->g) < 1 && fabs(background->blue * 255 - applied->b) < 1);
      assert(foreground.red == (applied->light ? 0 : 1));
      gdk_rgba_free(background);
      int narrow_height = gtk_widget_get_allocated_height(flow);
      GtkAllocation first, last;
      gtk_widget_get_allocation(GTK_WIDGET(gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(flow), 0)), &first);
      gtk_widget_get_allocation(GTK_WIDGET(gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(flow), 11)), &last);
      assert(last.y > first.y); /* actual multi-row layout */
#ifdef HAVE_XTEST
      GtkWidget *dragged_page = gtk_notebook_get_nth_page(notebook, 0);
      drag_tab(button_at(flow, 0), button_at(flow, 7));
      assert(gtk_notebook_get_nth_page(notebook, 7) == dragged_page);
      assert(gtk_notebook_get_nth_page(notebook, gtk_notebook_get_current_page(notebook)) == dragged_page);
#endif
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
      GtkWidget *label = g_object_get_data(G_OBJECT(button_at(flow, 2)), "rr-tab-title");
      assert(!strcmp(gtk_label_get_text(GTK_LABEL(label)), "Unread"));
      assert(gtk_label_get_use_markup(GTK_LABEL(label)));
      /* Drop a tab across rows, before then after other tabs. The drag source
       * widget survives reordering, and every selector uses the visible order. */
      GtkWidget *drag_button = button_at(flow, 2);
      GtkWidget *last_page = gtk_notebook_get_nth_page(notebook, 11);
      assert(tab_move(notebook, page, last_page, TRUE));
      assert(button_at(flow, 11) == drag_button);
      assert(gtk_notebook_get_nth_page(notebook, 11) == page);
      assert(gtk_tabs_select(notebook, 12)); /* /win 12 */
      assert(gtk_notebook_get_nth_page(notebook, gtk_notebook_get_current_page(notebook)) == page);
      assert(gtk_tabs_step(notebook, 1)); /* Alt-Right wraps */
      assert(gtk_notebook_get_current_page(notebook) == 0);
      assert(gtk_tabs_step(notebook, -1)); /* Alt-Left wraps */
      assert(gtk_notebook_get_current_page(notebook) == 11);
      assert(tab_move(notebook, page, gtk_notebook_get_nth_page(notebook, 0), FALSE));
      assert(button_at(flow, 0) == drag_button);
      assert(gtk_tabs_select(notebook, 1)); /* Alt-1 /win 1 */
      assert(gtk_notebook_get_nth_page(notebook, 0) == page);
      GtkLabel *number_label = g_object_get_data(G_OBJECT(drag_button), "rr-tab-number");
      assert(!strcmp(gtk_label_get_text(number_label), "(1)"));
      number_label = g_object_get_data(G_OBJECT(button_at(flow, 9)), "rr-tab-number");
      assert(!strcmp(gtk_label_get_text(number_label), "(0)"));
      assert(gtk_tabs_select(notebook, 10)); /* Alt-0 */
      assert(gtk_notebook_get_current_page(notebook) == 9);
      assert(!gtk_tabs_select(notebook, 0) && !gtk_tabs_select(notebook, 13));
      gtk_button_clicked(GTK_BUTTON(button_at(flow, 0)));
      assert(gtk_notebook_get_current_page(notebook) == 0);
      gtk_notebook_remove_page(notebook, 0);
      number_label = g_object_get_data(G_OBJECT(button_at(flow, 0)), "rr-tab-number");
      assert(!strcmp(gtk_label_get_text(number_label), "(1)"));
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
   g_print("PASS: multi-row tab dragging, numbering, navigation, labels and removal\n");
   return 0;
}
