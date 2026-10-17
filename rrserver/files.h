// Server-owned exported filesystem namespace. Part of RustyRig; MIT/GPL.
#ifndef RRSERVER_FILES_H
#define RRSERVER_FILES_H
#include <stdbool.h>
bool rrserver_files_valid_name(const char *name);
bool rrserver_files_config_init(void);
void rrserver_files_register(void);
void rrserver_files_fini(void);
#endif
