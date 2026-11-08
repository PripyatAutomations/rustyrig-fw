// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#include "pipeline.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static guint find_factory(rr_pipeline_graph_t *graph, const gchar *factory) {
   for (guint i = 0 ; i < rr_pipeline_graph_node_count(graph) ; i++) {
      if (!g_strcmp0(rr_pipeline_node_factory(rr_pipeline_graph_node(graph, i)), factory)) {
         return i;
      }
   }

   assert(false);

   return 0;
}

static void test_unchanged_round_trip(void) {
   const gchar *launch = "fakesrc num-buffers=3 name=src ! fakesink name=sink";
   GError *error = NULL;
   rr_pipeline_graph_t *graph = rr_pipeline_graph_parse(launch, &error);
   assert(graph && !error);
   assert(rr_pipeline_graph_node_count(graph) == 2);
   assert(rr_pipeline_graph_link_count(graph) == 1);
   gchar *serialized = rr_pipeline_graph_serialize(graph, &error);
   assert(serialized && !error && !strcmp(serialized, launch));
   g_free(serialized);
   rr_pipeline_graph_free(graph);
}

static void test_edit_and_round_trip(void) {
   GError *error = NULL;
   rr_pipeline_graph_t *graph = rr_pipeline_graph_parse("fakesrc num-buffers=3 name=src ! fakesink name=sink", &error);
   assert(graph && !error);
   guint source = find_factory(graph, "fakesrc");
   assert(rr_pipeline_graph_set_property(graph, source, "num-buffers", "7", &error));
   gchar *serialized = rr_pipeline_graph_serialize(graph, &error);
   assert(serialized && !error);
   assert(strstr(serialized, "num-buffers=7"));
   rr_pipeline_graph_t *round_trip = rr_pipeline_graph_parse(serialized, &error);
   assert(round_trip && !error);
   assert(rr_pipeline_graph_node_count(round_trip) == 2);
   assert(rr_pipeline_graph_link_count(round_trip) == 1);
   g_free(serialized);
   rr_pipeline_graph_free(round_trip);
   rr_pipeline_graph_free(graph);
}

static void test_existing_tee_branches(void) {
   const gchar *launch = "fakesrc num-buffers=3 name=src ! tee name=t " "t. ! queue name=q1 ! fakesink name=s1 " "t. ! queue name=q2 ! fakesink name=s2";
   GError *error = NULL;
   rr_pipeline_graph_t *graph = rr_pipeline_graph_parse(launch, &error);
   assert(graph && !error);
   assert(rr_pipeline_graph_node_count(graph) == 6);
   assert(rr_pipeline_graph_link_count(graph) == 5);
   guint tee = 0;

   for (guint i = 0 ; i < rr_pipeline_graph_node_count(graph) ; i++) {
      if (!g_strcmp0(rr_pipeline_node_factory(rr_pipeline_graph_node(graph, i)), "tee")) {
         tee = i;
      }
   }

   assert(tee);
   assert(rr_pipeline_graph_add_branch(graph, tee, "fakesink", &error));
   gchar *serialized = rr_pipeline_graph_serialize(graph, &error);
   assert(serialized && !error);
   rr_pipeline_graph_t *round_trip = rr_pipeline_graph_parse(serialized, &error);
   assert(round_trip && !error);
   assert(rr_pipeline_graph_node_count(round_trip) == 8);
   assert(rr_pipeline_graph_link_count(round_trip) == 7);
   g_free(serialized);
   rr_pipeline_graph_free(round_trip);
   rr_pipeline_graph_free(graph);
}

static void test_tee_insertion(void) {
   GError *error = NULL;
   rr_pipeline_graph_t *graph = rr_pipeline_graph_parse("fakesrc num-buffers=3 name=src ! fakesink name=sink", &error);
   assert(graph && !error);
   guint source = find_factory(graph, "fakesrc"), tee = 0;
   assert(rr_pipeline_graph_add_tee(graph, source, &tee, &error));
   assert(rr_pipeline_graph_add_branch(graph, tee, "fakesink", &error));
   gchar *serialized = rr_pipeline_graph_serialize(graph, &error);
   assert(serialized && !error);
   rr_pipeline_graph_t *round_trip = rr_pipeline_graph_parse(serialized, &error);
   assert(round_trip && !error);
   assert(rr_pipeline_graph_node_count(round_trip) == 6);
   assert(rr_pipeline_graph_link_count(round_trip) == 5);
   g_free(serialized);
   rr_pipeline_graph_free(round_trip);
   rr_pipeline_graph_free(graph);
}

