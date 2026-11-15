//
// rrpipelines/main.c: Standalone GTK GStreamer pipeline editor.
//    This is part of rustyrig-fw.
//
// SPDX-License-Identifier: GPL-2.0-only
#include "pipeline.h"
#include "editor.h"
#include <rrclient/gtk/gtk.graph.h>
#include <librustyaxe/json.h>
#include <gtk/gtk.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RR_PIPELINE_LIMIT 512
#define RR_PIPELINE_INPUT_LIMIT (16 * 1024 * 1024)

enum {
   FACTORY_NAME,
   FACTORY_CLASS,
   FACTORY_DESCRIPTION,
   FACTORY_COLUMNS
};

typedef struct {
   gchar *id;
   gchar *launch;
   gchar *parse_error;
   guint original_index;
   rr_pipeline_graph_t *graph;
} pipeline_entry_t;

typedef struct {
   dict *document;
   GPtrArray *pipelines;
   guint selected_pipeline;
   gint selected_node;
   gboolean dragging, wiring;
   guint wire_source;
   double wire_x, wire_y;
   double drag_x, drag_y;
   GtkWidget *window;
   GtkWidget *pipeline_combo;
   GtkWidget *factory_view;
   GtkWidget *factory_search;
   GtkWidget *canvas;
   GtkWidget *pipeline_status;
   GtkWidget *property_box;
   GtkWidget *property_scroll;
   GPtrArray *property_entries;
   gboolean applied, read_only, embedded;
   gboolean (*apply_callback)(const gchar *, gpointer);
   gpointer apply_data;
   void (*focus_callback)(const gchar *, gpointer);
   gpointer focus_data;
} editor_t;

static void show_error(editor_t *editor, const gchar *message) {
   GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(editor->window)), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE, "%s", message ? message : "Pipeline editor error");
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
}

static void pipeline_entry_free(gpointer data) {
   pipeline_entry_t *entry = data;

   if (!entry) {
      return;
   }
   g_free(entry->id);
   g_free(entry->launch);
   g_free(entry->parse_error);
   rr_pipeline_graph_free(entry->graph);
   g_free(entry);
}

static pipeline_entry_t *active_pipeline(editor_t *editor) {
   return editor && editor->pipelines && editor->selected_pipeline < editor->pipelines->len
      ? g_ptr_array_index(editor->pipelines, editor->selected_pipeline) : NULL;
}

static void refresh_canvas(editor_t *editor);
static void refresh_properties(editor_t *editor);
static gboolean flush_properties(editor_t *editor, GError **error);
static gboolean flush_properties_with_dialog(editor_t *editor);
static gboolean json_key_pipeline_index(const gchar *key, guint *index, const gchar **suffix);

static gboolean pipeline_id_valid(const gchar *id) {
   if (!id || !*id || strlen(id) > 127) {
      return FALSE;
   }

   for (const gchar *p = id ; *p ; p++) {
      if (g_ascii_isspace(*p) || *p == '=' || *p == '[' || *p == ']') {
         return FALSE;
      }
   }

   return TRUE;
}

static gboolean pipeline_id_exists(editor_t *editor, const gchar *id) {
   for (guint i = 0 ; i < editor->pipelines->len ; i++) {
      pipeline_entry_t *entry = g_ptr_array_index(editor->pipelines, i);

      if (!g_strcmp0(entry->id, id)) {
         return TRUE;
      }
   }

   return FALSE;
}

static gchar *input_read_all(GError **error) {
   GString *text = g_string_sized_new(4096);
   gchar buffer[8192];
   size_t n;
   while ((n = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
      if (text->len + n > RR_PIPELINE_INPUT_LIMIT) {
         g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Pipeline JSON exceeds the 16 MiB input limit");
         g_string_free(text, TRUE);

         return NULL;
      }
      g_string_append_len(text, buffer, n);
   }

   if (ferror(stdin)) {
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot read pipeline JSON from stdin");
      g_string_free(text, TRUE);

      return NULL;
   }

   return g_string_free(text, FALSE);
}

static gboolean document_load(editor_t *editor, const gchar *provided, GError **error) {
   gchar *input = provided ? g_strdup(provided) : input_read_all(error);

   if (!input) {
      return FALSE;
   }

   if (!g_utf8_validate(input, -1, NULL)) {
      g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Pipeline JSON on stdin is not valid UTF-8");
      g_free(input);

      return FALSE;
   }
   editor->document = json2dict(input);
   g_free(input);

   if (!editor->document) {
      g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Cannot parse pipeline JSON with librustyaxe");

      return FALSE;
   }
   val_type_t count_type = dict_get_type(editor->document, "pipelines.count");
   guint count = 0;

   if (count_type != VAL_END) {
      if (count_type != VAL_INT && count_type != VAL_UINT && count_type != VAL_LONG && count_type != VAL_ULONG &&
         count_type != VAL_LLONG && count_type != VAL_ULLONG) {
         g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Pipeline JSON has an invalid pipelines.count");

         return FALSE;
      }
      unsigned long parsed = dict_get_ulong(editor->document, "pipelines.count", ULONG_MAX);

      if (parsed > RR_PIPELINE_LIMIT) {
         g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Pipeline JSON has an invalid pipelines.count");

         return FALSE;
      }
      count = (guint) parsed;
   }
   int rank = 0;
   const gchar *key;
   char *value;
   while ((rank = dict_enumerate(editor->document, rank, &key, &value)) >= 0) {
      if (!*key || strchr(key, '[')) {
         g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "The librustyaxe JSON contract requires an object-only envelope; arrays cannot be round-tripped");

         return FALSE;
      }

      if (g_str_has_prefix(key, "pipelines.") && strcmp(key, "pipelines.count")) {
         guint index = 0;
         const gchar *suffix = NULL;

         if (count_type == VAL_END || !json_key_pipeline_index(key, &index, &suffix) || index >= count || !*suffix) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Pipeline JSON contains an invalid indexed pipeline field");

            return FALSE;
         }
      }
   }

   for (guint i = 0 ; i < count ; i++) {
      gchar *id_key = g_strdup_printf("pipelines.%u.id", i);
      gchar *launch_key = g_strdup_printf("pipelines.%u.launch", i);
      const gchar *id = dict_get(editor->document, id_key, NULL);
      const gchar *launch = dict_get(editor->document, launch_key, NULL);

      if (!pipeline_id_valid(id) || !launch || pipeline_id_exists(editor, id)) {
         g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Pipeline entry %u has a missing, invalid, or duplicate id/launch", i);
         g_free(id_key);
         g_free(launch_key);

         return FALSE;
      }
      pipeline_entry_t *entry = g_new0(pipeline_entry_t, 1);
      entry->id = g_strdup(id);
      entry->launch = g_strdup(launch);
      entry->original_index = i;
      entry->graph = rr_pipeline_graph_parse(launch, error);

      if (!entry->graph) {
         entry->parse_error = g_strdup(error && *error ? (*error)->message : "Unsupported pipeline syntax");

         if (error) {
            g_clear_error(error);
         }
      }
      g_free(id_key);
      g_free(launch_key);

      if (!entry->id || !entry->launch || (!entry->graph && !entry->parse_error)) {
         pipeline_entry_free(entry);
         g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Cannot store pipeline entry %u", i);

         return FALSE;
      }
      g_ptr_array_add(editor->pipelines, entry);
   }

   return TRUE;
}

