//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Original IRC wire parser API. Licensed under MIT, or GPL with mongoose. */
#ifndef LIBRUSTYIRC_IRC_H
#define LIBRUSTYIRC_IRC_H
#include <stddef.h>
#include <stdbool.h>
#define RUSTYIRC_MAX_PARAMS 15
/* Preserve the original IRCv3 receive ceiling, excluding CRLF. */
#define RUSTYIRC_MAX_LINE 1022
#define LOGINLEN 240            // an email address
#define IRC_MSGLEN 1024         // extended for IRCv3
#define CHANLEN 64              // channel name length
#define NICKLEN 40              // nick name length
#define TOPICLEN 256
#define PASSLEN 128
#define USERLEN 16
#define HOSTLEN 256
#define NETLEN 64
#define RECVQLEN 16384          // read, but unprocessed data from the
// server
#define SENDQLEN 16384          // data waiting to be sent to the server
#define AUTOJOIN_LEN 1024       // auto-join channels
#define USER_HASHSZ 127         // prime number


typedef struct {
   char mode;                   // mode letter, e.g. 'o', 'v', 'k'
   const char *desc;            // description
   bool arg_set;                // requires argument when being set
   bool arg_unset;              // requires argument when being unset
} irc_mode_t;



typedef struct irc_chan_user {
   char nick[NICKLEN + 1];
   bool is_op;                  // @
   bool is_voice;               // +
   bool is_halfop;              // % optional
   struct irc_chan_user *next;  // hash bucket chain
} irc_chan_user_t;

typedef struct irc_channel {
   char name[CHANLEN + 1];
   char mode[16];
   char topic[TOPICLEN + 1];
   int users;

   bool names_in_progress;
   bool userlist_complete;

   irc_chan_user_t *user_table[USER_HASHSZ]; // hash table of users
   struct irc_channel *next;    // list of channels
} irc_channel_t;


typedef struct {
   const char *name;
   const char *desc;
} irc_cap_t;

typedef struct irc_message {
   int argc;
   char **argv;
   char *prefix;
} irc_message_t;
typedef irc_message_t rustyirc_message_t;
/* Caller owns the message; argv[0] is the command and argv[argc] is NULL. */
irc_message_t *irc_parse_message(const char *line);
void irc_message_free(irc_message_t *message);
/* Compatibility names for the initial standalone API: one parser only. */
#define rustyirc_parse irc_parse_message
#define rustyirc_message_free irc_message_free
bool rustyirc_whoisuser(const rustyirc_message_t *message, const char **nick, const char **user, const char **host, const char **realname);
#endif
