/* Original IRC protocol API, transport owned by the embedding application. */
#ifndef LIBRUSTYIRC_PROTOCOL_H
#define LIBRUSTYIRC_PROTOCOL_H
#include "irc.h"
#include "connection.h"
#include <librustyaxe/dict.h>
/* Internal wire handlers are distinct from application event listeners. */
typedef bool (*rustyirc_handler_t)(irc_conn_t *, irc_message_t *);
typedef struct rustyirc_callback {
   char *cmd;
   int numeric;
   char *event_key;
   rustyirc_handler_t cb;
   bool relayed;
   bool unidle;
   struct rustyirc_callback *next;
} rustyirc_callback_t;

//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
extern const irc_cap_t irc_capabilities[];
extern void rustyirc_capabilities_reset(irc_conn_t *);
extern void rustyirc_capabilities_clear(irc_conn_t *);
extern void rustyirc_capabilities_shutdown(void);
extern void rustyirc_capabilities_message(irc_conn_t *, const irc_message_t *);
extern bool rustyirc_supports_rustyrig(const irc_conn_t *);
extern char rustyirc_prefix_mode(const irc_conn_t *, char symbol);
extern const char *rustyirc_modes_symbol(const irc_conn_t *, const char *modes);
extern bool rustyirc_mode_has_argument(const irc_conn_t *, char mode, bool adding);

/* Borrowed ISUPPORT value until update/reset/disconnect; NULL means absent. */
extern const char *rustyirc_server_property(const irc_conn_t *, const char *name);
/* Includes CRLF, bounded by the transport's IRC_MSGLEN buffer. */
extern size_t rustyirc_line_limit(const irc_conn_t *);

/* CAP names are case-sensitive. Values are borrowed like ISUPPORT values. */
extern const char *rustyirc_server_capability(const irc_conn_t *, const char *name);
extern bool rustyirc_capability_enabled(const irc_conn_t *, const char *name);
extern const char *rustyirc_server_capab(const irc_conn_t *, const char *name);

extern void rustyirc_capabilities_poll(irc_conn_t *);

//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* RustyRig DCC AUDIO: v1 rig referrals, v2 peer microphone voice over RTP. */




/* Complete CTCP payload, including delimiters. Caller owns returned string/dict. Decode produces the same media.subscribed fields as WebSocket, plus
 * parent-session. */
char *rustyirc_dcc_audio_encode(const char *parent_session, dict *grant);
dict *rustyirc_dcc_audio_decode(const char *payload);
bool rustyirc_send_dcc_audio(irc_conn_t *client, const char *target, const char *parent_session, dict *grant);
/* Consume DCC AUDIO messages, emitting only valid private PRIVMSG/NOTICE offers. Invalid/public offers are consumed without exposing credentials in chat logs.
 */
bool rustyirc_dcc_audio_message(irc_conn_t *client, const irc_message_t *message);


bool rustyirc_process_message(irc_conn_t *, const char *);
bool rustyirc_dispatch_message(irc_conn_t *, irc_message_t *);
bool rustyirc_register_callback(rustyirc_callback_t *);
bool rustyirc_remove_callback(rustyirc_callback_t *);
bool rustyirc_register_default_callbacks(void);
bool rustyirc_register_default_numeric_callbacks(void);
void rustyirc_shutdown(void);
bool rustyirc_send(irc_conn_t *, const char *, ...) __attribute__((format(printf, 2, 3)));
void rustyirc_emit_message(const char *, irc_conn_t *, const irc_message_t *);
#endif