static gboolean factory_filter_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   editor_t *editor = data;
   const gchar *name = NULL, *klass = NULL, *description = NULL;
   gtk_tree_model_get(model, iter, FACTORY_NAME, &name, FACTORY_CLASS, &klass, FACTORY_DESCRIPTION, &description, -1);
   gchar *query = g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(editor->factory_search)), -1);
   gchar *raw = g_strdup_printf("%s %s %s", name ? name : "", klass ? klass : "", description ? description : "");
   gchar *haystack = g_utf8_casefold(raw, -1);
   gboolean visible = !*query || strstr(haystack, query) != NULL;
   g_free(query);
   g_free(raw);
   g_free(haystack);
   g_free((gpointer) name);
   g_free((gpointer) klass);
   g_free((gpointer) description);

   return visible;
}

static gint factory_feature_compare(gconstpointer left, gconstpointer right) {
   const gchar *a = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE((gpointer) left));
   const gchar *b = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE((gpointer) right));

   return g_strcmp0(a, b);
}

static void factory_search_changed(GtkEditable *editable, gpointer data) {
   (void)editable;
   editor_t *editor = data;
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(editor->factory_view));
   gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(model));
}

static void append_registry_factories(GtkListStore *store) {
   GList *features = gst_registry_get_feature_list(gst_registry_get(), GST_TYPE_ELEMENT_FACTORY);
   features = g_list_sort(features, factory_feature_compare);

   for (GList *item = features ; item ; item = item->next) {
      GstElementFactory *factory = item->data;
      GtkTreeIter iter;
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, FACTORY_NAME, gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)), FACTORY_CLASS, gst_element_factory_get_klass(
         factory), FACTORY_DESCRIPTION, gst_element_factory_get_description(factory), -1);
   }

   gst_plugin_feature_list_free(features);
}

static void refresh_pipeline_combo(editor_t *editor) {
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(editor->pipeline_combo));

   for (guint i = 0 ; i < editor->pipelines->len ; i++) {
      pipeline_entry_t *entry = g_ptr_array_index(editor->pipelines, i);
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(editor->pipeline_combo), entry->id);
   }

   if (editor->pipelines->len) {
      if (editor->selected_pipeline >= editor->pipelines->len) {
         editor->selected_pipeline = editor->pipelines->len - 1;
      }
      gtk_combo_box_set_active(GTK_COMBO_BOX(editor->pipeline_combo), (gint) editor->selected_pipeline);
   }
}

static void pipeline_selected(GtkComboBox *combo, gpointer data) {
   editor_t *editor = data;
   gint selected = gtk_combo_box_get_active(combo);

   if (selected < 0 || (guint) selected >= editor->pipelines->len) {
      return;
   }

   if ((guint) selected == editor->selected_pipeline) {
      return;
   }

   if (!flush_properties_with_dialog(editor)) {
      gtk_combo_box_set_active(combo, (gint) editor->selected_pipeline);

      return;
   }
   editor->selected_pipeline = (guint) selected;
   editor->selected_node = -1;
   refresh_canvas(editor);
   refresh_properties(editor);
}

static void pipeline_new(GtkButton *button, gpointer data) {
   (void)button;
   editor_t *editor = data;

   if (!flush_properties_with_dialog(editor)) {
      return;
   }
   GtkWidget *dialog = gtk_dialog_new_with_buttons("New pipeline", GTK_WINDOW(gtk_widget_get_toplevel(editor->window)), GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Create",
      GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Pipeline id (for example custom.rx)");
   gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
   gtk_container_set_border_width(GTK_CONTAINER(entry), 8);
   gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, TRUE, TRUE, 0);
   gtk_widget_show_all(dialog);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      const gchar *id = gtk_entry_get_text(GTK_ENTRY(entry));

      if (!pipeline_id_valid(id) || pipeline_id_exists(editor, id)) {
         show_error(editor, "Choose a unique pipeline id with no spaces or '=' characters.");
      } else {
         pipeline_entry_t *pipeline = g_new0(pipeline_entry_t, 1);
         pipeline->id = g_strdup(id);
         pipeline->launch = g_strdup("");
         pipeline->original_index = G_MAXUINT;
         pipeline->graph = rr_pipeline_graph_new();

         if (!pipeline->id || !pipeline->launch || !pipeline->graph) {
            pipeline_entry_free(pipeline);
            show_error(editor, "Cannot allocate a new pipeline.");
         } else {
            g_ptr_array_add(editor->pipelines, pipeline);
            editor->selected_pipeline = editor->pipelines->len - 1;
            editor->selected_node = -1;
            refresh_pipeline_combo(editor);
            refresh_canvas(editor);
            refresh_properties(editor);
                  }
      }
   }
   gtk_widget_destroy(dialog);
}

static void node_select(editor_t *editor, gint index) {
   editor->selected_node = index;
   refresh_canvas(editor);
   refresh_properties(editor);
}

static gboolean canvas_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
   (void)widget;
   editor_t *editor = data;
   pipeline_entry_t *pipeline = active_pipeline(editor);

   if (!pipeline || !pipeline->graph) {
      return FALSE;
   }
   guint count = rr_pipeline_graph_node_count(pipeline->graph);
   guint link_count = rr_pipeline_graph_link_count(pipeline->graph);
   rr_gtk_graph_node_t *nodes = g_new0(rr_gtk_graph_node_t, count);
   rr_gtk_graph_edge_t *edges = g_new0(rr_gtk_graph_edge_t, link_count);

   for (guint i = 0 ; i < count ; i++) {
      const rr_pipeline_node_t *node = rr_pipeline_graph_node(pipeline->graph, i);
      nodes[i] = (rr_gtk_graph_node_t) {
         .x = node->x, .y = node->y, .title = rr_pipeline_node_factory(node), .subtitle = rr_pipeline_node_name(node), .selected = (gint) i == editor->
            selected_node
      };
   }

   for (guint i = 0 ; i < link_count ; i++) {
      const rr_pipeline_link_t *link = rr_pipeline_graph_link(pipeline->graph, i);
      edges[i] = (rr_gtk_graph_edge_t) {
         .from = link->src, .to = link->dst, .kind = RR_GTK_GRAPH_NORMAL
      };
   }

   rr_gtk_graph_draw_vertical(cr, nodes, count, edges, link_count);
   if (editor->wiring && editor->wire_source < count) {
      const rr_pipeline_node_t *from =
         rr_pipeline_graph_node(pipeline->graph, editor->wire_source);
      double sx = from->x + RR_GTK_GRAPH_NODE_W / 2.;
      double sy = from->y + RR_GTK_GRAPH_NODE_H;
      cairo_set_source_rgb(cr, .34, .78, .93);
      cairo_set_line_width(cr, 2.);
      cairo_move_to(cr, sx, sy);
      cairo_curve_to(cr, sx, (sy + editor->wire_y) / 2.,
         editor->wire_x, (sy + editor->wire_y) / 2.,
         editor->wire_x, editor->wire_y);
      cairo_stroke(cr);
   }
   g_free(edges);
   g_free(nodes);

   return FALSE;
}

