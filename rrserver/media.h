//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef __rrserver_media_h
#define __rrserver_media_h

#include <librrprotocol/rrprotocol.h>

extern void rrserver_media_record_ptt(rr_vfo_t vfo, bool ptt, rrconn_t *talker,
   const char *recording_id);
extern bool rrserver_media_activate_ptt(rr_vfo_t vfo, rrconn_t *talker);
extern void rrserver_media_recording_tick(void);
extern bool rrserver_media_audio_init(void);
extern void rrserver_media_init(void);
extern void rrserver_media_register_events(void);

#endif
