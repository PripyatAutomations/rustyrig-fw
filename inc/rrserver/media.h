//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef __rrserver_media_h
#define __rrserver_media_h

#include <librrprotocol/rrprotocol.h>

extern void rrserver_media_record_ptt(rr_vfo_t vfo, bool ptt, rrconn_t *talker, const char *recording_id);
extern bool rrserver_media_activate_ptt(rr_vfo_t vfo, rrconn_t *talker);
extern void rrserver_media_recording_tick(void);
extern bool rrserver_media_audio_init(void);
struct rr_server_rig;
extern void rrserver_media_rx_pcm(struct rr_server_rig *radio, const void *samples, size_t len);
extern bool rrserver_media_rx_requested(struct rr_server_rig *radio);
extern bool rrserver_media_init(void);
struct rr_mediachan;
extern bool rrserver_media_uuid_assign(struct rr_mediachan *channel, const char *scope_uuid, const char *role);
extern bool rrserver_media_uuid_assign_node(struct rr_mediachan *channel, const char *role);
bool rrserver_media_add_rig(struct rr_server_rig *radio);
extern void rrserver_media_register_events(void);

#endif