static gboolean canvas_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;

   if (event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   editor_t *editor = data;

   if (!flush_properties_with_dialog(editor)) {
      return TRUE;
   }
   pipeline_entry_t *pipeline = active_pipeline(editor);
   editor->selected_node = -1;
   editor->dragging = FALSE;

   if (pipeline) {
      for (guint i = 0 ; i < rr_pipeline_graph_node_count(pipeline->graph) ; i++) {
         const rr_pipeline_node_t *node = rr_pipeline_graph_node(pipeline->graph, i);

         double px = node->x + RR_GTK_GRAPH_NODE_W / 2.;
         double py = node->y + RR_GTK_GRAPH_NODE_H;
         if (!editor->read_only && fabs(event->x - px) <= 14. &&
             fabs(event->y - py) <= 14.) {
            editor->wiring = TRUE;
            editor->wire_source = i;
            editor->wire_x = event->x;
            editor->wire_y = event->y;
            editor->selected_node = (gint)i;
            break;
         }
         if (event->x >= node->x && event->x <= node->x + RR_GTK_GRAPH_NODE_W && event->y >= node->y && event->y <= node->y + RR_GTK_GRAPH_NODE_H) {
            editor->selected_node = (gint) i;
            editor->dragging = !editor->read_only;
            editor->drag_x = event->x - node->x;
            editor->drag_y = event->y - node->y;
            break;
         }
      }
   }
   refresh_properties(editor);
   gtk_widget_queue_draw(editor->canvas);
   if (event->type == GDK_2BUTTON_PRESS && editor->selected_node >= 0 &&
       editor->embedded && editor->focus_callback && pipeline)
      editor->focus_callback(pipeline->id, editor->focus_data);

   return TRUE;
}

static gboolean canvas_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   editor_t *editor = data;
   editor->dragging = FALSE;
   if (!editor->wiring || event->button != GDK_BUTTON_PRIMARY) return FALSE;
   editor->wiring = FALSE;
   pipeline_entry_t *pipeline = active_pipeline(editor);
   if (pipeline && pipeline->graph) {
      for (guint i = 0; i < rr_pipeline_graph_node_count(pipeline->graph); i++) {
         const rr_pipeline_node_t *node = rr_pipeline_graph_node(pipeline->graph, i);
         double px = node->x + RR_GTK_GRAPH_NODE_W / 2.;
         if (i == editor->wire_source || fabs(event->x - px) > 18. ||
             fabs(event->y - node->y) > 18.) continue;
         GError *error = NULL;
         if (rr_pipeline_graph_add_link(pipeline->graph, editor->wire_source, i, &error))
            rr_pipeline_graph_auto_layout(pipeline->graph);
         else
            show_error(editor, error ? error->message : "Cannot connect those elements.");
         g_clear_error(&error);
         break;
      }
   }
   refresh_canvas(editor);
   return TRUE;
}

static gboolean canvas_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   (void)widget;
   editor_t *editor = data;
   pipeline_entry_t *pipeline = active_pipeline(editor);

   if (editor->wiring) {
      editor->wire_x = event->x;
      editor->wire_y = event->y;
      gtk_widget_queue_draw(editor->canvas);
      return TRUE;
   }
   if (!editor->dragging || !pipeline || editor->selected_node < 0) {
      return FALSE;
   }
   rr_pipeline_node_t *node = rr_pipeline_graph_node_edit(pipeline->graph, (guint) editor->selected_node);
   rr_pipeline_node_set_position(node, MAX(0, event->x - editor->drag_x), MAX(0, event->y - editor->drag_y));
   gtk_widget_queue_draw(editor->canvas);

   return TRUE;
}

static void pipeline_auto_layout(GtkButton *button, gpointer data) {
   (void)button;
   editor_t *editor = data;
   pipeline_entry_t *pipeline = active_pipeline(editor);
   if (!pipeline || !pipeline->graph) return;
   rr_pipeline_graph_auto_layout(pipeline->graph);
   refresh_canvas(editor);
}

static void refresh_canvas(editor_t *editor) {
   pipeline_entry_t *pipeline = active_pipeline(editor);

   if (editor->pipeline_status) {
      if (pipeline && pipeline->parse_error) {
         gchar *message = g_strdup_printf("This pipeline cannot be edited and will be preserved unchanged: %s", pipeline->parse_error);
         gtk_label_set_text(GTK_LABEL(editor->pipeline_status), message);
         gtk_widget_show(editor->pipeline_status);
         g_free(message);
      } else {
         gtk_widget_hide(editor->pipeline_status);
      }
   }
   /* The layout may extend to many branches or processing stages. Allow
    * scrolling instead of silently clipping nodes outside the default area. */
   if (pipeline && pipeline->graph) {
      double right = 1120., bottom = 680.;
      for (guint i = 0; i < rr_pipeline_graph_node_count(pipeline->graph); i++) {
         const rr_pipeline_node_t *n = rr_pipeline_graph_node(pipeline->graph, i);
         right = MAX(right, n->x + RR_GTK_GRAPH_NODE_W + 50.);
         bottom = MAX(bottom, n->y + RR_GTK_GRAPH_NODE_H + 50.);
      }
      gtk_widget_set_size_request(editor->canvas, (gint)right, (gint)bottom);
   }
   gtk_widget_queue_draw(editor->canvas);
}

static void property_box_clear(editor_t *editor) {
   g_ptr_array_set_size(editor->property_entries, 0);
   GList *children = gtk_container_get_children(GTK_CONTAINER(editor->property_box));

   for (GList *item = children ; item ; item = item->next) {
      gtk_widget_destroy(GTK_WIDGET(item->data));
   }

   g_list_free(children);
}

