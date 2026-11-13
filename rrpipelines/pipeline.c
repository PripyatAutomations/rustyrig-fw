//
// rrpipelines/pipeline.c: GStreamer pipeline graph model.
//    This is part of rustyrig-fw.
//
// SPDX-License-Identifier: GPL-2.0-only
#include "pipeline.h"
#include <string.h>

struct rr_pipeline_graph {
   GPtrArray *nodes;
   GPtrArray *links;
   gchar *original;
   gboolean modified;
};

GQuark rr_pipeline_error_quark(void) {
   return g_quark_from_static_string("rr-pipeline-error");
}

static void rr_pipeline_node_free(gpointer data) {
   rr_pipeline_node_t *node = data;

   if (!node) {
      return;
   }

   if (node->element) {
      gst_object_unref(node->element);
   }
   g_free(node);
}

static void rr_pipeline_link_free(gpointer data) {
   rr_pipeline_link_t *link = data;

   if (!link) {
      return;
   }
   g_free(link->src_pad);
   g_free(link->dst_pad);
   g_free(link);
}

static gboolean rr_pipeline_graph_add_node(rr_pipeline_graph_t *graph, GstElement *element, double x, double y, guint *index) {
   rr_pipeline_node_t *node = g_new0(rr_pipeline_node_t, 1);

   if (!node) {
      return FALSE;
   }
   node->element = gst_object_ref(element);
   node->x = x;
   node->y = y;

   if (index) {
      *index = graph->nodes->len;
   }
   g_ptr_array_add(graph->nodes, node);

   return TRUE;
}

static GstPad *rr_pipeline_pad_unwrap(GstPad *pad) {
   GstPad *resolved = gst_object_ref(pad);
   while (GST_IS_GHOST_PAD(resolved)) {
      GstPad *target = gst_ghost_pad_get_target(GST_GHOST_PAD(resolved));
      gst_object_unref(resolved);

      if (!target) {
         return NULL;
      }
      resolved = target;
   }
   return resolved;
}

static gchar *rr_pipeline_pad_spec(GstPad *pad) {
   GstPadTemplate *pad_template = gst_pad_get_pad_template(pad);

   if (pad_template && GST_PAD_TEMPLATE_PRESENCE(pad_template) == GST_PAD_REQUEST) {
      return g_strdup(GST_PAD_TEMPLATE_NAME_TEMPLATE(pad_template));
   }

   return g_strdup(GST_PAD_NAME(pad));
}

static gchar *rr_pipeline_choose_pad(GstElement *element, GstPadDirection direction) {
   GList *templates = gst_element_class_get_pad_template_list(GST_ELEMENT_GET_CLASS(element));

   for (GList *item = templates ; item ; item = item->next) {
      GstPadTemplate *pad_template = item->data;

      if (GST_PAD_TEMPLATE_DIRECTION(pad_template) != direction) {
         continue;
      }
      GstPadPresence presence = GST_PAD_TEMPLATE_PRESENCE(pad_template);

      if (presence == GST_PAD_REQUEST) {
         return g_strdup(GST_PAD_TEMPLATE_NAME_TEMPLATE(pad_template));
      }

      if (presence != GST_PAD_ALWAYS) {
         continue;
      }
      GstPad *pad = gst_element_get_static_pad(element, GST_PAD_TEMPLATE_NAME_TEMPLATE(pad_template));

      if (pad) {
         gchar *name = g_strdup(GST_PAD_NAME(pad));
         gst_object_unref(pad);

         return name;
      }
      gst_clear_object(&pad);
   }

   return NULL;
}

