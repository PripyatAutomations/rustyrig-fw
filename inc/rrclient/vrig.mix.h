// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_VRIG_MIX_H
#define RRCLIENT_VRIG_MIX_H
#include <rrclient/audio.engine.h>
/* Explicit slot-local PCM inputs. No implicit media or PTT selection.
 * Lifecycle/patch calls run on the core thread. Feed may run on PCM producers. */
typedef void (*rr_vrig_mix_output_fn)(unsigned slot,const int16_t *pcm,size_t frames,void *user);
bool rr_vrig_mix_open(unsigned slot,rr_vrig_mix_output_fn output,void *user);
void rr_vrig_mix_close(unsigned slot);
void rr_vrig_mix_shutdown(void);
bool rr_vrig_mix_connect(unsigned slot,const char *source,const rr_pcm_stream_format_t *format);
void rr_vrig_mix_disconnect(unsigned slot,const char *source);
bool rr_vrig_mix_feed(unsigned slot,const char *source,const void *pcm,size_t frames);
bool rr_vrig_mix_gain(unsigned slot,const char *source,unsigned channel,double db);
#endif