static void refresh_properties(editor_t *editor) {
   property_box_clear(editor);
   pipeline_entry_t *pipeline = active_pipeline(editor);

   if (!pipeline || !pipeline->graph || editor->selected_node < 0) {
      if (editor->property_scroll) gtk_widget_hide(editor->property_scroll);
      return;
   }
   rr_pipeline_node_t *node = rr_pipeline_graph_node_edit(pipeline->graph, (guint) editor->selected_node);

   if (!node) {
      if (editor->property_scroll) gtk_widget_hide(editor->property_scroll);
      return;
   }
   guint count = 0;
   GParamSpec **properties = g_object_class_list_properties(G_OBJECT_GET_CLASS(node->element), &count);

   for (guint i = 0 ; i < count ; i++) {
      GParamSpec *spec = properties[i];

      if (!(spec->flags & G_PARAM_READABLE) || !(spec->flags & G_PARAM_WRITABLE) || (spec->flags & G_PARAM_CONSTRUCT_ONLY) || !strcmp(spec->name, "name")) {
         continue;
      }
      GValue value = G_VALUE_INIT;
      g_value_init(&value, G_PARAM_SPEC_VALUE_TYPE(spec));
      g_object_get_property(G_OBJECT(node->element), spec->name, &value);
      gchar *serialized = gst_value_serialize(&value);
      g_value_unset(&value);

      if (!serialized) {
         continue;
      }
      GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
      GtkWidget *label = gtk_label_new(spec->name);
      gtk_widget_set_size_request(label, 150, -1);
      gtk_label_set_xalign(GTK_LABEL(label), 0);
      gtk_widget_set_tooltip_text(label, g_param_spec_get_blurb(spec));
      GtkWidget *entry = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(entry), serialized);
      gtk_editable_set_editable(GTK_EDITABLE(entry), !editor->read_only);
      gtk_widget_set_hexpand(entry, TRUE);
      g_object_set_data_full(G_OBJECT(entry), "rr-property-name", g_strdup(spec->name), g_free);
      g_object_set_data_full(G_OBJECT(entry), "rr-property-original", g_strdup(serialized), g_free);
      g_object_set_data(G_OBJECT(entry), "rr-property-node", GUINT_TO_POINTER((guint) editor->selected_node + 1));
      gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(row), entry, TRUE, TRUE, 0);
      gtk_box_pack_start(GTK_BOX(editor->property_box), row, FALSE, FALSE, 0);
      g_ptr_array_add(editor->property_entries, entry);
      g_free(serialized);
   }

   g_free(properties);
   gtk_widget_show_all(editor->property_box);
   if (editor->property_scroll)
      gtk_widget_show(editor->property_scroll);
}

static gboolean flush_properties(editor_t *editor, GError **error) {
   pipeline_entry_t *pipeline = active_pipeline(editor);

   if (!pipeline || !pipeline->graph) {
      return TRUE;
   }

   for (guint i = 0 ; i < editor->property_entries->len ; i++) {
      GtkWidget *entry = g_ptr_array_index(editor->property_entries, i);
      const gchar *property = g_object_get_data(G_OBJECT(entry), "rr-property-name");
      const gchar *original = g_object_get_data(G_OBJECT(entry), "rr-property-original");
      const gchar *value = gtk_entry_get_text(GTK_ENTRY(entry));
      guint node = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(entry), "rr-property-node")) - 1;

      if (g_strcmp0(value, original) && !rr_pipeline_graph_set_property(pipeline->graph, node, property, value, error)) {
         return FALSE;
      }
   }

   return TRUE;
}

static gboolean flush_properties_with_dialog(editor_t *editor) {
   GError *error = NULL;

   if (flush_properties(editor, &error)) {
      return TRUE;
   }
   show_error(editor, error ? error->message : "Pipeline property is invalid.");
   g_clear_error(&error);

   return FALSE;
}

static void add_element_clicked(GtkButton *button, gpointer data) {
   (void)button;
   editor_t *editor = data;

   if (!flush_properties_with_dialog(editor)) {
      return;
   }
   pipeline_entry_t *pipeline = active_pipeline(editor);
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(editor->factory_view));
   GtkTreeModel *model = NULL;
   GtkTreeIter iter;

   if (!pipeline || !pipeline->graph || !gtk_tree_selection_get_selected(selection, &model, &iter)) {
      show_error(editor, "Choose an element from the GStreamer toolbox first.");

      return;
   }
   gchar *factory = NULL;
   gtk_tree_model_get(model, &iter, FACTORY_NAME, &factory, -1);
   guint index = 0;
   GError *error = NULL;

   if (rr_pipeline_graph_add_element(pipeline->graph, factory, 40 + rr_pipeline_graph_node_count(pipeline->graph) % 4 * 190, 40 + rr_pipeline_graph_node_count(
      pipeline->graph) / 4 * 110, &index, &error)) {
      editor->selected_node = (gint) index;
      refresh_canvas(editor);
      refresh_properties(editor);
      } else {
      show_error(editor, error ? error->message : "Cannot add GStreamer element.");
   }
   g_clear_error(&error);
   g_free(factory);
}

static void add_tee_clicked(GtkButton *button, gpointer data) {
   (void)button;
   editor_t *editor = data;

   if (!flush_properties_with_dialog(editor)) {
      return;
   }
   pipeline_entry_t *pipeline = active_pipeline(editor);
   GError *error = NULL;
   guint tee = 0;

   if (editor->selected_node < 0 || !pipeline || !rr_pipeline_graph_add_tee(pipeline->graph, (guint) editor->selected_node, &tee, &error)) {
      show_error(editor, error ? error->message : "Select an element with an existing downstream link first.");
      g_clear_error(&error);

      return;
   }
   editor->selected_node = (gint) tee;
   rr_pipeline_graph_auto_layout(pipeline->graph);
   refresh_canvas(editor);
   refresh_properties(editor);
}

static void remove_node_clicked(GtkButton *button, gpointer data) {
   (void)button;
   editor_t *editor = data;

   if (!flush_properties_with_dialog(editor)) {
      return;
   }
   pipeline_entry_t *pipeline = active_pipeline(editor);
   GError *error = NULL;

   if (!pipeline || editor->selected_node < 0 || !rr_pipeline_graph_remove_node(pipeline ? pipeline->graph : NULL, (guint) editor->selected_node, &error)) {
      show_error(editor, error ? error->message : "Select an element to remove.");
      g_clear_error(&error);

      return;
   }
   editor->selected_node = -1;
   refresh_canvas(editor);
   refresh_properties(editor);
}

static void add_branch_clicked(GtkButton *button, gpointer data) {
   (void)button;
   editor_t *editor = data;
   pipeline_entry_t *pipeline = active_pipeline(editor);
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Add tee branch", GTK_WINDOW(gtk_widget_get_toplevel(editor->window)), GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL,
      "_Add branch", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(entry), "fakesink");
   gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "GStreamer sink element (for example filesink)");
   gtk_container_set_border_width(GTK_CONTAINER(entry), 8);
   gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, TRUE, TRUE, 0);
   gtk_widget_show_all(dialog);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      GError *error = NULL;
      const gchar *sink = gtk_entry_get_text(GTK_ENTRY(entry));

      if (editor->selected_node < 0 || !pipeline ||
         !rr_pipeline_graph_add_branch(pipeline ? pipeline->graph : NULL, (guint) editor->selected_node, sink, &error)) {
         show_error(editor, error ? error->message : "Select a tee before adding a branch.");
         g_clear_error(&error);
      } else {
         refresh_canvas(editor);
         refresh_properties(editor);
            }
   }
   gtk_widget_destroy(dialog);
}

static gboolean json_key_pipeline_index(const gchar *key, guint *index, const gchar **suffix) {
   if (!g_str_has_prefix(key, "pipelines.")) {
      return FALSE;
   }
   const gchar *start = key + strlen("pipelines.");
   gchar *end = NULL;
   guint64 parsed = g_ascii_strtoull(start, &end, 10);

   if (end == start || *end != '.' || parsed > RR_PIPELINE_LIMIT) {
      return FALSE;
   }
   *index = (guint) parsed;
   *suffix = end + 1;

   return TRUE;
}

