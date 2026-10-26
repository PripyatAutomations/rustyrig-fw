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
/* GTK themes do not consistently honor TreeView's legacy odd-row CSS.
 * Paint the renderer cells explicitly, using the same 8.5% theme blend as
 * the admin Audit Log. Use the displayed model path so filtering works. */
static inline void rr_gtk_tree_stripe_cell(GtkTreeViewColumn *column,
   GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter,
   gpointer user_data) {
   (void)column;
   GtkWidget *tree = GTK_WIDGET(user_data);
   GtkTreePath *path = gtk_tree_model_get_path(model, iter);
   const gint *indices = path ? gtk_tree_path_get_indices(path) : NULL;
   gboolean odd = indices && (indices[0] & 1);
   gboolean selected = path && gtk_tree_selection_path_is_selected(
      gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), path);
   if (odd && !selected) {
      GtkStyleContext *style = gtk_widget_get_style_context(tree);
      GdkRGBA base, ink, shade;
      gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL, &base);
      gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &ink);
      const double mix = 0.085;
      shade.red = base.red * (1.0 - mix) + ink.red * mix;
      shade.green = base.green * (1.0 - mix) + ink.green * mix;
      shade.blue = base.blue * (1.0 - mix) + ink.blue * mix;
      shade.alpha = 1.0;
      g_object_set(renderer, "cell-background-rgba", &shade,
         "cell-background-set", TRUE, NULL);
   } else {
      g_object_set(renderer, "cell-background-set", FALSE, NULL);
   }
   if (path) gtk_tree_path_free(path);
}
#endif
