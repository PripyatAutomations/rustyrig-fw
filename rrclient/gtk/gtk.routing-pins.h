// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_GTK_ROUTING_PINS_H
#define RRCLIENT_GTK_ROUTING_PINS_H
#include <rrclient/audio.graph.h>
#include <string.h>

/* Physical audio connectors use pin 0=L/mono and pin 1=R.
 * On modem TX and rig.sink, pin 1 is instead the red PTT/control pin.
 * Keep UI pin numbers separate from graph link channel masks. */
static inline gboolean rr_routing_pin_matches_link(
   const rr_audio_graph_snapshot_t *snapshot,
   const rr_ag_view_link_t *link, guint node, gboolean incoming, gint pin)
{
   if (!snapshot || !link || node >= snapshot->node_count ||
       link->src >= snapshot->node_count ||
       link->dst >= snapshot->node_count || pin < 0 || pin > 2 ||
       (incoming ? link->dst : link->src) != node)
      return FALSE;
   if(link->kind==RR_AG_NUMBER) {
      if(incoming)return pin==link->control_pin &&
         g_str_has_prefix(snapshot->nodes[node].id,"osc.");
      return pin==0 && g_str_has_prefix(snapshot->nodes[node].id,"control.");
   }
   if(pin>1)return FALSE;

   const char *id = snapshot->nodes[node].id;
   gboolean ptt_pin = !strcmp(id, "rig.sink") ||
      (g_str_has_prefix(id, "modem") && g_str_has_suffix(id, ".tx"));

   if (link->kind == RR_AG_CONTROL)
      return ptt_pin && pin == 1;
   if (ptt_pin && pin != 0)
      return FALSE;
   unsigned mask = incoming ? link->dst_channels : link->src_channels;
   if (!mask) mask=link->channels; /* legacy/synthetic snapshots */
   return !!(mask & (pin ? RR_AG_CHANNEL_RIGHT : RR_AG_CHANNEL_LEFT));
}
#endif