static gchar *document_serialize(editor_t *editor, GError **error) {
   dict *result = dict_new();

   if (!result || dict_merge(result, editor->document)) {
      dict_free(result);
      g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Cannot copy the pipeline JSON envelope");

      return NULL;
   }
   guint next_index = (guint) dict_get_ulong(editor->document, "pipelines.count", 0);

   for (guint i = 0 ; i < editor->pipelines->len ; i++) {
      pipeline_entry_t *entry = g_ptr_array_index(editor->pipelines, i);
      GError *graph_error = NULL;
      gchar *launch = entry->graph ? rr_pipeline_graph_serialize(entry->graph, &graph_error) : g_strdup(entry->launch);

      if (!launch) {
         g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_SERIALIZE, "%s: %s", entry->id, graph_error ? graph_error->message :
            "cannot serialize pipeline");
         g_clear_error(&graph_error);
         dict_free(result);

         return NULL;
      }
      guint output_index = entry->original_index != G_MAXUINT ? entry->original_index : next_index++;
      gchar *id_key = g_strdup_printf("pipelines.%u.id", output_index);
      gchar *launch_key = g_strdup_printf("pipelines.%u.launch", output_index);
      gboolean ok = dict_add(result, id_key, entry->id) == 0 && dict_add(result, launch_key, launch) == 0;
      g_free(id_key);
      g_free(launch_key);

      if (!ok) {
         g_free(launch);
         dict_free(result);
         g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Cannot store edited pipeline JSON");

         return NULL;
      }
      g_free(launch);
   }

   if (dict_add_uint(result, "pipelines.count", next_index)) {
      dict_free(result);
      g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Cannot store pipeline count");

      return NULL;
   }
   gchar *json = dict2json(result);
   dict_free(result);

   if (!json) {
      g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "librustyaxe could not serialize pipeline JSON");
   }

   return json;
}

static void apply_clicked(GtkButton *button, gpointer data) {
   (void)button;
   editor_t *editor = data;
   GError *error = NULL;

   if (!flush_properties(editor, &error)) {
      show_error(editor, error ? error->message : "Pipeline property is invalid.");
      g_clear_error(&error);

      return;
   }
   gchar *json = document_serialize(editor, &error);

   if (!json) {
      show_error(editor, error ? error->message : "Cannot serialize pipelines.");
      g_clear_error(&error);

      return;
   }

   if (editor->embedded) {
      if (editor->apply_callback && editor->apply_callback(json, editor->apply_data))
         editor->applied = TRUE;
      g_free(json);
      return;
   }
   if (fprintf(stdout, "%s\n", json) < 0 || fflush(stdout) == EOF) {
      show_error(editor, "Cannot write pipeline JSON to stdout.");
      g_free(json);

      return;
   }
   editor->applied = TRUE;
   g_free(json);
   gtk_main_quit();
}

static void cancel_clicked(GtkButton *button, gpointer data) {
   (void)button;
   (void)data;
   gtk_main_quit();
}

static gboolean window_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   (void)widget;
   (void)event;
   (void)data;
   gtk_main_quit();

   return TRUE;
}

static GtkWidget *build_factory_toolbox(editor_t *editor) {
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
   gtk_widget_set_size_request(box, 300, -1);
   editor->factory_search = gtk_search_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(editor->factory_search), "Search GStreamer elements…");
   gtk_box_pack_start(GTK_BOX(box), editor->factory_search, FALSE, FALSE, 4);
   GtkListStore *store = gtk_list_store_new(FACTORY_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
   append_registry_factories(store);
   GtkTreeModel *filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL);
   gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter), factory_filter_visible, editor, NULL);
   editor->factory_view = gtk_tree_view_new_with_model(filter);
   GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("GStreamer toolbox", renderer, "text", FACTORY_NAME, NULL);
   gtk_tree_view_column_set_resizable(column, TRUE);
   gtk_tree_view_append_column(GTK_TREE_VIEW(editor->factory_view), column);
   gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(editor->factory_view), TRUE);
   gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(editor->factory_view)), GTK_SELECTION_SINGLE);
   GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroller), editor->factory_view);
   gtk_box_pack_start(GTK_BOX(box), scroller, TRUE, TRUE, 0);
   GtkWidget *add = gtk_button_new_with_label("Add element to graph");
   g_signal_connect(add, "clicked", G_CALLBACK(add_element_clicked), editor);
   gtk_box_pack_start(GTK_BOX(box), add, FALSE, FALSE, 4);
   g_signal_connect(editor->factory_search, "changed", G_CALLBACK(factory_search_changed), editor);
   g_object_unref(store);

   return box;
}

/* LV2 control ports are exposed by GStreamer as GObject properties.
 * This enumerates metadata only; actual remote control needs fwdsp IPC and
 * must not be faked by setting GTK's inspection element. */
