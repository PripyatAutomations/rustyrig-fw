// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef RR_GTK_KIWI_PANEL_H
#define RR_GTK_KIWI_PANEL_H
#include <librustyaxe/core.h>
#include <rrclient/vfo.snapshot.h>
void gtk_local_sdr_controls_show(void);
void gtk_kiwi_panel_active_event(const rr_event_message_t *message, void *unused);
void gtk_kiwi_panel_status_event(const rr_event_message_t *message, void *unused);
void gtk_kiwi_panel_vfo_snapshot(const rrclient_vfo_snapshot_t *snapshot);
#endif