static gboolean rr_pipeline_collect_elements(GstBin *bin, rr_pipeline_graph_t *graph, GHashTable *indices, guint *order, GError **error) {
   GstIterator *iterator = gst_bin_iterate_elements(bin);
   GValue value = G_VALUE_INIT;
   gboolean ok = TRUE;
   GstIteratorResult result;

   while ((result = gst_iterator_next(iterator, &value)) == GST_ITERATOR_OK) {
      GstElement *element = g_value_get_object(&value);
      GstElementFactory *factory = gst_element_get_factory(element);
      gboolean synthetic_bin = GST_IS_BIN(element) && !factory;

      if (synthetic_bin) {
         ok = rr_pipeline_collect_elements(GST_BIN(element), graph, indices, order, error);
      } else {
         guint index = 0;

         if (!factory || !rr_pipeline_graph_add_node(graph, element, 0, 0, &index)) {
            g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_ELEMENT, "Cannot add GStreamer element %s to the editor graph", GST_ELEMENT_NAME(element));
            ok = FALSE;
         } else {
            g_hash_table_insert(indices, element, GUINT_TO_POINTER(index + 1));
            rr_pipeline_node_t *node = g_ptr_array_index(graph->nodes, index);
            node->x = 40 + (*order % 4) * 230;
            node->y = 40 + (*order / 4) * 145;
            (*order)++;
         }
      }
      g_value_reset(&value);

      if (!ok) {
         break;
      }
   }

   if (G_VALUE_TYPE(&value)) {
      g_value_unset(&value);
   }
   gst_iterator_free(iterator);

   if (ok && result == GST_ITERATOR_ERROR) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_ELEMENT, "Cannot enumerate elements in a GStreamer pipeline");
      ok = FALSE;
   }

   return ok;
}

static gboolean rr_pipeline_add_imported_links(rr_pipeline_graph_t *graph, GHashTable *indices, GError **error) {
   for (guint i = 0 ; i < graph->nodes->len ; i++) {
      rr_pipeline_node_t *node = g_ptr_array_index(graph->nodes, i);
      GstIterator *iterator = gst_element_iterate_pads(node->element);
      GValue value = G_VALUE_INIT;
      GstIteratorResult result;

      while ((result = gst_iterator_next(iterator, &value)) == GST_ITERATOR_OK) {
         GstPad *src_pad = g_value_get_object(&value);

         if (GST_PAD_DIRECTION(src_pad) == GST_PAD_SRC) {
            GstPad *peer = gst_pad_get_peer(src_pad);

            if (peer) {
               GstPad *resolved_src = rr_pipeline_pad_unwrap(src_pad);
               GstPad *resolved_dst = rr_pipeline_pad_unwrap(peer);
               GstElement *src_element = resolved_src ? gst_pad_get_parent_element(resolved_src) : NULL;
               GstElement *dst_element = resolved_dst ? gst_pad_get_parent_element(resolved_dst) : NULL;
               gpointer src_index = src_element ? g_hash_table_lookup(indices, src_element) : NULL;
               gpointer dst_index = dst_element ? g_hash_table_lookup(indices, dst_element) : NULL;

               if (src_index && dst_index) {
                  rr_pipeline_link_t *link = g_new0(rr_pipeline_link_t, 1);

                  if (!link) {
                     g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_LINK, "Out of memory while reading pipeline links");
                     gst_clear_object(&src_element);
                     gst_clear_object(&dst_element);
                     gst_clear_object(&resolved_src);
                     gst_clear_object(&resolved_dst);
                     gst_clear_object(&peer);
                     g_value_reset(&value);
                     gst_iterator_free(iterator);

                     if (G_VALUE_TYPE(&value)) {
                        g_value_unset(&value);
                     }

                     return FALSE;
                  }
                  link->src = GPOINTER_TO_UINT(src_index) - 1;
                  link->dst = GPOINTER_TO_UINT(dst_index) - 1;
                  link->src_pad = rr_pipeline_pad_spec(resolved_src);
                  link->dst_pad = rr_pipeline_pad_spec(resolved_dst);
                  g_ptr_array_add(graph->links, link);
               }
               gst_clear_object(&src_element);
               gst_clear_object(&dst_element);
               gst_clear_object(&resolved_src);
               gst_clear_object(&resolved_dst);
               gst_clear_object(&peer);
            }
         }
         g_value_reset(&value);
      }

      if (G_VALUE_TYPE(&value)) {
         g_value_unset(&value);
      }
      gst_iterator_free(iterator);

      if (result == GST_ITERATOR_ERROR) {
         g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_LINK, "Cannot enumerate GStreamer pads");

         return FALSE;
      }
   }

   return TRUE;
}

