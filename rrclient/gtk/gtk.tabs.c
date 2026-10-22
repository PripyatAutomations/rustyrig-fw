/* GTK-only tab presentation; the notebook remains the page/shortcut owner. */
#include <rrclient/gtk/gtk.tabs.h>

/* Native side tabs wrap their label in an event box; wrapped tabs keep it directly. */
static GtkWidget *tab_source_label(GtkNotebook *notebook, GtkWidget *page) {
   GtkWidget *label = gtk_notebook_get_tab_label(notebook, page);

   if (GTK_IS_EVENT_BOX(label)) {
      label = gtk_bin_get_child(GTK_BIN(label));
   }

   return GTK_IS_LABEL(label) ? label : NULL;
}

/* Reading a tab clears both ordinary unread and nickname-mention markers. */
static void tab_activity_read(GtkNotebook *notebook, GtkWidget *page, guint number, gpointer data) {
   (void)number;
   (void)data;
   const char *title = g_object_get_data(G_OBJECT(page), "rr-tab-base-title");
   GtkWidget *label = tab_source_label(notebook, page);

   if (title && label) {
      gtk_label_set_text(GTK_LABEL(label), title);
   }
   g_object_set_data(G_OBJECT(page), "rr-tab-unread", NULL);
}

/* Mark background messages without selecting pages or overriding server colors. */
void gtk_tabs_page_activity(GtkNotebook *notebook, GtkWidget *page, gboolean mention) {
   if (gtk_notebook_get_nth_page(notebook, gtk_notebook_get_current_page(notebook)) == page) {
      return;
   }
   GtkWidget *label = tab_source_label(notebook, page);

   if (!label) {
      return;
   }
   const char *title = g_object_get_data(G_OBJECT(page), "rr-tab-base-title");

   if (!title) {
      title = g_strdup(gtk_label_get_text(GTK_LABEL(label)));
      g_object_set_data_full(G_OBJECT(page), "rr-tab-base-title", (gpointer) title, g_free);
   }
   int unread = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(page), "rr-tab-unread"));
   int next = mention || unread == 2 ? 2 : 1;

   if (unread == next) {
      return;
   }
   g_object_set_data(G_OBJECT(page), "rr-tab-unread", GINT_TO_POINTER(next));
   char *escaped = g_markup_escape_text(title, -1);
   char *marked = g_strdup_printf(next == 2 ? "<b>! %s</b>" : "• %s", escaped);
   gtk_label_set_markup(GTK_LABEL(label), marked);
   g_free(marked);
   g_free(escaped);
}

typedef struct tab_color {
   unsigned r, g, b;
   gboolean light;
} tab_color_t;

static guint32 tab_hash(const char *name) {
   guint32 hash = 2166136261u;

   for (const unsigned char *p = (const unsigned char *)name ; *p ; p++) {
      hash = (hash ^ *p) * 16777619u;
   }

   /* Avalanche similar profile names before selecting hue and brightness. */
   hash ^= hash >> 16;
   hash *= 0x7feb352du;
   hash ^= hash >> 15;
   hash *= 0x846ca68bu;

   return hash ^ (hash >> 16);
}

static tab_color_t tab_color_candidate(guint32 hash, unsigned attempt) {
   unsigned hue = (hash % 360 + attempt * 137) % 360;
   gboolean light = ((hash >> 12) ^ attempt) & 1;
   unsigned saturation = light ? 25 + (hash >> 16) % 16 : 65 + (hash >> 16) % 21;
   /* White on dark: every component <= 110 (contrast >= 5.1:1). Black on light: every component >= 132 (contrast >= 5.6:1). Bounds guarantee readable text
    * without floating-point luminance work. */
   unsigned value = light ? 220 + (hash >> 24) % 26 : 90 + (hash >> 24) % 21;
   unsigned low = value * (100 - saturation) / 100;
   unsigned rising = low + (value - low) * (hue % 60) / 60;
   unsigned falling = value - (value - low) * (hue % 60) / 60;
   tab_color_t color = {
      .light = light
   };

   switch (hue / 60) {
      case 0: {
         color.r = value;
         color.g = rising;
         color.b = low;
         break;
      }
      case 1: {
         color.r = falling;
         color.g = value;
         color.b = low;
         break;
      }
      case 2: {
         color.r = low;
         color.g = value;
         color.b = rising;
         break;
      }
      case 3: {
         color.r = low;
         color.g = falling;
         color.b = value;
         break;
      }
      case 4: {
         color.r = rising;
         color.g = low;
         color.b = value;
         break;
      }
      default: {
         color.r = value;
         color.g = low;
         color.b = falling;
         break;
      }
   }

   return color;
}

