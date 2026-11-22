// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#include <assert.h>
#include <stdio.h>
#include <rrclient/audio.graph.h>

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
   /* Direct modem audio is a logical TX patch alongside the single real
    * transport input, but this special case must not weaken ordinary sink
    * single-input checks or merge independent PTT/control links. */
   g=rr_audio_graph_new();
   assert(g);
   assert(rr_audio_graph_add_node(g,"tx.transport",RR_AG_PROCESSOR));
   assert(rr_audio_graph_add_node(g,"modem1.tx",RR_AG_SOURCE));
   assert(rr_audio_graph_add_node(g,"modem2.tx",RR_AG_SOURCE));
   assert(rr_audio_graph_add_node(g,"other",RR_AG_SOURCE));
   assert(rr_audio_graph_add_node(g,"rig.sink",RR_AG_SINK));
   assert(rr_audio_graph_add_node(g,"ordinary.sink",RR_AG_SINK));
   assert(rr_audio_graph_add_link(g,"tx.transport","rig.sink",RR_AG_NORMAL));
   assert(rr_audio_graph_add_channel_link(g,"modem1.tx","rig.sink",RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(rr_audio_graph_add_channel_link(g,"modem2.tx","rig.sink",RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(!rr_audio_graph_add_channel_link(g,"modem1.tx","rig.sink",RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(!rr_audio_graph_add_channel_link(g,"other","rig.sink",RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(rr_audio_graph_add_channel_link(g,"modem1.tx","rig.sink",RR_AG_CONTROL,RR_AG_CHANNEL_LEFT));
   assert(rr_audio_graph_add_link(g,"tx.transport","ordinary.sink",RR_AG_NORMAL));
   assert(!rr_audio_graph_add_channel_link(g,"modem1.tx","ordinary.sink",RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(rr_audio_graph_link_count(g)==5);
   rr_audio_graph_free(g);
   /* The same logical TX input graph must work whichever endpoint is
    * registered first; the transport is the only real encoder output. */
   g=rr_audio_graph_new();
   assert(g);
   assert(rr_audio_graph_add_node(g,"modem1.tx",RR_AG_SOURCE));
   assert(rr_audio_graph_add_node(g,"modem2.tx",RR_AG_SOURCE));
   assert(rr_audio_graph_add_node(g,"tx.transport",RR_AG_PROCESSOR));
   assert(rr_audio_graph_add_node(g,"rig.sink",RR_AG_SINK));
   assert(rr_audio_graph_add_channel_link(g,"modem1.tx","rig.sink",RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(rr_audio_graph_add_channel_link(g,"modem2.tx","rig.sink",RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(rr_audio_graph_add_channel_link(g,"tx.transport","rig.sink",RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(!rr_audio_graph_add_channel_link(g,"tx.transport","rig.sink",RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(rr_audio_graph_link_count(g)==3);
   rr_audio_graph_free(g);
   /* A single mono source can fan out to *both* independent mixer ears;
    * adding the second channel coalesces the same logical source edge. */
   g=rr_audio_graph_new();
   assert(g);
   assert(rr_audio_graph_add_node(g,"src.client.dsp0",RR_AG_SOURCE));
   assert(rr_audio_graph_add_node(g,"tx.mix",RR_AG_MIXER));
   assert(rr_audio_graph_add_channel_link(g,"src.client.dsp0","tx.mix",
           RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(rr_audio_graph_add_channel_link(g,"src.client.dsp0","tx.mix",
           RR_AG_NORMAL,RR_AG_CHANNEL_RIGHT));
   assert(rr_audio_graph_link_count(g)==1);
   rr_audio_graph_snapshot_t *fanout=g_new0(rr_audio_graph_snapshot_t,1);
   assert(rr_audio_graph_snapshot(g,fanout));
   assert(fanout->links[0].channels==RR_AG_CHANNEL_STEREO);
   g_free(fanout);
   rr_audio_graph_free(g);
   /* LV2 slot IDs are not an execution order. The graph must accept
    * an effect output feeding a lower-numbered slot, provided all nodes
    * have been registered before connecting their edges. */
   g=rr_audio_graph_new();
   assert(g);
   assert(rr_audio_graph_add_node(g,"proc.lv2.1",RR_AG_PROCESSOR));
   assert(rr_audio_graph_add_node(g,"proc.lv2.4",RR_AG_PROCESSOR));
   assert(rr_audio_graph_add_node(g,"rx.mix",RR_AG_MIXER));
   assert(rr_audio_graph_add_channel_link(g,"proc.lv2.4","proc.lv2.1",RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(rr_audio_graph_add_channel_link(g,"proc.lv2.1","rx.mix",RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(!rr_audio_graph_add_channel_link(g,"proc.lv2.1","proc.lv2.4",RR_AG_NORMAL,RR_AG_CHANNEL_LEFT));
   assert(rr_audio_graph_link_count(g)==2);
   rr_audio_graph_free(g);
   /* Rate-bridged listening routes: RX16 already feeds HQ playback
    * through rx.mix -> rx16.resample -> hq.mix. HQ-to-RX16 is unsupported
    * without an explicit downsampler and must not silently misclock PCM. */
   assert(!strcmp(rr_audio_graph_listen_bus(16000, "rx.mix"), "rx.mix"));
   assert(!strcmp(rr_audio_graph_listen_bus(16000, "hq.mix"), "rx.mix"));
   assert(!strcmp(rr_audio_graph_listen_bus(8000, "hq.mix"), "rx.mix"));
   assert(!strcmp(rr_audio_graph_listen_bus(44100, "hq.mix"), "hq.mix"));
   assert(!strcmp(rr_audio_graph_listen_bus(48000, "hq.mix"), "hq.mix"));
   assert(rr_audio_graph_listen_bus(44100, "rx.mix") == NULL);
   assert(rr_audio_graph_listen_bus(48000, "tx.mix") == NULL);
   assert(rr_audio_graph_listen_bus(16000, "tx.transport") == NULL);
   assert(rr_audio_graph_listen_bus(16000, NULL) == NULL);
   /* The graph shows the actual adapter path, never a phantom direct edge. */
   g=rr_audio_graph_new();
   assert(g);
   assert(rr_audio_graph_add_node(g,"test.rx16",RR_AG_SOURCE));
   assert(rr_audio_graph_add_node(g,"rx.mix",RR_AG_MIXER));
   assert(rr_audio_graph_add_node(g,"rx16.resample",RR_AG_PROCESSOR));
   assert(rr_audio_graph_add_node(g,"hq.mix",RR_AG_MIXER));
   assert(rr_audio_graph_add_channel_link(g,"test.rx16","rx.mix",
                                        RR_AG_NORMAL,RR_AG_CHANNEL_STEREO));
   assert(rr_audio_graph_add_link(g,"rx.mix","rx16.resample",RR_AG_NORMAL));
   assert(rr_audio_graph_add_link(g,"rx16.resample","hq.mix",RR_AG_NORMAL));
   assert(rr_audio_graph_link_count(g)==3);
   rr_audio_graph_free(g);
   puts("audio graph bypass and direct modem TX routing passed");

   return 0;
}
