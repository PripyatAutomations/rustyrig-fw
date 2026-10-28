#ifndef RRCLIENT_DATABASE_H
#define RRCLIENT_DATABASE_H
#include <stdbool.h>
/* Opens and initializes the persistent client settings database. */
bool rrclient_database_init(void);
void rrclient_database_shutdown(void);
#endif