static const tab_color_t *tab_server_color(GtkNotebook *notebook, const char *server) {
   GHashTable *palette = g_object_get_data(G_OBJECT(notebook), "rr-tab-palette");

   if (!palette) {
      palette = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
      g_object_set_data_full(G_OBJECT(notebook), "rr-tab-palette", palette, (GDestroyNotify) g_hash_table_destroy);
   }
   tab_color_t *stored = g_hash_table_lookup(palette, server);

   if (stored) {
      return stored;
   }
   guint32 hash = tab_hash(server);
   unsigned best_distance = 0;
   tab_color_t best = {
      0
   };

   for (unsigned attempt = 0 ; attempt < 32 ; attempt++) {
      tab_color_t candidate = tab_color_candidate(hash, attempt);
      unsigned nearest = G_MAXUINT;
      GHashTableIter iter;
      gpointer value;
      g_hash_table_iter_init(&iter, palette);
      while (g_hash_table_iter_next(&iter, NULL, &value)) {
         tab_color_t *other = value;
         int dr = (int)candidate.r - (int)other->r;
         int dg = (int)candidate.g - (int)other->g;
         int db = (int)candidate.b - (int)other->b;
         unsigned distance = dr * dr + dg * dg + db * db;
         nearest = MIN(nearest, distance);
      }

      if (nearest > best_distance) {
         best = candidate;
         best_distance = nearest;
      }

      if (nearest >= 48 * 48) {
         break;
      }
   }

   stored = g_new(tab_color_t, 1);
   *stored = best;
   g_hash_table_insert(palette, g_strdup(server), stored);

   return stored;
}

static void tab_apply_color(GtkNotebook *notebook, GtkWidget *page, GtkWidget *widget) {
   const char *server = g_object_get_data(G_OBJECT(page), "rr-server-name");

   if (!server || !*server) {
      return;
   }
   const tab_color_t *color = tab_server_color(notebook, server);
   const char *foreground = color->light ? "#000000" : "#ffffff";
   char css[1024];
   g_snprintf(css, sizeof(css), "* {background-image:none; background-color:rgb(%u,%u,%u); color:%s;}"
      "button:checked {box-shadow:inset 0 -3px %s; border-color:%s;}"
      "button:hover {border-color:%s;} button label {color:%s;}", color->r, color->g, color->b, foreground, foreground, foreground, foreground, foreground);
   GtkCssProvider *provider = gtk_css_provider_new();
   gtk_css_provider_load_from_data(provider, css, -1, NULL);
   gtk_style_context_add_provider(gtk_widget_get_style_context(widget), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
   g_object_unref(provider);
   gtk_widget_set_tooltip_text(widget, server);
}

static void tab_native_color(GtkNotebook *notebook, GtkWidget *page, guint number, gpointer data) {
   (void)number;
   (void)data;
   GtkWidget *label = gtk_notebook_get_tab_label(notebook, page);

   if (label) {
      if (!GTK_IS_EVENT_BOX(label)) {
         GtkWidget *events = gtk_event_box_new();
         g_object_ref(label);
         gtk_notebook_set_tab_label(notebook, page, events);
         gtk_container_add(GTK_CONTAINER(events), label);
         g_object_unref(label);
         gtk_tabs_bind_menu(events, notebook, page);
         gtk_widget_show_all(events);
         label = events;
      }
      tab_apply_color(notebook, page, label);
   }
}

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

   for (GList *item = children ; item ; item = item->next) {
      GtkWidget *button = gtk_bin_get_child(GTK_BIN(item->data));
      tab_set_selected(button, g_object_get_data(G_OBJECT(button), "rr-tab-page") == page);
   }

   g_list_free(children);
}

