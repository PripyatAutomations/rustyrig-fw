// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// Native GTK RX routing window; playback state belongs to rrclient/audio.c.
#ifndef RRCLIENT_GTK_AUDIO_ROUTING_H
#define RRCLIENT_GTK_AUDIO_ROUTING_H
void gtk_audio_routing_init(void);
void gtk_audio_routing_shutdown(void);
void gtk_audio_graph_present(void);
void gtk_audio_graph_hide(void);
void gtk_audio_graph_toggle(void);
void gtk_audio_graph_present_pipeline(const char *pipeline_id);
void gtk_volume_toggle(void);
/* Whitelisted script UI binding: persistent, redraws all wire canvases. */
double rr_gtk_routing_wire_sag_get(void);
void rr_gtk_routing_wire_sag_set(double sag);
#endif
