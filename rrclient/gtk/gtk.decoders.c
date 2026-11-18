// SPDX-License-Identifier: GPL-2.0-only
/* Modem management lives in Routing; no separate floating decoder window. */
#include "gtk.decoders.h"
#include "gtk.audio-routing.h"

void gtk_decoders_show(void) {
   gtk_audio_graph_present();
}

void gtk_decoders_shutdown(void) {
   /* The Routing page owns modem creation and configuration. */
}
