/* Native-widget, per-panel zoom. Unlike a Cairo surface this keeps controls
 * and their event windows interactive at every scale. */
#include <math.h>
#include <string.h>
#include "gtk.scale-container.h"

typedef struct {
   gint width, height, left, right, top, bottom, spacing, row, col, icon;
   guint packing;
   gint applied[11];
   gboolean initialized;
} RRScaleGeometry;

typedef struct {
   GtkWidget *content;
   GtkCssProvider *css;
   double scale;
} RRScaleContainer;

static gint rr_scale_dim(gint base, double scale) {
   if (base < 0) return base;
   return (gint)CLAMP(round(base * scale), 0.0, (double)G_MAXINT);
}
static void rr_scale_widget(GtkWidget *widget, double scale, GtkCssProvider *css) {
   if (css && !g_object_get_data(G_OBJECT(widget), "rr-scale-styled")) {
      gtk_style_context_add_provider(gtk_widget_get_style_context(widget),
         GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
      g_object_set_data(G_OBJECT(widget), "rr-scale-styled", GINT_TO_POINTER(1));
   }
   RRScaleGeometry *g = g_object_get_data(G_OBJECT(widget), "rr-scale-geometry");
   gint raw[11] = {0};
   gtk_widget_get_size_request(widget, &raw[0], &raw[1]);
   raw[2] = gtk_widget_get_margin_start(widget);
   raw[3] = gtk_widget_get_margin_end(widget);
   raw[4] = gtk_widget_get_margin_top(widget);
   raw[5] = gtk_widget_get_margin_bottom(widget);
   if (GTK_IS_BOX(widget)) raw[6] = gtk_box_get_spacing(GTK_BOX(widget));
   if (GTK_IS_GRID(widget)) {
      raw[7] = gtk_grid_get_row_spacing(GTK_GRID(widget));
      raw[8] = gtk_grid_get_column_spacing(GTK_GRID(widget));
   }
   raw[9] = -1;
   if (GTK_IS_IMAGE(widget)) {
      raw[9] = gtk_image_get_pixel_size(GTK_IMAGE(widget));
      if (raw[9] < 0 && gtk_image_get_storage_type(GTK_IMAGE(widget)) == GTK_IMAGE_ICON_NAME) {
         const gchar *name = NULL;
         GtkIconSize size;
         gint h = 0;
         gtk_image_get_icon_name(GTK_IMAGE(widget), &name, &size);
         gtk_icon_size_lookup(size, &raw[9], &h);
      }
   }
   GtkWidget *parent = gtk_widget_get_parent(widget);
   gboolean expand = FALSE, fill = FALSE;
   GtkPackType pack = GTK_PACK_START;
   if (GTK_IS_BOX(parent)) {
      guint padding = 0;
      gtk_box_query_child_packing(GTK_BOX(parent), widget, &expand, &fill, &padding, &pack);
      raw[10] = (gint)padding;
   }
   if (!g) {
      g = g_new0(RRScaleGeometry, 1);
      memcpy(&g->width, raw, sizeof(raw));
      memcpy(g->applied, raw, sizeof(raw));
      g->initialized = TRUE;
      g_object_set_data_full(G_OBJECT(widget), "rr-scale-geometry", g, g_free);
   }
   for (guint i = 0; i < G_N_ELEMENTS(raw); ++i) {
      if (raw[i] != g->applied[i]) ((gint *)&g->width)[i] = raw[i];
      g->applied[i] = rr_scale_dim(((gint *)&g->width)[i], scale);
   }
   gtk_widget_set_size_request(widget, g->applied[0], g->applied[1]);
   gtk_widget_set_margin_start(widget, g->applied[2]);
   gtk_widget_set_margin_end(widget, g->applied[3]);
   gtk_widget_set_margin_top(widget, g->applied[4]);
   gtk_widget_set_margin_bottom(widget, g->applied[5]);
   if (GTK_IS_BOX(widget)) gtk_box_set_spacing(GTK_BOX(widget), g->applied[6]);
   if (GTK_IS_GRID(widget)) {
      gtk_grid_set_row_spacing(GTK_GRID(widget), g->applied[7]);
      gtk_grid_set_column_spacing(GTK_GRID(widget), g->applied[8]);
   }
   if (GTK_IS_IMAGE(widget) && g->applied[9] >= 0)
      gtk_image_set_pixel_size(GTK_IMAGE(widget), g->applied[9]);
   if (GTK_IS_BOX(parent))
      gtk_box_set_child_packing(GTK_BOX(parent), widget, expand, fill,
         (guint)MAX(0,g->applied[10]), pack);
   if (GTK_IS_CONTAINER(widget)) {
      GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
      for (GList *i = children; i; i = i->next)
         rr_scale_widget(GTK_WIDGET(i->data), scale, css);
      g_list_free(children);
   }
}
static void rr_scale_child_added(GtkContainer *container, GtkWidget *child, gpointer user) {
   (void)container;
   RRScaleContainer *ctx = user;
   if (ctx && ctx->scale != 1.0) rr_scale_widget(child, ctx->scale, ctx->css);
}
static void rr_scale_install(GtkWidget *widget, RRScaleContainer *ctx) {
   if (!GTK_IS_CONTAINER(widget)) return;
   if (!g_object_get_data(G_OBJECT(widget), "rr-scale-observed")) {
      g_object_set_data(G_OBJECT(widget), "rr-scale-observed", GINT_TO_POINTER(1));
      g_signal_connect(GTK_CONTAINER(widget), "add", G_CALLBACK(rr_scale_child_added), ctx);
   }
   GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
   for (GList *i = children; i; i = i->next)
      rr_scale_install(GTK_WIDGET(i->data), ctx);
   g_list_free(children);
}
static void rr_scale_dispose(gpointer data) {
   RRScaleContainer *ctx = data;
   g_clear_object(&ctx->css);
   g_free(ctx);
}
GtkWidget *rr_scale_container_new(GtkWidget *content) {
   g_return_val_if_fail(GTK_IS_WIDGET(content), NULL);
   GtkWidget *wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   RRScaleContainer *ctx = g_new0(RRScaleContainer, 1);
   ctx->content = content;
   ctx->scale = 1.0;
   ctx->css = gtk_css_provider_new();
   gtk_style_context_add_class(gtk_widget_get_style_context(wrapper), "rr-scale-container");
   gtk_style_context_add_provider(gtk_widget_get_style_context(wrapper),
      GTK_STYLE_PROVIDER(ctx->css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_set_data_full(G_OBJECT(wrapper), "rr-scale-context", ctx, rr_scale_dispose);
   gtk_box_pack_start(GTK_BOX(wrapper), content, TRUE, TRUE, 0);
   return wrapper;
}
void rr_scale_container_set_zoom(GtkWidget *wrapper, double scale) {
   RRScaleContainer *ctx = g_object_get_data(G_OBJECT(wrapper), "rr-scale-context");
   if (!ctx) return;
   ctx->scale = CLAMP(scale, .10, 3.0);
   char css[240];
   g_snprintf(css, sizeof(css),
      ".rr-scale-container { font-size: %.2fpx; } "
      ".rr-scale-container button, .rr-scale-container entry, "
      ".rr-scale-container combobox { min-height: %.2fpx; padding: %.2fpx; }",
      13.0 * ctx->scale, 20.0 * ctx->scale, 4.0 * ctx->scale);
   gtk_css_provider_load_from_data(ctx->css, css, -1, NULL);
   rr_scale_widget(ctx->content, ctx->scale, ctx->css);
   rr_scale_install(ctx->content, ctx);
   gtk_widget_queue_resize(wrapper);
}
