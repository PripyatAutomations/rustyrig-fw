#ifndef __rrserver_media_h
#define __rrserver_media_h

#include <librrprotocol/rrprotocol.h>

extern void rrserver_media_record_ptt(rr_vfo_t vfo, bool ptt, rrconn_t *talker);
extern void rrserver_media_recording_tick(void);

#endif
