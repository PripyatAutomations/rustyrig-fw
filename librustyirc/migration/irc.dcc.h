//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Classic IRC DCC CHAT/SEND. UI and destination-path policy belong to the app. */
#ifndef LIBRRPROTOCOL_IRC_DCC_H
#define LIBRRPROTOCOL_IRC_DCC_H
#include <librustyaxe/core.h>
#include <librrprotocol/irc.types.h>
#define IRC_DCC_MAX_SESSIONS 16
#define IRC_DCC_EVENT "irc.dcc.session"
#define IRC_DCC_CHAT_EVENT "irc.dcc.chat"
typedef enum {
   IRC_DCC_CHAT, IRC_DCC_SEND
} irc_dcc_type_t;
typedef enum {
   IRC_DCC_PENDING, IRC_DCC_LISTENING, IRC_DCC_CONNECTING, IRC_DCC_ACTIVE
} irc_dcc_state_t;
typedef struct {
   const char *bind, *host;
   unsigned port_min, port_max;
} irc_dcc_options_t;
typedef struct {
   rrconn_t *control;
   unsigned id;
   irc_dcc_type_t type;
   irc_dcc_state_t state;
   bool outgoing, size_known;
   char nick[NICKLEN + 1], filename[256], ip[64];
   unsigned port;
   uint64_t size, transferred, acknowledged;
} irc_dcc_session_t;
/* Strict CTCP offer parsing/encoding; payload includes the CTCP delimiters. IPv4 decimal and dotted addresses, quoted filenames, optional SEND size. Reverse
 * DCC, RESUME, and SDCC are not negotiated by this implementation. */
bool irc_dcc_decode(const char *payload, irc_dcc_session_t *offer);
char *irc_dcc_encode(const irc_dcc_session_t *offer);
bool irc_dcc_message(rrconn_t *client, const irc_message_t *message);
bool irc_dcc_advertised_ip(rrconn_t *client, const char *configured, char ip[64]);
/* File descriptors are duplicated on success and remain caller-owned. SEND requires a readable regular file; receiving requires an empty writable file. */
unsigned irc_dcc_offer_chat(rrconn_t *client, const char *nick, const irc_dcc_options_t *options);
unsigned irc_dcc_offer_file(rrconn_t *client, const char *nick, const char *filename, int fd, const irc_dcc_options_t *options);
bool irc_dcc_accept(rrconn_t *client, unsigned id, int destination_fd);
bool irc_dcc_chat_send(rrconn_t *client, unsigned id, const char *text);
bool irc_dcc_close(rrconn_t *client, unsigned id);
bool irc_dcc_session_get(unsigned index, irc_dcc_session_t *session);
void irc_dcc_init(void);
void irc_dcc_shutdown(void);
void irc_dcc_poll(void);
#endif
