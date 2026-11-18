//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRSERVER_ACL_H
#define RRSERVER_ACL_H
#define MAX_ACL_RULES 256
#include <librustyaxe/core.h>
bool rrserver_acl_config_init(void);
bool rrserver_acl_validate(void);
void rrserver_acl_register_events(void);
acl_result_t rrserver_acl_check(const char *section, const char *ip, const http_user_t *account, const char *user);
#endif