rr_pipeline_graph_t *rr_pipeline_graph_parse(const gchar *launch, GError **error) {
   if (!launch) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_PARSE, "Pipeline text is missing");

      return NULL;
   }

   if (!*launch) {
      return rr_pipeline_graph_new();
   }
   GError *parse_error = NULL;
   GstElement *root = gst_parse_launch_full(launch, NULL, GST_PARSE_FLAG_FATAL_ERRORS, &parse_error);

   if (!root || parse_error) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_PARSE, "Cannot parse GStreamer pipeline: %s", parse_error ? parse_error->message :
         "no pipeline was created");
      g_clear_error(&parse_error);
      gst_clear_object(&root);

      return NULL;
   }
   rr_pipeline_graph_t *graph = g_new0(rr_pipeline_graph_t, 1);

   if (!graph) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_ELEMENT, "Out of memory while creating pipeline graph");
      gst_object_unref(root);

      return NULL;
   }
   graph->nodes = g_ptr_array_new_with_free_func(rr_pipeline_node_free);
   graph->links = g_ptr_array_new_with_free_func(rr_pipeline_link_free);
   graph->original = g_strdup(launch);
   GHashTable *indices = g_hash_table_new(g_direct_hash, g_direct_equal);
   guint order = 0;
   gboolean ok = graph->nodes && graph->links && graph->original && indices;

   if (!ok) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_ELEMENT, "Out of memory while creating pipeline graph");
   } else if (GST_IS_BIN(root)) {
      ok = rr_pipeline_collect_elements(GST_BIN(root), graph, indices, &order, error);
   } else {
      ok = rr_pipeline_graph_add_node(graph, root, 40, 40, NULL);

      if (ok) {
         g_hash_table_insert(indices, root, GUINT_TO_POINTER(1));
      }
   }

   if (ok) {
      ok = rr_pipeline_add_imported_links(graph, indices, error);
   }
   g_hash_table_unref(indices);
   gst_object_unref(root);

   if (!ok) {
      rr_pipeline_graph_free(graph);

      return NULL;
   }
   rr_pipeline_graph_auto_layout(graph);
   return graph;
}

/* Layered, top-down layout. Order comes from actual pad links rather
 * than GST bin iterator order. A bounded relaxation handles branched DAGs;
 * cyclic/unconnected components get their own column and cannot loop. */
void rr_pipeline_graph_auto_layout(rr_pipeline_graph_t *graph) {
   if (!graph || !graph->nodes || !graph->nodes->len) return;
   guint n = graph->nodes->len;
   guint *depth = g_new0(guint, n);
   guint *incoming = g_new0(guint, n);
   gboolean *visited = g_new0(gboolean, n);
   guint *queue = g_new(guint, n);
   guint head = 0, tail = 0;
   for (guint i = 0; i < graph->links->len; i++) {
      const rr_pipeline_link_t *link = g_ptr_array_index(graph->links, i);
      if (link->src < n && link->dst < n && link->src != link->dst)
         incoming[link->dst]++;
   }
   for (guint i = 0; i < n; i++)
      if (!incoming[i]) { queue[tail++] = i; visited[i] = TRUE; }
   guint max_depth = 0;
   while (head < tail) {
      guint src = queue[head++];
      for (guint i = 0; i < graph->links->len; i++) {
         const rr_pipeline_link_t *link = g_ptr_array_index(graph->links, i);
         if (link->src != src || link->dst >= n || link->dst == src) continue;
         depth[link->dst] = MAX(depth[link->dst], depth[src] + 1);
         max_depth = MAX(max_depth, depth[link->dst]);
         if (incoming[link->dst] && !--incoming[link->dst] && !visited[link->dst]) {
            visited[link->dst] = TRUE;
            queue[tail++] = link->dst;
         }
      }
   }
   /* A loop cannot be topologically sorted: put leftover nodes in
    * one visually separate lower layer rather than producing bad depths. */
   for (guint i = 0; i < n; i++)
      if (!visited[i]) depth[i] = max_depth + 1;
   guint *columns = g_new0(guint, max_depth + 2);
   for (guint i = 0; i < n; i++) {
      rr_pipeline_node_t *node = g_ptr_array_index(graph->nodes, i);
      node->x = 40. + columns[depth[i]]++ * 220.;
      node->y = 40. + depth[i] * 170.;
   }
   g_free(columns);
   g_free(queue);
   g_free(visited);
   g_free(incoming);
   g_free(depth);
}