/* Preserve source markup, but replace legacy fixed shortcut prefixes with the page's current visual number. Sources without a prefix (room tabs) work too. */
static void tab_title_changed(GObject *source, GParamSpec *spec, GtkWidget *button) {
   (void)spec;
   const char *text = gtk_label_get_label(GTK_LABEL(source));
   const char *title = text;

   if (*title == '(') {
      const char *p = title + 1;
      gboolean underline = g_str_has_prefix(p, "<u>");

      if (underline) {
         p += 3;
      }
      const char *digits = p;
      while (g_ascii_isdigit(*p)) {
         p++;
      }

      if (p != digits) {
         if (underline && g_str_has_prefix(p, "</u>")) {
            p += 4;
         }

         if (g_str_has_prefix(p, ") ")) {
            title = p + 2;
         }
      }
   }
   GtkWidget *label = g_object_get_data(G_OBJECT(button), "rr-tab-title");

   /* Lowercase is only a visual treatment: source labels, page IDs and
    * shortcut handling retain their original spelling. */
   char *lower = g_utf8_strdown(title, -1);
   if (gtk_label_get_use_markup(GTK_LABEL(source))) {
      gtk_label_set_markup(GTK_LABEL(label), lower);
   } else {
      gtk_label_set_text(GTK_LABEL(label), lower);
   }
   g_free(lower);
}

static gint tabs_compare(GtkFlowBoxChild *a, GtkFlowBoxChild *b, gpointer data) {
   GtkNotebook *notebook = data;
   GtkWidget *left = gtk_bin_get_child(GTK_BIN(a));
   GtkWidget *right = gtk_bin_get_child(GTK_BIN(b));

   if (g_object_get_data(G_OBJECT(left), "rr-panel-restore")) return 1;
   if (g_object_get_data(G_OBJECT(right), "rr-panel-restore")) return -1;
   return gtk_notebook_page_num(notebook, g_object_get_data(G_OBJECT(left), "rr-tab-page")) -
          gtk_notebook_page_num(notebook, g_object_get_data(G_OBJECT(right), "rr-tab-page"));
}

static void tabs_reordered(GtkNotebook *notebook, GtkWidget *page, guint number, GtkWidget *flow) {
   (void)page;
   (void)number;
   /* Keep the actual drag source alive: sorting reuses buttons. */
   gtk_flow_box_invalidate_sort(GTK_FLOW_BOX(flow));
   GList *children = gtk_container_get_children(GTK_CONTAINER(flow));

   for (GList *item = children ; item ; item = item->next) {
      GtkWidget *button = gtk_bin_get_child(GTK_BIN(item->data));
      if (g_object_get_data(G_OBJECT(button), "rr-panel-restore")) continue;
      int index = gtk_notebook_page_num(notebook, g_object_get_data(G_OBJECT(button), "rr-tab-page")) + 1;
      GtkLabel *label = g_object_get_data(G_OBJECT(button), "rr-tab-number");
      char markup[48];

      if (index <= 10) {
         g_snprintf(markup, sizeof(markup), "(<u>%d</u>)", index == 10 ? 0 : index);
      } else {
         g_snprintf(markup, sizeof(markup), "(%d)", index);
      }
      gtk_label_set_markup(label, markup);
   }

   g_list_free(children);
}

static char tab_target_name[] = "application/x-rustyrig-tab";
static const GtkTargetEntry tab_target = {
   tab_target_name, GTK_TARGET_SAME_APP, 0
};

static void tab_drag_data_get(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection, guint info, guint time, gpointer data) {
   (void)widget;
   (void)context;
   (void)info;
   (void)time;
   (void)data;
   const guchar marker = 1;
   gtk_selection_data_set(selection, gtk_selection_data_get_target(selection), 8, &marker, 1);
}

static gboolean tab_move(GtkNotebook *notebook, GtkWidget *page, GtkWidget *target, gboolean after) {
   int from = gtk_notebook_page_num(notebook, page);
   int to = target ? gtk_notebook_page_num(notebook, target) : gtk_notebook_get_n_pages(notebook);

   if (from < 0 || to < 0) {
      return FALSE;
   }

   if (target && after) {
      to++;
   }

   if (from < to) {
      to--;
   }
   gtk_notebook_reorder_child(notebook, page, to);

   return TRUE;
}

