// Common file requests and chunked transfers; frontend owns presentation.
#ifndef RRCLIENT_FILES_H
#define RRCLIENT_FILES_H
#include <librrprotocol/rrprotocol.h>
void rrclient_files_register(void);
/* Request IDs and completion dictionaries belong to the event producer. */
char *rrclient_files_request(rrconn_t *,const char *command,const char *path,const char *dest);
bool rrclient_files_transfer(rrconn_t *,const char *remote,const char *local,bool upload,bool overwrite);
bool cmd_files(int argc,char **args);
#endif
