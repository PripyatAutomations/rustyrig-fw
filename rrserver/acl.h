//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRSERVER_ACL_H
#define RRSERVER_ACL_H
#include <librustyaxe/core.h>
bool rrserver_acl_config_init(void);
bool rrserver_acl_validate(void);
void rrserver_acl_register_events(void);
acl_result_t rrserver_acl_check(const char *section, const char *ip, const http_user_t *account, const char *user);
#endif
