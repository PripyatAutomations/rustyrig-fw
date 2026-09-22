#ifndef __rrserver_media_h
#define __rrserver_media_h

#include <librrprotocol/rrprotocol.h>

extern void rrserver_media_record_ptt(rr_vfo_t vfo, bool ptt, rrconn_t *talker,
   const char *recording_id);
extern bool rrserver_media_activate_ptt(rr_vfo_t vfo, rrconn_t *talker);
extern void rrserver_media_recording_tick(void);
extern bool rrserver_media_audio_init(void);

#endif
