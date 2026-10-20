//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Server-owned exported filesystem namespace. Part of RustyRig; MIT/GPL.
#ifndef RRSERVER_FILES_H
#define RRSERVER_FILES_H
#include <stdbool.h>
bool rrserver_files_valid_name(const char *name);
bool rrserver_files_config_init(void);
void rrserver_files_register(void);
void rrserver_files_fini(void);
#endif
