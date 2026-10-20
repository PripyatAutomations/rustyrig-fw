//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Common file requests and chunked transfers; frontend owns presentation.
#ifndef RRCLIENT_FILES_H
#define RRCLIENT_FILES_H
#include <librrprotocol/rrprotocol.h>
void rrclient_files_register(void);
/* Request IDs and completion dictionaries belong to the event producer. */
char *rrclient_files_request(rrconn_t *, const char *command, const char *path, const char *dest);
bool rrclient_files_transfer(rrconn_t *, const char *remote, const char *local, bool upload, bool overwrite);
bool cmd_files(int argc, char **args);
#endif
