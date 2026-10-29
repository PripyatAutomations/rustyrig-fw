//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_DATABASE_H
#define RRCLIENT_DATABASE_H
#include <stdbool.h>
/* Opens and initializes the persistent client settings database. */
bool rrclient_database_init(void);
void rrclient_database_shutdown(void);
bool rrclient_database_load_profiles(void);
bool rrclient_database_config_save(const char *path);
char *rrclient_database_normalize_url(const char *url);
bool rrclient_database_server_set(const char *name, const char *property, const char *value);
bool rrclient_database_server_delete(const char *name);
bool rrclient_database_server_move(const char *name, const char *network);
bool rrclient_database_network_set(const char *name, const char *members);
#endif
