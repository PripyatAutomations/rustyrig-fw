// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
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
   assert(rr_audio_graph_add_channel_link(g, "sdr", "eq", RR_AG_NORMAL, RR_AG_CHANNEL_LEFT));
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
   rr_audio_graph_snapshot_t *view = g_new0(rr_audio_graph_snapshot_t, 1);
   assert(rr_audio_graph_set_route(g, "sdr", 'L'));
   assert(rr_audio_graph_snapshot(g, view));
   assert(view->node_count == 4 && view->link_count == 3);
   assert(!g_strcmp0(view->nodes[0].id, "sdr"));
   assert(view->nodes[0].route == 'L');
   assert(view->links[0].channels == RR_AG_CHANNEL_LEFT);
   assert(view->links[1].kind == RR_AG_BYPASS);
   assert(view->links[1].channels == RR_AG_CHANNEL_STEREO);
   g_free(view);
   rr_audio_graph_free(g);
   puts("audio graph bypass selection passed");

   return 0;
}