rr_pipeline_graph_t *rr_pipeline_graph_new(void) {
   rr_pipeline_graph_t *graph = g_new0(rr_pipeline_graph_t, 1);

   if (!graph) {
      return NULL;
   }
   graph->nodes = g_ptr_array_new_with_free_func(rr_pipeline_node_free);
   graph->links = g_ptr_array_new_with_free_func(rr_pipeline_link_free);
   graph->original = g_strdup("");

   if (!graph->nodes || !graph->links || !graph->original) {
      rr_pipeline_graph_free(graph);

      return NULL;
   }

   return graph;
}

void rr_pipeline_graph_free(rr_pipeline_graph_t *graph) {
   if (!graph) {
      return;
   }

   if (graph->nodes) {
      g_ptr_array_unref(graph->nodes);
   }

   if (graph->links) {
      g_ptr_array_unref(graph->links);
   }
   g_free(graph->original);
   g_free(graph);
}

guint rr_pipeline_graph_node_count(const rr_pipeline_graph_t *graph) {
   return graph ? graph->nodes->len : 0;
}

guint rr_pipeline_graph_link_count(const rr_pipeline_graph_t *graph) {
   return graph ? graph->links->len : 0;
}

const rr_pipeline_node_t *rr_pipeline_graph_node(const rr_pipeline_graph_t *graph, guint index) {
   return graph && index < graph->nodes->len ? g_ptr_array_index(graph->nodes, index) : NULL;
}

rr_pipeline_node_t *rr_pipeline_graph_node_edit(rr_pipeline_graph_t *graph, guint index) {
   return graph && index < graph->nodes->len ? g_ptr_array_index(graph->nodes, index) : NULL;
}

const rr_pipeline_link_t *rr_pipeline_graph_link(const rr_pipeline_graph_t *graph, guint index) {
   return graph && index < graph->links->len ? g_ptr_array_index(graph->links, index) : NULL;
}

const gchar *rr_pipeline_node_factory(const rr_pipeline_node_t *node) {
   GstElementFactory *factory = node ? gst_element_get_factory(node->element) : NULL;

   return factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : NULL;
}

const gchar *rr_pipeline_node_name(const rr_pipeline_node_t *node) {
   return node ? GST_ELEMENT_NAME(node->element) : NULL;
}

void rr_pipeline_node_set_position(rr_pipeline_node_t *node, double x, double y) {
   if (node) {
      node->x = x;
      node->y = y;
   }
}

gboolean rr_pipeline_graph_add_element(rr_pipeline_graph_t *graph, const gchar *factory_name, double x, double y, guint *index, GError **error) {
   GstElementFactory *factory = factory_name ? gst_element_factory_find(factory_name) : NULL;

   if (!graph || !factory) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_ELEMENT, "GStreamer element factory '%s' is unavailable", factory_name ? factory_name : "");
      gst_clear_object(&factory);

      return FALSE;
   }
   guint suffix = graph->nodes->len;
   gboolean used;
   gchar *name = NULL;
   do {
      used = FALSE;
      g_free(name);
      name = g_strdup_printf("rrp%u", suffix++);

      for (guint i = 0 ; i < graph->nodes->len ; i++) {
         rr_pipeline_node_t *node = g_ptr_array_index(graph->nodes, i);

         if (!g_strcmp0(GST_ELEMENT_NAME(node->element), name)) {
            used = TRUE;
            break;
         }
      }
   } while (used);
   GstElement *element = gst_element_factory_create(factory, name);
   gst_object_unref(factory);
   g_free(name);

   if (!element || !rr_pipeline_graph_add_node(graph, element, x, y, index)) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_ELEMENT, "Cannot create GStreamer element '%s'", factory_name);
      gst_clear_object(&element);

      return FALSE;
   }
   gst_object_unref(element);
   graph->modified = TRUE;

   return TRUE;
}

