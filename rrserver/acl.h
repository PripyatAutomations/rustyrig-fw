#ifndef RRSERVER_ACL_H
#define RRSERVER_ACL_H
#include <librustyaxe/core.h>
bool rrserver_acl_config_init(void);
bool rrserver_acl_validate(void);
void rrserver_acl_register_events(void);
acl_result_t rrserver_acl_check(const char *section, const char *ip, const http_user_t *account, const char *user);
#endif
