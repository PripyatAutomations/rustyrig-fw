// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// Recording waveform playback floating panel. GPL-2.0-only.
#ifndef RRCLIENT_GTK_RECORDING_PLAYER_H
#define RRCLIENT_GTK_RECORDING_PLAYER_H
void gtk_recording_player_init(void);
void gtk_recording_player_shutdown(void);
void gtk_recording_player_show(const char *path);
#endif