gboolean rr_pipeline_graph_add_link(rr_pipeline_graph_t *graph, guint src, guint dst, GError **error) {
   if (!graph || src >= graph->nodes->len || dst >= graph->nodes->len || src == dst) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_LINK, "Select two different pipeline elements to connect");

      return FALSE;
   }

   for (guint i = 0 ; i < graph->links->len ; i++) {
      rr_pipeline_link_t *link = g_ptr_array_index(graph->links, i);

      if (link->src == src && link->dst == dst) {
         g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_LINK, "Those elements are already connected");

         return FALSE;
      }
   }

   rr_pipeline_link_t *link = g_new0(rr_pipeline_link_t, 1);

   if (!link) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_LINK, "Out of memory while adding pipeline link");

      return FALSE;
   }
   link->src = src;
   link->dst = dst;
   rr_pipeline_node_t *src_node = g_ptr_array_index(graph->nodes, src);
   rr_pipeline_node_t *dst_node = g_ptr_array_index(graph->nodes, dst);
   link->src_pad = rr_pipeline_choose_pad(src_node->element, GST_PAD_SRC);
   link->dst_pad = rr_pipeline_choose_pad(dst_node->element, GST_PAD_SINK);

   if (!link->src_pad || !link->dst_pad) {
      rr_pipeline_link_free(link);
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_LINK, "Cannot find an available source/sink pad on %s and %s", GST_ELEMENT_NAME(src_node->element)
         , GST_ELEMENT_NAME(dst_node->element));

      return FALSE;
   }
   g_ptr_array_add(graph->links, link);
   graph->modified = TRUE;

   return TRUE;
}

gboolean rr_pipeline_graph_remove_node(rr_pipeline_graph_t *graph, guint index, GError **error) {
   if (!graph || index >= graph->nodes->len) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_ELEMENT, "Select a pipeline element to remove");

      return FALSE;
   }

   for (guint i = graph->links->len ; i > 0 ; i--) {
      rr_pipeline_link_t *link = g_ptr_array_index(graph->links, i - 1);

      if (link->src == index || link->dst == index) {
         g_ptr_array_remove_index(graph->links, i - 1);
      } else {
         if (link->src > index) {
            link->src--;
         }

         if (link->dst > index) {
            link->dst--;
         }
      }
   }

   g_ptr_array_remove_index(graph->nodes, index);
   graph->modified = TRUE;

   return TRUE;
}

gboolean rr_pipeline_graph_set_property(rr_pipeline_graph_t *graph, guint index, const gchar *property, const gchar *text, GError **error) {
   if (!graph || index >= graph->nodes->len || !property || !text) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_PROPERTY, "Invalid pipeline property selection");

      return FALSE;
   }
   GstElement *element = ((rr_pipeline_node_t *) g_ptr_array_index(graph->nodes, index))->element;
   GParamSpec *spec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), property);

   if (!spec || !(spec->flags & G_PARAM_WRITABLE) || (spec->flags & G_PARAM_CONSTRUCT_ONLY)) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_PROPERTY, "Property '%s' cannot be edited on %s", property, GST_ELEMENT_NAME(element));

      return FALSE;
   }
   GValue value = G_VALUE_INIT;
   g_value_init(&value, G_PARAM_SPEC_VALUE_TYPE(spec));

   if (!gst_value_deserialize(&value, text)) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_PROPERTY, "Invalid value for property '%s' on %s", property, GST_ELEMENT_NAME(element));
      g_value_unset(&value);

      return FALSE;
   }
   g_object_set_property(G_OBJECT(element), property, &value);
   g_value_unset(&value);
   graph->modified = TRUE;

   return TRUE;
}