static void lv2_control_ports_clicked(GtkButton *button, gpointer data) {
   (void)button;
   editor_t *editor = data;
   pipeline_entry_t *pipeline = active_pipeline(editor);
   if (!pipeline || !pipeline->graph || editor->selected_node < 0) {
      show_error(editor, "Select an LV2 effect block to inspect its control ports.");
      return;
   }
   const rr_pipeline_node_t *node =
      rr_pipeline_graph_node(pipeline->graph, (guint) editor->selected_node);
   if (!node || !node->element) return;
   GstElementFactory *factory = gst_element_get_factory(node->element);
   const char *provider = factory ? gst_plugin_feature_get_plugin_name(
      GST_PLUGIN_FEATURE(factory)) : NULL;
   if (g_strcmp0(provider, "lv2")) {
      show_error(editor, "The selected block is not an LV2 effect.");
      return;
   }
   GtkWidget *parent = gtk_widget_get_toplevel(editor->window);
   GtkWidget *dialog = gtk_dialog_new_with_buttons("LV2 Control Ports",
      GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Close", GTK_RESPONSE_CLOSE, NULL);
   gtk_window_set_default_size(GTK_WINDOW(dialog), 590, 380);
   GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *info = gtk_label_new(
      "Blue sockets represent control parameters, not audio connections. "
      "This is the port inventory; routing them to floating widgets requires live fwdsp parameter IPC.");
   gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
   gtk_label_set_xalign(GTK_LABEL(info), 0);
   gtk_box_pack_start(GTK_BOX(content), info, FALSE, FALSE, 8);
   GtkListStore *store = gtk_list_store_new(4,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
   guint count = 0;
   GParamSpec **properties =
      g_object_class_list_properties(G_OBJECT_GET_CLASS(node->element), &count);
   for (guint i = 0; i < count; i++) {
      GParamSpec *spec = properties[i];
      /* Skip framework properties and entries that cannot be set or read. */
      if (spec->owner_type != G_OBJECT_TYPE(node->element) ||
          !strcmp(spec->name, "name") || !strcmp(spec->name, "parent") ||
          !(spec->flags & G_PARAM_READABLE))
         continue;
      const char *type = NULL;
      double low = 0., high = 0.;
      if (G_IS_PARAM_SPEC_FLOAT(spec)) {
         GParamSpecFloat *v = G_PARAM_SPEC_FLOAT(spec);
         type = "Float"; low = v->minimum; high = v->maximum;
      } else if (G_IS_PARAM_SPEC_DOUBLE(spec)) {
         GParamSpecDouble *v = G_PARAM_SPEC_DOUBLE(spec);
         type = "Double"; low = v->minimum; high = v->maximum;
      } else if (G_IS_PARAM_SPEC_INT(spec)) {
         GParamSpecInt *v = G_PARAM_SPEC_INT(spec);
         type = "Integer"; low = v->minimum; high = v->maximum;
      } else if (G_IS_PARAM_SPEC_UINT(spec)) {
         GParamSpecUInt *v = G_PARAM_SPEC_UINT(spec);
         type = "Integer"; low = v->minimum; high = v->maximum;
      } else if (G_IS_PARAM_SPEC_BOOLEAN(spec)) {
         type = "Switch";
      } else if (G_IS_PARAM_SPEC_ENUM(spec)) {
         type = "Choice";
      } else continue;
      gchar *range = (G_IS_PARAM_SPEC_BOOLEAN(spec) || G_IS_PARAM_SPEC_ENUM(spec)) ?
         g_strdup("—") : g_strdup_printf("%.4g … %.4g", low, high);
      const char *direction = (spec->flags & G_PARAM_WRITABLE) ?
         "● Blue input" : "Output (read-only)";
      GtkTreeIter row;
      gtk_list_store_append(store, &row);
      gtk_list_store_set(store, &row, 0, spec->name, 1, type,
                         2, range, 3, direction, -1);
      g_free(range);
   }
   g_free(properties);
   GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
   g_object_unref(store);
   const char *headings[] = {"Control port", "Type", "Range", "Direction"};
   for (guint col = 0; col < G_N_ELEMENTS(headings); col++) {
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
         headings[col], renderer, "text", col, NULL);
      gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
   }
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_vexpand(scroll, TRUE);
   gtk_container_add(GTK_CONTAINER(scroll), view);
   gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 4);
   gtk_widget_show_all(dialog);
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
}

static GtkWidget *build_work_area(editor_t *editor) {
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
   gtk_widget_set_hexpand(box, TRUE);
   gtk_widget_set_vexpand(box, TRUE);
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   /* Connections are made by dragging from a block's output pin to
    * another block's input pin. No redundant From/To selectors. */
   gtk_box_pack_start(GTK_BOX(header), gtk_label_new("Pipeline:"), FALSE, FALSE, 0);
   editor->pipeline_combo = gtk_combo_box_text_new();
   gtk_widget_set_size_request(editor->pipeline_combo, 180, -1);
   gtk_box_pack_start(GTK_BOX(header), editor->pipeline_combo, FALSE, FALSE, 0);
   GtkWidget *new_pipeline = gtk_button_new_with_label("New pipeline");
   g_signal_connect(new_pipeline, "clicked", G_CALLBACK(pipeline_new), editor);
   gtk_box_pack_start(GTK_BOX(header), new_pipeline, FALSE, FALSE, 0);
   GtkWidget *tee = gtk_button_new_with_label("Insert tee + queues");
   g_signal_connect(tee, "clicked", G_CALLBACK(add_tee_clicked), editor);
   gtk_box_pack_start(GTK_BOX(header), tee, FALSE, FALSE, 0);
   GtkWidget *ports = gtk_button_new_with_label("LV2 Controls…");
   gtk_widget_set_tooltip_text(ports,
      "List control parameters and ranges exposed by the selected LV2 effect");
   g_signal_connect(ports, "clicked", G_CALLBACK(lv2_control_ports_clicked), editor);
   gtk_box_pack_start(GTK_BOX(header), ports, FALSE, FALSE, 0);
   GtkWidget *remove_node = gtk_button_new_with_label("Remove block");
   g_signal_connect(remove_node, "clicked", G_CALLBACK(remove_node_clicked), editor);
   gtk_box_pack_start(GTK_BOX(header), remove_node, FALSE, FALSE, 0);
   GtkWidget *branch = gtk_button_new_with_label("Add tee branch…");
   g_signal_connect(branch, "clicked", G_CALLBACK(add_branch_clicked), editor);
   gtk_box_pack_start(GTK_BOX(header), branch, FALSE, FALSE, 0);
   GtkWidget *arrange = gtk_button_new_with_label("Arrange top-down");
   gtk_widget_set_tooltip_text(arrange,
      "Arrange GStreamer blocks by actual pad links: sources above processors and sinks.");
   g_signal_connect(arrange, "clicked", G_CALLBACK(pipeline_auto_layout), editor);
   gtk_box_pack_start(GTK_BOX(header), arrange, FALSE, FALSE, 0);
   /* Long toolbars must not set the window's minimum width. Keep every
    * action available in a horizontally scrollable toolbar instead. */
   GtkWidget *header_scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(header_scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
   gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(header_scroll), FALSE);
   gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(header_scroll), 240);
   gtk_container_add(GTK_CONTAINER(header_scroll), header);
   gtk_box_pack_start(GTK_BOX(box), header_scroll, FALSE, FALSE, 4);
   editor->pipeline_status = gtk_label_new("");
   gtk_label_set_line_wrap(GTK_LABEL(editor->pipeline_status), TRUE);
   gtk_label_set_xalign(GTK_LABEL(editor->pipeline_status), 0);
   gtk_style_context_add_class(gtk_widget_get_style_context(editor->pipeline_status), "error");
   gtk_box_pack_start(GTK_BOX(box), editor->pipeline_status, FALSE, FALSE, 4);

   editor->canvas = gtk_drawing_area_new();
   gtk_widget_set_size_request(editor->canvas, 1120, 680);
   gtk_widget_add_events(editor->canvas, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
   g_signal_connect(editor->canvas, "draw", G_CALLBACK(canvas_draw), editor);
   g_signal_connect(editor->canvas, "button-press-event", G_CALLBACK(canvas_button_press), editor);
   g_signal_connect(editor->canvas, "button-release-event", G_CALLBACK(canvas_button_release), editor);
   g_signal_connect(editor->canvas, "motion-notify-event", G_CALLBACK(canvas_motion), editor);
   GtkWidget *canvas_scroller = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(canvas_scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   /* The graph may be many screens wide. Its content size must never
    * propagate into the pipeline editor's minimum window size. */
   gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(canvas_scroller), FALSE);
   gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(canvas_scroller), FALSE);
   gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(canvas_scroller), 240);
   gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(canvas_scroller), 180);
   gtk_container_add(GTK_CONTAINER(canvas_scroller), editor->canvas);
   gtk_box_pack_start(GTK_BOX(box), canvas_scroller, TRUE, TRUE, 0);

   GtkWidget *properties_scroll = gtk_scrolled_window_new(NULL, NULL);
   editor->property_scroll = properties_scroll;
   /* Only reserve an inspector when a graph element is selected.
    * Otherwise the canvas owns the full remaining tab height. */
   gtk_widget_set_size_request(properties_scroll, -1, 170);
   gtk_widget_set_no_show_all(properties_scroll, TRUE);
   gtk_widget_hide(properties_scroll);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(properties_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(properties_scroll), FALSE);
   gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(properties_scroll), 240);
   editor->property_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   gtk_container_add(GTK_CONTAINER(properties_scroll), editor->property_box);
   gtk_box_pack_start(GTK_BOX(box), properties_scroll, FALSE, TRUE, 0);
   g_signal_connect(editor->pipeline_combo, "changed", G_CALLBACK(pipeline_selected), editor);

   return box;
}

