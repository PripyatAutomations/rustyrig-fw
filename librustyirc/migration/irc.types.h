//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RR_IRC_TYPES_H
#define RR_IRC_TYPES_H

#include <librustyaxe/struct.h>

/* Internal wire handlers are distinct from application event listeners. */
typedef bool (*rr_irc_handler_t)(rrconn_t *, irc_message_t *);
typedef struct rr_irc_command {
   const char *name;
   const char *desc;
   const char *event_key;
   rr_irc_handler_t cb;
   bool relayed;
   bool unidle;
} rr_irc_command_t;
typedef struct rr_irc_numeric {
   int code;
   const char *name;
   const char *desc;
   const char *event_key;
   rr_irc_handler_t cb;
   bool unidle;
} rr_irc_numeric_t;
typedef struct rr_irc_callback {
   char *cmd;
   int numeric;
   char *event_key;
   rr_irc_handler_t cb;
   bool relayed;
   bool unidle;
   struct rr_irc_callback *next;
} rr_irc_callback_t;

#endif
