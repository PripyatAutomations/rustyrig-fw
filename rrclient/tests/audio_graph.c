#include <assert.h>
#include <stdio.h>
#include "../audio.graph.h"

int main(void) {
   rr_audio_graph_t *g = rr_audio_graph_new();
   assert(g);
   assert(rr_audio_graph_add_node(g, "sdr", RR_AG_SOURCE));
   assert(rr_audio_graph_add_node(g, "fallback", RR_AG_SOURCE));
   assert(rr_audio_graph_add_node(g, "eq", RR_AG_PROCESSOR));
   assert(rr_audio_graph_add_node(g, "sink", RR_AG_SINK));
   assert(!rr_audio_graph_add_node(g, "eq", RR_AG_PROCESSOR));
   assert(rr_audio_graph_add_link(g, "sdr", "eq", RR_AG_NORMAL));
   assert(rr_audio_graph_add_link(g, "fallback", "eq", RR_AG_BYPASS));
   assert(rr_audio_graph_add_link(g, "eq", "sink", RR_AG_NORMAL));
   assert(!rr_audio_graph_add_link(g, "fallback", "eq", RR_AG_NORMAL));
   assert(!rr_audio_graph_add_link(g, "eq", "sdr", RR_AG_NORMAL));
   assert(!rr_audio_graph_add_link(g, "eq", "sink", RR_AG_NORMAL));
   assert(rr_audio_graph_node_count(g) == 4);
   assert(rr_audio_graph_link_count(g) == 3);
   assert(rr_audio_graph_set_health(g, "sdr", RR_AG_HEALTHY));
   assert(rr_audio_graph_set_health(g, "fallback", RR_AG_HEALTHY));
   assert(!g_strcmp0(rr_audio_graph_input(g, "eq"), "sdr"));
   assert(rr_audio_graph_set_health(g, "sdr", RR_AG_FAILED));
   assert(!g_strcmp0(rr_audio_graph_input(g, "eq"), "fallback"));
   assert(rr_audio_graph_set_health(g, "fallback", RR_AG_FAILED));
   assert(rr_audio_graph_input(g, "eq") == NULL);
   assert(rr_audio_graph_set_health(g, "sdr", RR_AG_HEALTHY));
   assert(!g_strcmp0(rr_audio_graph_input(g, "eq"), "sdr"));
   rr_audio_graph_free(g);
   puts("audio graph bypass selection passed");
   return 0;
}