static GtkWidget *build_window(editor_t *editor) {
   editor->window = editor->embedded ? gtk_box_new(GTK_ORIENTATION_VERTICAL, 6) :
                                     gtk_window_new(GTK_WINDOW_TOPLEVEL);
   if (!editor->embedded)
      gtk_window_set_title(GTK_WINDOW(editor->window), editor->read_only ?
         "RustyRig GStreamer Pipeline Viewer (read-only)" : "RustyRig GStreamer Pipeline Editor");
   /* Screen size, not graph size, determines the initial editor window.
    * Keep room for XFCE panels and other desktop work-area reservations. */
   gint width = 1100, height = 760;
   GdkDisplay *display = gdk_display_get_default();
   GdkMonitor *monitor = display ? gdk_display_get_primary_monitor(display) : NULL;
   if (monitor) {
      GdkRectangle area;
      gdk_monitor_get_workarea(monitor, &area);
      width = MIN(width, MAX(480, area.width - 48));
      height = MIN(height, MAX(360, area.height - 48));
   }
   if (!editor->embedded) {
      gtk_window_set_default_size(GTK_WINDOW(editor->window), width, height);
      g_signal_connect(editor->window, "delete-event", G_CALLBACK(window_delete), editor);
   }
   GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
   gtk_widget_set_hexpand(root, TRUE);
   gtk_widget_set_vexpand(root, TRUE);
   gtk_container_set_border_width(GTK_CONTAINER(root), 8);
   GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_widget_set_hexpand(paned, TRUE);
   gtk_widget_set_vexpand(paned, TRUE);
   GtkWidget *toolbox = build_factory_toolbox(editor);
   gtk_paned_pack1(GTK_PANED(paned), toolbox, FALSE, FALSE);

   if (editor->read_only) {
      gtk_widget_set_sensitive(toolbox, FALSE);
   }
   GtkWidget *work = build_work_area(editor);
   gtk_paned_pack2(GTK_PANED(paned), work, TRUE, TRUE);

   if (editor->read_only) {
      GList *children = gtk_container_get_children(GTK_CONTAINER(work));
      GtkWidget *header_scroll = children ? GTK_WIDGET(children->data) : NULL;
      GtkWidget *header = GTK_IS_BIN(header_scroll) ?
         gtk_bin_get_child(GTK_BIN(header_scroll)) : NULL;

      if (header && GTK_IS_CONTAINER(header)) {
         GList *controls = gtk_container_get_children(GTK_CONTAINER(header));

         for (GList *item = controls ; item ; item = item->next) {
            GtkWidget *w = GTK_WIDGET(item->data);

            if (GTK_IS_BUTTON(w) || (GTK_IS_COMBO_BOX(w) && w != editor->pipeline_combo)) {
               gtk_widget_set_sensitive(w, FALSE);
            }
         }

         g_list_free(controls);
      }
      g_list_free(children);
   }
   gtk_paned_set_position(GTK_PANED(paned), 300);
   gtk_box_pack_start(GTK_BOX(root), paned, TRUE, TRUE, 0);
   GtkWidget *buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
   GtkWidget *cancel = gtk_button_new_with_mnemonic("_Cancel");
   GtkWidget *apply = gtk_button_new_with_mnemonic("_Apply");
   gtk_widget_set_can_default(apply, TRUE);
   if (!editor->embedded)
      gtk_window_set_default(GTK_WINDOW(editor->window), apply);
   g_signal_connect(cancel, "clicked", G_CALLBACK(cancel_clicked), editor);
   g_signal_connect(apply, "clicked", G_CALLBACK(apply_clicked), editor);
   gtk_container_add(GTK_CONTAINER(buttons), cancel);
   gtk_container_add(GTK_CONTAINER(buttons), apply);

   if (editor->embedded) {
      gtk_widget_set_no_show_all(cancel, TRUE);
      gtk_widget_hide(cancel);
   }
   if (editor->read_only) {
      gtk_button_set_label(GTK_BUTTON(cancel), "Close");
      gtk_widget_set_no_show_all(apply, TRUE);
      gtk_widget_hide(apply);
   }
   gtk_box_pack_start(GTK_BOX(root), buttons, FALSE, FALSE, 0);
   if (editor->embedded)
      gtk_box_pack_start(GTK_BOX(editor->window), root, TRUE, TRUE, 0);
   else
      gtk_container_add(GTK_CONTAINER(editor->window), root);
   refresh_pipeline_combo(editor);
   refresh_canvas(editor);
   refresh_properties(editor);

   return editor->window;
}

#ifndef RR_PIPELINE_EMBEDDED
int main(int argc, char **argv) {
   gboolean read_only = argc > 1 && !strcmp(argv[1], "--read-only");

   if (read_only) {
      argv++;
      argc--;
   }
   gst_init(&argc, &argv);
   gtk_init(&argc, &argv);
   editor_t editor = {
      0
   };
   editor.read_only = read_only;
   editor.pipelines = g_ptr_array_new_with_free_func(pipeline_entry_free);
   editor.property_entries = g_ptr_array_new();
   GError *error = NULL;

   if (!editor.pipelines || !editor.property_entries || !document_load(&editor, NULL, &error)) {
      fprintf(stderr, "rrpipelines: %s\n", error ? error->message : "Cannot initialize pipeline editor");
      g_clear_error(&error);
      dict_free(editor.document);

      if (editor.pipelines) {
         g_ptr_array_unref(editor.pipelines);
      }

      if (editor.property_entries) {
         g_ptr_array_unref(editor.property_entries);
      }

      return 1;
   }
   build_window(&editor);
   gtk_widget_show_all(editor.window);
   refresh_canvas(&editor);
   gtk_main();
   gtk_widget_destroy(editor.window);
   g_ptr_array_unref(editor.property_entries);
   g_ptr_array_unref(editor.pipelines);
   dict_free(editor.document);

   return editor.applied ? 0 : 2;
}

