//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Original DCC CHAT/SEND wire format. Sessions and sockets belong to the app. */
#ifndef LIBRUSTYIRC_DCC_H
#define LIBRUSTYIRC_DCC_H
#include <stdbool.h>
#include <stdint.h>
typedef enum {
   IRC_DCC_CHAT, IRC_DCC_SEND
} irc_dcc_type_t;
typedef struct {
   irc_dcc_type_t type;
   bool size_known;
   char filename[256], ip[64];
   unsigned port;
   uint64_t size;
} rustyirc_dcc_offer_t;
bool rustyirc_dcc_filename_valid(const char *);
bool rustyirc_dcc_decode(const char *, rustyirc_dcc_offer_t *);
char *rustyirc_dcc_encode(const rustyirc_dcc_offer_t *);
#endif