static gboolean rr_pipeline_graph_make_link(rr_pipeline_graph_t *graph, guint src, guint dst, const gchar *src_pad, const gchar *dst_pad, GError **error) {
   if (!rr_pipeline_graph_add_link(graph, src, dst, error)) {
      return FALSE;
   }
   rr_pipeline_link_t *link = g_ptr_array_index(graph->links, graph->links->len - 1);

   if (src_pad) {
      g_free(link->src_pad);
      link->src_pad = g_strdup(src_pad);
   }

   if (dst_pad) {
      g_free(link->dst_pad);
      link->dst_pad = g_strdup(dst_pad);
   }

   return TRUE;
}

gboolean rr_pipeline_graph_add_tee(rr_pipeline_graph_t *graph, guint source, guint *tee_index, GError **error) {
   if (!graph || source >= graph->nodes->len) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_LINK, "Select a connected element before adding a tee");

      return FALSE;
   }
   GArray *outgoing = g_array_new(FALSE, FALSE, sizeof(rr_pipeline_link_t));

   for (guint i = 0 ; i < graph->links->len ; i++) {
      rr_pipeline_link_t *link = g_ptr_array_index(graph->links, i);

      if (link->src == source) {
         rr_pipeline_link_t copy = *link;
         copy.src_pad = g_strdup(link->src_pad);
         copy.dst_pad = g_strdup(link->dst_pad);
         g_array_append_val(outgoing, copy);
      }
   }

   if (!outgoing->len) {
      g_array_unref(outgoing);
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_LINK, "A tee can be inserted only before an existing downstream element");

      return FALSE;
   }

   for (guint i = graph->links->len ; i > 0 ; i--) {
      rr_pipeline_link_t *link = g_ptr_array_index(graph->links, i - 1);

      if (link->src == source) {
         g_ptr_array_remove_index(graph->links, i - 1);
      }
   }

   guint tee = 0;
   gboolean ok = rr_pipeline_graph_add_element(graph, "tee", 0, 0, &tee, error);

   if (ok) {
      rr_pipeline_node_t *node = g_ptr_array_index(graph->nodes, tee);
      rr_pipeline_node_t *upstream = g_ptr_array_index(graph->nodes, source);
      node->x = upstream->x + 190;
      node->y = upstream->y;
      ok = rr_pipeline_graph_add_link(graph, source, tee, error);
   }

   for (guint i = 0 ; ok && i < outgoing->len ; i++) {
      rr_pipeline_link_t *old = &g_array_index(outgoing, rr_pipeline_link_t, i);
      guint queue = 0;
      rr_pipeline_node_t *tee_node = g_ptr_array_index(graph->nodes, tee);
      ok = rr_pipeline_graph_add_element(graph, "queue", tee_node->x + 190, tee_node->y + i * 120, &queue, error)
         && rr_pipeline_graph_make_link(graph, tee, queue, NULL, NULL, error)
         && rr_pipeline_graph_make_link(graph, queue, old->dst, NULL, old->dst_pad, error);
   }

   for (guint i = 0 ; i < outgoing->len ; i++) {
      rr_pipeline_link_t *old = &g_array_index(outgoing, rr_pipeline_link_t, i);
      g_free(old->src_pad);
      g_free(old->dst_pad);
   }

   g_array_unref(outgoing);

   if (ok && tee_index) {
      *tee_index = tee;
   }

   return ok;
}

gboolean rr_pipeline_graph_add_branch(rr_pipeline_graph_t *graph, guint tee, const gchar *sink_factory, GError **error) {
   if (!graph || tee >= graph->nodes->len || g_strcmp0(rr_pipeline_node_factory(g_ptr_array_index(graph->nodes, tee)), "tee")) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_LINK, "Select a tee element before adding a branch");

      return FALSE;
   }
   GstElementFactory *factory = sink_factory ? gst_element_factory_find(sink_factory) : NULL;

   if (!factory || !gst_element_factory_list_is_type(factory, GST_ELEMENT_FACTORY_TYPE_SINK)) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_ELEMENT, "'%s' is not an available GStreamer sink", sink_factory ? sink_factory : "");
      gst_clear_object(&factory);

      return FALSE;
   }
   gst_object_unref(factory);
   rr_pipeline_node_t *tee_node = g_ptr_array_index(graph->nodes, tee);
   guint queue = 0, sink = 0;
   gboolean ok = rr_pipeline_graph_add_element(graph, "queue", tee_node->x + 190, tee_node->y + 120, &queue, error)
      && rr_pipeline_graph_add_element(graph, sink_factory, tee_node->x + 380, tee_node->y + 120, &sink, error)
      && rr_pipeline_graph_add_link(graph, tee, queue, error)
      && rr_pipeline_graph_add_link(graph, queue, sink, error);

   return ok;
}