#else
static void editor_destroy(gpointer user) {
   editor_t *editor = user;
   if (!editor) return;
   if (editor->property_entries) g_ptr_array_unref(editor->property_entries);
   if (editor->pipelines) g_ptr_array_unref(editor->pipelines);
   dict_free(editor->document);
   g_free(editor);
}
GtkWidget *rr_pipeline_editor_new(const gchar *json,
                                  gboolean (*apply)(const gchar *, gpointer),
                                  gpointer apply_data,
                                  void (*focus)(const gchar *, gpointer),
                                  gpointer focus_data, GError **error) {
   if (!json) return NULL;
   if (!gst_is_initialized()) gst_init(NULL, NULL);
   editor_t *editor = g_new0(editor_t, 1);
   editor->embedded = TRUE;
   editor->apply_callback = apply;
   editor->apply_data = apply_data;
   editor->focus_callback = focus;
   editor->focus_data = focus_data;
   editor->pipelines = g_ptr_array_new_with_free_func(pipeline_entry_free);
   editor->property_entries = g_ptr_array_new();
   if (!document_load(editor, json, error)) {
      editor_destroy(editor);
      return NULL;
   }
   GtkWidget *widget = build_window(editor);
   g_object_set_data_full(G_OBJECT(widget), "rr-pipeline-editor", editor,
                          editor_destroy);
   return widget;
}
static gboolean lv2_effect_filter(GtkTreeModel *model, GtkTreeIter *iter, gpointer user);
/* The GTK registry is an editor/catalog only. The instantiated GStreamer
 * LV2 element is ultimately hosted by the standalone fwdsp child when the
 * pipeline configuration is applied and that worker is restarted. */
void rr_pipeline_editor_add_effect(GtkWidget *widget) {
   editor_t *editor = widget ? g_object_get_data(G_OBJECT(widget),
                                                "rr-pipeline-editor") : NULL;
   if (!editor || editor->read_only) return;
   pipeline_entry_t *pipeline = active_pipeline(editor);
   if (!pipeline || !pipeline->graph) {
      show_error(editor, "Select a pipeline before adding an LV2 effect.");
      return;
   }
   if (!flush_properties_with_dialog(editor)) return;
   GtkWidget *owner = gtk_widget_get_toplevel(editor->window);
   GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Add LV2 Effect", GTK_IS_WINDOW(owner) ? GTK_WINDOW(owner) : NULL,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Add Effect", GTK_RESPONSE_ACCEPT, NULL);
   gtk_window_set_default_size(GTK_WINDOW(dialog), 540, 370);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *search = gtk_search_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Find LV2 effect…");
   gtk_box_pack_start(GTK_BOX(area), search, FALSE, FALSE, 6);
   GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
   GList *features = gst_registry_get_feature_list(gst_registry_get(),
                                                    GST_TYPE_ELEMENT_FACTORY);
   guint available = 0;
   for (GList *item = features; item; item = item->next) {
      GstElementFactory *factory = item->data;
      const char *provider = gst_plugin_feature_get_plugin_name(
         GST_PLUGIN_FEATURE(factory));
      if (g_strcmp0(provider, "lv2")) continue;
      GtkTreeIter row;
      gtk_list_store_append(store, &row);
      gtk_list_store_set(store, &row, 0,
         gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)),
         1, gst_element_factory_get_description(factory), -1);
      available++;
   }
   gst_plugin_feature_list_free(features);
   GtkTreeModel *filtered = gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL);
   GtkWidget *view = gtk_tree_view_new_with_model(filtered);
   GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
   gtk_tree_view_append_column(GTK_TREE_VIEW(view),
      gtk_tree_view_column_new_with_attributes("LV2 Effect", renderer, "text", 0, NULL));
   renderer = gtk_cell_renderer_text_new();
   gtk_tree_view_append_column(GTK_TREE_VIEW(view),
      gtk_tree_view_column_new_with_attributes("Description", renderer, "text", 1, NULL));
   gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), TRUE);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_vexpand(scroll, TRUE);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroll), view);
   gtk_box_pack_start(GTK_BOX(area), scroll, TRUE, TRUE, 0);
   GtkWidget *hint = gtk_label_new(available ?
      "The new effect starts disconnected. Connect its audio pins, configure its parameters, then Apply the pipeline." :
      "No GStreamer LV2 effects found. Install the GStreamer LV2 plugin and LV2 bundles on this processing host.");
   gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
   gtk_widget_set_halign(hint, GTK_ALIGN_START);
   gtk_box_pack_start(GTK_BOX(area), hint, FALSE, FALSE, 6);
   gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT,
                                     available != 0);
   /* Refilter while typing without using the general toolbox's search state. */
   g_object_set_data(G_OBJECT(filtered), "rr-lv2-search", search);
   gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filtered),
      (GtkTreeModelFilterVisibleFunc)lv2_effect_filter, search, NULL);
   g_signal_connect_swapped(search, "changed",
      G_CALLBACK(gtk_tree_model_filter_refilter), filtered);
   gtk_widget_show_all(dialog);
   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
      GtkTreeModel *model = NULL;
      GtkTreeIter selected;
      if (gtk_tree_selection_get_selected(selection, &model, &selected)) {
         gchar *factory = NULL;
         gtk_tree_model_get(model, &selected, 0, &factory, -1);
         guint index = 0;
         GError *error = NULL;
         if (rr_pipeline_graph_add_element(pipeline->graph, factory,
             40 + rr_pipeline_graph_node_count(pipeline->graph) % 4 * 190,
             40 + rr_pipeline_graph_node_count(pipeline->graph) / 4 * 110,
             &index, &error)) {
            editor->selected_node = (gint)index;
            refresh_canvas(editor);
            refresh_properties(editor);
         } else {
            show_error(editor, error ? error->message : "Cannot insert LV2 effect.");
         }
         g_clear_error(&error);
         g_free(factory);
      }
   }
   gtk_widget_destroy(dialog);
   g_object_unref(filtered);
   g_object_unref(store);
}
static gboolean lv2_effect_filter(GtkTreeModel *model, GtkTreeIter *iter, gpointer user) {
   GtkWidget *search = user;
   gchar *name = NULL, *description = NULL;
   gtk_tree_model_get(model, iter, 0, &name, 1, &description, -1);
   const gchar *query = gtk_entry_get_text(GTK_ENTRY(search));
   gchar *folded_query = g_utf8_casefold(query, -1);
   gchar *folded_name = g_utf8_casefold(name ? name : "", -1);
   gchar *folded_description = g_utf8_casefold(description ? description : "", -1);
   gboolean matches = strstr(folded_name, folded_query) ||
                      strstr(folded_description, folded_query);
   g_free(name);
   g_free(description);
   g_free(folded_query);
   g_free(folded_name);
   g_free(folded_description);
   return matches;
}
void rr_pipeline_editor_select(GtkWidget *widget, const gchar *id) {
   editor_t *editor = widget ? g_object_get_data(G_OBJECT(widget),
                                                "rr-pipeline-editor") : NULL;
   if (!editor || !id) return;
   for (guint i = 0; i < editor->pipelines->len; i++) {
      pipeline_entry_t *entry = g_ptr_array_index(editor->pipelines, i);
      if (!g_strcmp0(entry->id, id)) {
         gtk_combo_box_set_active(GTK_COMBO_BOX(editor->pipeline_combo), (gint)i);
         return;
      }
   }
}
#endif
