// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rrclient/gtk/gtk.routing-pins.h>

int main(void) {
   rr_audio_graph_snapshot_t *g=g_new0(rr_audio_graph_snapshot_t,1);
   g->node_count=4;
   strcpy(g->nodes[0].id,"modem1.tx");
   strcpy(g->nodes[1].id,"rig.sink");
   strcpy(g->nodes[2].id,"src.client.dsp0");
   strcpy(g->nodes[3].id,"tx.mix");
   rr_ag_view_link_t audio={.src=0,.dst=1,.kind=RR_AG_NORMAL,.channels=RR_AG_CHANNEL_LEFT};
   rr_ag_view_link_t ptt={.src=0,.dst=1,.kind=RR_AG_CONTROL,.channels=RR_AG_CHANNEL_LEFT};
   rr_ag_view_link_t mic={.src=2,.dst=3,.kind=RR_AG_NORMAL,.channels=RR_AG_CHANNEL_STEREO};

   /* PTT is the lower physical pin (1), despite the graph's L mask. */
   assert(rr_routing_pin_matches_link(g,&ptt,0,FALSE,1));
   assert(rr_routing_pin_matches_link(g,&ptt,1,TRUE,1));
   assert(!rr_routing_pin_matches_link(g,&ptt,0,FALSE,0));
   assert(!rr_routing_pin_matches_link(g,&ptt,1,TRUE,0));
   assert(rr_routing_pin_matches_link(g,&audio,0,FALSE,0));
   assert(rr_routing_pin_matches_link(g,&audio,1,TRUE,0));
   assert(!rr_routing_pin_matches_link(g,&audio,0,FALSE,1));
   assert(!rr_routing_pin_matches_link(g,&audio,1,TRUE,1));
   assert(rr_routing_pin_matches_link(g,&mic,3,TRUE,0));
   assert(rr_routing_pin_matches_link(g,&mic,3,TRUE,1));
   assert(rr_routing_pin_matches_link(g,&mic,2,FALSE,0));
   assert(rr_routing_pin_matches_link(g,&mic,2,FALSE,1));
   assert(!rr_routing_pin_matches_link(g,&mic,2,TRUE,0));
   assert(!rr_routing_pin_matches_link(g,&mic,3,FALSE,0));
   assert(!rr_routing_pin_matches_link(g,&ptt,99,TRUE,1));
   assert(!rr_routing_pin_matches_link(g,&ptt,0,FALSE,-1));
   /* Oscillator I may connect to the *right* input of an LV2 effect.
    * The output and input hit tests must use their independent masks. */
   g->node_count=6;
   strcpy(g->nodes[4].id,"osc.1");
   strcpy(g->nodes[5].id,"proc.lv2.1");
   rr_ag_view_link_t crossed={
      .src=4,.dst=5,.kind=RR_AG_NORMAL,
      .channels=RR_AG_CHANNEL_LEFT,
      .src_channels=RR_AG_CHANNEL_LEFT,
      .dst_channels=RR_AG_CHANNEL_RIGHT
   };
   assert(rr_routing_pin_matches_link(g,&crossed,4,FALSE,0));
   assert(!rr_routing_pin_matches_link(g,&crossed,4,FALSE,1));
   assert(rr_routing_pin_matches_link(g,&crossed,5,TRUE,1));
   assert(!rr_routing_pin_matches_link(g,&crossed,5,TRUE,0));

   g_free(g);
   puts("PASS: modem and rig PTT pins remain distinct from audio; stereo mixer pins match");
   return 0;
}
