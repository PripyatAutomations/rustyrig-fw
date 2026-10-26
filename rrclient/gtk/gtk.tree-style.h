/* Shared, theme-derived GTK3 TreeView stripe styling. */
#ifndef RR_GTK_TREE_STYLE_H
#define RR_GTK_TREE_STYLE_H
#include <gtk/gtk.h>

/* Match the admin tables' 8.5% foreground/base color mix.\n * Use the same source for every list view, including filtered/sorted models.
 * Keep normal selection/focus rendering under GTK theme control. */
static inline void rr_gtk_tree_style(GtkWidget *tree) {
   if (!GTK_IS_TREE_VIEW(tree))
      return;
   gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(tree), TRUE);
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css,
      "treeview.view { -GtkTreeView-even-row-color: @theme_base_color; "
      "-GtkTreeView-odd-row-color: mix(@theme_base_color, @theme_fg_color, 0.085); }",
      -1, NULL);
   gtk_style_context_add_provider(gtk_widget_get_style_context(tree),
      GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(css);
}
#endif
