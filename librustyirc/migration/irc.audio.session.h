//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef LIBRRPROTOCOL_IRC_AUDIO_SESSION_H
#define LIBRRPROTOCOL_IRC_AUDIO_SESSION_H
#include <librustyaxe/core.h>
#define IRC_AUDIO_MAX_SESSIONS 16
#define IRC_AUDIO_RX 1U
#define IRC_AUDIO_TX 2U
#define IRC_AUDIO_BOTH (IRC_AUDIO_RX | IRC_AUDIO_TX)
#define IRC_AUDIO_OFFER_EVENT "irc.dcc.audio.offer"
#define IRC_AUDIO_PCM_EVENT "irc.dcc.audio.pcm"
#define IRC_AUDIO_CLOSED_EVENT "irc.dcc.audio.closed"
typedef struct {
   const char *bind, *host;
   unsigned port_min, port_max;
   bool encrypt;
} irc_audio_options_t;
typedef struct {
   rrconn_t *control;
   char nick[NICKLEN + 1], endpoint[96];
   bool tx, outgoing, accepted, active;
} irc_audio_session_t;
/* Synchronous binary event payload; pointers valid only during the callback. PCM is mono S16LE, 16 kHz, at most 20 ms per packet. */
typedef struct {
   const char *attachment;
   const void *samples;
   size_t length;
} irc_audio_pcm_t;
/* Directions are always relative to the local client. No incoming offer opens a socket or permits microphone audio before the application calls accept. */
bool irc_audio_offer(rrconn_t *client, const char *nick, unsigned directions, const irc_audio_options_t *options);
bool irc_audio_accept(rrconn_t *client, const char *nick, unsigned directions);
unsigned irc_audio_close(rrconn_t *client, const char *nick, unsigned directions);
/* Enumerate pending and active sessions without exposing attachment keys. */
bool irc_audio_session_get(unsigned index, irc_audio_session_t *session);
void irc_audio_sessions_init(void);
void irc_audio_sessions_shutdown(void);
void irc_audio_sessions_poll(void);
/* NULL control broadcasts to accepted, authenticated local TX attachments. */
void irc_audio_send_pcm(rrconn_t *control, const void *pcm, size_t length);
#endif
