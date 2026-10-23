//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* RustyRig DCC AUDIO: v1 rig referrals, v2 peer microphone voice over RTP. */
#ifndef LIBRRPROTOCOL_IRC_AUDIO_H
#define LIBRRPROTOCOL_IRC_AUDIO_H
#include <librustyaxe/core.h>
#include <rrclient/irc.types.h>
/* Complete CTCP payload, including delimiters. Caller owns returned string/dict. Decode produces the same media.subscribed fields as WebSocket, plus
 * parent-session. */
char *irc_dcc_audio_encode(const char *parent_session, dict *grant);
dict *irc_dcc_audio_decode(const char *payload);
bool irc_send_dcc_audio(rrconn_t *client, const char *target, const char *parent_session, dict *grant);
/* Consume DCC AUDIO messages, emitting only valid private PRIVMSG/NOTICE offers. Invalid/public offers are consumed without exposing credentials in chat logs.
 */
bool irc_dcc_audio_message(rrconn_t *client, const irc_message_t *message);
#endif