static gboolean rr_pipeline_append_element(GString *description, const rr_pipeline_node_t *node, GError **error) {
   GstElementFactory *factory = gst_element_get_factory(node->element);

   if (!factory) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_SERIALIZE, "Element %s has no GStreamer factory", GST_ELEMENT_NAME(node->element));

      return FALSE;
   }
   g_string_append_printf(description, "%s name=%s", gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)), GST_ELEMENT_NAME(node->element));
   guint count = 0;
   GParamSpec **properties = g_object_class_list_properties(G_OBJECT_GET_CLASS(node->element), &count);

   for (guint i = 0 ; i < count ; i++) {
      GParamSpec *spec = properties[i];

      if (!(spec->flags & G_PARAM_WRITABLE) || !strcmp(spec->name, "name")) {
         continue;
      }
      GValue value = G_VALUE_INIT;
      g_value_init(&value, G_PARAM_SPEC_VALUE_TYPE(spec));
      g_object_get_property(G_OBJECT(node->element), spec->name, &value);

      if (!g_param_value_defaults(spec, &value)) {
         gchar *serialized = gst_value_serialize(&value);

         if (!serialized) {
            g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_SERIALIZE, "Cannot serialize non-default property '%s' on %s", spec->name, GST_ELEMENT_NAME(
               node->element));
            g_value_unset(&value);
            g_free(properties);

            return FALSE;
         }
         g_string_append_printf(description, " %s=%s", spec->name, serialized);
         g_free(serialized);
      }
      g_value_unset(&value);
   }

   g_free(properties);

   return TRUE;
}

gchar *rr_pipeline_graph_serialize(const rr_pipeline_graph_t *graph, GError **error) {
   if (!graph) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_SERIALIZE, "No pipeline graph to serialize");

      return NULL;
   }

   if (!graph->nodes->len) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_SERIALIZE, "Add at least one GStreamer element before applying");

      return NULL;
   }

   if (!graph->modified) {
      return g_strdup(graph->original);
   }
   GString *description = g_string_new(NULL);

   for (guint i = 0 ; i < graph->nodes->len ; i++) {
      if (i) {
         g_string_append_c(description, ' ');
      }

      if (!rr_pipeline_append_element(description, g_ptr_array_index(graph->nodes, i), error)) {
         g_string_free(description, TRUE);

         return NULL;
      }
   }

   for (guint i = 0 ; i < graph->links->len ; i++) {
      const rr_pipeline_link_t *link = g_ptr_array_index(graph->links, i);
      const rr_pipeline_node_t *src = g_ptr_array_index(graph->nodes, link->src);
      const rr_pipeline_node_t *dst = g_ptr_array_index(graph->nodes, link->dst);
      g_string_append_printf(description, " %s.%s ! %s.%s", GST_ELEMENT_NAME(src->element), link->src_pad, GST_ELEMENT_NAME(dst->element), link->dst_pad);
   }

   gchar *result = g_string_free(description, FALSE);
   GError *parse_error = NULL;
   GstElement *parsed = gst_parse_launch_full(result, NULL, GST_PARSE_FLAG_FATAL_ERRORS, &parse_error);

   if (!parsed || parse_error) {
      g_set_error(error, RR_PIPELINE_ERROR, RR_PIPELINE_ERROR_SERIALIZE, "Edited pipeline is not valid GStreamer launch syntax: %s", parse_error ? parse_error->
         message : "no pipeline was created");
      g_clear_error(&parse_error);
      gst_clear_object(&parsed);
      g_free(result);

      return NULL;
   }
   gst_object_unref(parsed);

   return result;
}
