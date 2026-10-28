#ifndef RRCLIENT_DATABASE_H
#define RRCLIENT_DATABASE_H
#include <stdbool.h>
/* Opens and initializes the persistent client settings database. */
bool rrclient_database_init(void);
void rrclient_database_shutdown(void);
bool rrclient_database_load_profiles(void);
bool rrclient_database_config_save(const char *path);
bool rrclient_database_server_set(const char *name, const char *property, const char *value);
bool rrclient_database_network_set(const char *name, const char *members);
#endif