static void tab_drag_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *selection, guint info, guint time, GtkNotebook *
   notebook) {
   (void)info;
   GtkWidget *source = gtk_drag_get_source_widget(context);
   gboolean success = FALSE;

   if (source && gtk_selection_data_get_length(selection) == 1 && g_object_get_data(G_OBJECT(source), "rr-tab-notebook") == notebook) {
      GtkWidget *page = g_object_get_data(G_OBJECT(source), "rr-tab-page");
      GtkWidget *target = widget;

      if (GTK_IS_FLOW_BOX(widget)) {
         GtkFlowBoxChild *child = gtk_flow_box_get_child_at_pos(GTK_FLOW_BOX(widget), x, y);
         target = child ? gtk_bin_get_child(GTK_BIN(child)) : NULL;

         if (target) {
            gint local_y;
            gtk_widget_translate_coordinates(widget, target, x, y, &x, &local_y);
         }
      }
      GtkWidget *target_page = target ? g_object_get_data(G_OBJECT(target), "rr-tab-page") : NULL;
      success = tab_move(notebook, page, target_page, target && x >= gtk_widget_get_allocated_width(target) / 2);
   }
   gtk_drag_finish(context, success, FALSE, time);
}

static void tabs_rebuild(GtkNotebook *notebook, GtkWidget *page, guint number, GtkWidget *flow) {
   (void)page;
   (void)number;
   GList *children = gtk_container_get_children(GTK_CONTAINER(flow));

   for (GList *item = children ; item ; item = item->next) {
      GtkWidget *button = gtk_bin_get_child(GTK_BIN(item->data));
      if (!g_object_get_data(G_OBJECT(button), "rr-panel-restore")) gtk_widget_destroy(item->data);
   }

   g_list_free(children);

   for (int i = 0 ; i < gtk_notebook_get_n_pages(notebook) ; i++) {
      GtkWidget *child = gtk_notebook_get_nth_page(notebook, i);
      GtkWidget *source = gtk_notebook_get_tab_label(notebook, child);
      GtkWidget *button = gtk_toggle_button_new();
      GtkWidget *label = gtk_label_new(NULL);
      /* Long room/server names must not force the entire notebook wider.
       * The full source title remains intact for tooltips and selection. */
      gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
      gtk_label_set_max_width_chars(GTK_LABEL(label), 28);
      GtkWidget *number_label = gtk_label_new(NULL);
      GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
      g_object_set_data(G_OBJECT(button), "rr-tab-title", label);
      g_object_set_data(G_OBJECT(button), "rr-tab-number", number_label);

      if (GTK_IS_LABEL(source)) {
         tab_title_changed(G_OBJECT(source), NULL, button);
         g_signal_connect_object(source, "notify::label", G_CALLBACK(tab_title_changed), button, 0);
         g_signal_connect_object(source, "notify::use-markup", G_CALLBACK(tab_title_changed), button, 0);
      }
      gtk_box_pack_start(GTK_BOX(content), number_label, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
      gtk_container_add(GTK_CONTAINER(button), content);
      tab_apply_color(notebook, child, button);
      gtk_tabs_bind_menu(button, notebook, child);
      gtk_widget_set_focus_on_click(button, FALSE);
      gtk_style_context_add_class(gtk_widget_get_style_context(button), "tab");
      g_object_set_data(G_OBJECT(button), "rr-tab-page", child);
      g_object_set_data(G_OBJECT(button), "rr-tab-notebook", notebook);
      gulong handler = g_signal_connect_object(button, "clicked", G_CALLBACK(tab_clicked), notebook, 0);
      g_object_set_data(G_OBJECT(button), "rr-tab-click-handler", GSIZE_TO_POINTER(handler));
      gtk_drag_source_set(button, GDK_BUTTON1_MASK, &tab_target, 1, GDK_ACTION_MOVE);
      gtk_drag_dest_set(button, GTK_DEST_DEFAULT_ALL, &tab_target, 1, GDK_ACTION_MOVE);
      g_signal_connect(button, "drag-data-get", G_CALLBACK(tab_drag_data_get), NULL);
      g_signal_connect_object(button, "drag-data-received", G_CALLBACK(tab_drag_received), notebook, 0);
      gtk_container_add(GTK_CONTAINER(flow), button);
      gtk_widget_set_can_focus(gtk_widget_get_parent(button), FALSE);
   }

   gtk_flow_box_invalidate_sort(GTK_FLOW_BOX(flow));
   tabs_reordered(notebook, NULL, 0, flow);
   tabs_selected(notebook, gtk_notebook_get_nth_page(notebook, gtk_notebook_get_current_page(notebook)), 0, flow);
   gtk_widget_show_all(flow);
}

GtkWidget *gtk_wrapping_tabs_new(GtkNotebook *notebook, GtkPositionType position) {
   g_signal_connect(notebook, "switch-page", G_CALLBACK(tab_activity_read), NULL);
   gtk_notebook_set_tab_pos(notebook, position);

   if (position == GTK_POS_LEFT || position == GTK_POS_RIGHT) {
      g_signal_connect(notebook, "page-added", G_CALLBACK(tab_native_color), NULL);

      for (int i = 0 ; i < gtk_notebook_get_n_pages(notebook) ; i++) {
         tab_native_color(notebook, gtk_notebook_get_nth_page(notebook, i), i, NULL);
      }

      return GTK_WIDGET(notebook);
   }
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   gtk_widget_set_hexpand(box, TRUE);
   GtkWidget *flow = gtk_flow_box_new();
   g_object_set_data(G_OBJECT(notebook), "rr-floating-tab-strip", flow);
   gtk_widget_set_hexpand(flow, TRUE);
   gtk_widget_set_halign(flow, GTK_ALIGN_FILL);
   gtk_flow_box_set_sort_func(GTK_FLOW_BOX(flow), tabs_compare, notebook, NULL);
   gtk_drag_dest_set(flow, GTK_DEST_DEFAULT_ALL, &tab_target, 1, GDK_ACTION_MOVE);
   g_signal_connect_object(flow, "drag-data-received", G_CALLBACK(tab_drag_received), notebook, 0);
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
   g_signal_connect_object(notebook, "page-reordered", G_CALLBACK(tabs_reordered), flow, 0);
   g_signal_connect_object(notebook, "switch-page", G_CALLBACK(tabs_selected), flow, G_CONNECT_AFTER);
   tabs_rebuild(notebook, NULL, 0, flow);

   return box;
}

/* All numbered and relative navigation follows the same reordered pages. */
gboolean gtk_tabs_select(GtkNotebook *notebook, int number) {
   if (!notebook || number < 1 || number > gtk_notebook_get_n_pages(notebook)) {
      return FALSE;
   }
   gtk_notebook_set_current_page(notebook, number - 1);

   return TRUE;
}

gboolean gtk_tabs_step(GtkNotebook *notebook, int direction) {
   if (!notebook) {
      return FALSE;
   }
   int count = gtk_notebook_get_n_pages(notebook);

   if (count < 1) {
      return FALSE;
   }
   int current = gtk_notebook_get_current_page(notebook);

   return gtk_tabs_select(notebook, (current + (direction < 0 ? count - 1 : 1)) % count + 1);
}

/* A minimized floating panel is a restore action at the end of the tab
 * strip, not a notebook page (so it never steals a chat tab/shortcut). */
GtkWidget *gtk_tabs_panel_restore_add(GtkNotebook *notebook, const char *title,
                                      GCallback callback, gpointer user_data) {
   GtkWidget *flow = g_object_get_data(G_OBJECT(notebook), "rr-floating-tab-strip");
   if (!GTK_IS_FLOW_BOX(flow)) return NULL;
   GtkWidget *button = gtk_button_new();
   GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   GtkWidget *icon = gtk_image_new_from_icon_name("view-fullscreen", GTK_ICON_SIZE_MENU);
   gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(row), gtk_label_new(title), FALSE, FALSE, 0);
   gtk_container_add(GTK_CONTAINER(button), row);
   g_object_set_data(G_OBJECT(button), "rr-panel-restore", GINT_TO_POINTER(1));
   gtk_widget_set_tooltip_text(button, "Restore floating panel");
   gtk_container_add(GTK_CONTAINER(flow), button);
   g_signal_connect(button, "clicked", callback, user_data);
   gtk_widget_show_all(button);
   gtk_flow_box_invalidate_sort(GTK_FLOW_BOX(flow));
   return gtk_widget_get_parent(button);
}

void gtk_tabs_panel_restore_remove(GtkWidget *tab) {
   if (GTK_IS_WIDGET(tab)) gtk_widget_destroy(tab);
}