static void test_new_pipeline_from_toolbox(void) {
   GError *error = NULL;
   rr_pipeline_graph_t *graph = rr_pipeline_graph_new();
   assert(graph && rr_pipeline_graph_node_count(graph) == 0);
   guint source = 0, sink = 0;
   assert(rr_pipeline_graph_add_element(graph, "fakesrc", 40, 40, &source, &error));
   assert(rr_pipeline_graph_add_element(graph, "fakesink", 240, 40, &sink, &error));
   assert(rr_pipeline_graph_add_link(graph, source, sink, &error));
   gchar *serialized = rr_pipeline_graph_serialize(graph, &error);
   assert(serialized && !error);
   rr_pipeline_graph_t *round_trip = rr_pipeline_graph_parse(serialized, &error);
   assert(round_trip && !error);
   assert(rr_pipeline_graph_node_count(round_trip) == 2);
   assert(rr_pipeline_graph_link_count(round_trip) == 1);
   g_free(serialized);
   rr_pipeline_graph_free(round_trip);
   rr_pipeline_graph_free(graph);
}

static void test_edited_string_property(void) {
   GError *error = NULL;
   rr_pipeline_graph_t *graph = rr_pipeline_graph_parse("fakesrc name=src ! filesink location=old.wav name=sink", &error);
   assert(graph && !error);
   guint sink = find_factory(graph, "filesink");
   assert(rr_pipeline_graph_set_property(graph, sink, "location", "new capture.wav", &error));
   gchar *serialized = rr_pipeline_graph_serialize(graph, &error);

   if (!serialized || error) {
      fprintf(stderr, "string property serialization failed: %s\n", error ? error->message : "unknown");
   }
   assert(serialized && !error);
   rr_pipeline_graph_t *round_trip = rr_pipeline_graph_parse(serialized, &error);
   assert(round_trip && !error);
   g_free(serialized);
   rr_pipeline_graph_free(round_trip);
   rr_pipeline_graph_free(graph);
}

static void test_caps_and_property_round_trip(void) {
   GError *error = NULL;
   rr_pipeline_graph_t *graph =
      rr_pipeline_graph_parse("fakesrc num-buffers=3 name=src ! capsfilter caps=\"audio/x-raw,rate=8000\" name=caps ! fakesink name=sink", &error);
   assert(graph && !error);
   assert(rr_pipeline_graph_set_property(graph, find_factory(graph, "fakesrc"), "num-buffers", "5", &error));
   gchar *serialized = rr_pipeline_graph_serialize(graph, &error);

   if (!serialized || error) {
      fprintf(stderr, "caps serialization failed: %s\n", error ? error->message : "unknown");
   }
   assert(serialized && !error);
   rr_pipeline_graph_t *round_trip = rr_pipeline_graph_parse(serialized, &error);
   assert(round_trip && !error);
   assert(rr_pipeline_graph_link_count(round_trip) == 2);
   g_free(serialized);
   rr_pipeline_graph_free(round_trip);
   rr_pipeline_graph_free(graph);
}

static void test_invalid_pipeline_and_property(void) {
   GError *error = NULL;
   rr_pipeline_graph_t *graph = rr_pipeline_graph_parse("rr-not-a-real-factory", &error);
   assert(!graph && error);
   g_clear_error(&error);
   graph = rr_pipeline_graph_parse("fakesrc name=src ! fakesink name=sink", &error);
   assert(graph && !error);
   assert(!rr_pipeline_graph_set_property(graph, find_factory(graph, "fakesrc"), "num-buffers", "bad", &error));
   assert(error);
   g_clear_error(&error);
   rr_pipeline_graph_free(graph);
}

int main(int argc, char **argv) {
   gst_init(&argc, &argv);
   test_unchanged_round_trip();
   test_edit_and_round_trip();
   test_existing_tee_branches();
   test_tee_insertion();
   test_new_pipeline_from_toolbox();
   test_edited_string_property();
   test_caps_and_property_round_trip();
   test_invalid_pipeline_and_property();
   puts("rrpipelines graph tests passed");

   return 0;
}
