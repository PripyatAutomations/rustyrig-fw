#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rrserver/acl.h>
#include <rrserver/link.h>
#include <rrserver/media.direct.h>
int main(int argc, char **argv) {
   assert(argc == 3);
   unsigned first, last;
   assert(rrserver_rtp_ports("1-65535", &first, &last) && first == 1 && last == 65535);
   assert(rrserver_rtp_ports("8422", &first, &last) && first == last && first == 8422);
   const char *bad[] = {"", "0", "65536", "8422-8421", "8422-", "-8422", "1-2-3", "1x", "1 -2", "9999999999999"};
   for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) assert(!rrserver_rtp_ports(bad[i], &first, &last));
   cfg = dict_new();
   assert(rrserver_acl_config_init() && rrserver_peer_config_init() && rrserver_direct_config_init());
   dict *loaded = cfg_load(argv[1]);
   assert(loaded);
   dict_free(cfg); cfg = loaded;
   assert(rrserver_acl_validate());
   assert(rrserver_acl_check("listener:http", "10.200.1.2", NULL, NULL) == ACL_DEFER);
   assert(rrserver_acl_check("listener:http", "10.200.1.2", NULL, "guest") == ACL_DENY);
   assert(rrserver_acl_check("listener:http", "10.201.1.2", NULL, "guest") == ACL_ALLOW);
   assert(rrserver_acl_check("listener:http", "192.0.2.1", NULL, "guest") == ACL_DENY);
   assert(rrserver_acl_check("listener:https", "10.201.1.2", NULL, "guest") == ACL_DENY);
   http_user_t user = {.enabled = true};
   strcpy(user.name, "TEST"); strcpy(user.privs, "view,chat,admin");
   assert(rrserver_acl_check("listener:link", "127.0.0.1", &user, user.name) == ACL_ALLOW);
   strcpy(user.privs, "view,chat");
   assert(rrserver_acl_check("listener:link", "127.0.0.1", &user, user.name) == ACL_DENY);
   assert(cfg_save(cfg, argv[2]));
   loaded = cfg_load(argv[2]); assert(loaded);
   dict_free(cfg); cfg = loaded;
   assert(rrserver_acl_validate());
   assert(rrserver_acl_check("listener:http", "10.200.1.2", NULL, "guest") == ACL_DENY);
   assert(rrserver_acl_check("listener:http", "10.201.1.2", NULL, "guest") == ACL_ALLOW);
   assert(!strcmp(cfg_get("peer.acl"), "private"));
   assert(!strcmp(cfg_get("peer:branch.acl"), "staff"));
   assert(!strcmp(cfg_get("rtp.ports"), "8422-8499"));
   assert(!strcmp(cfg_get("rtp:staff.ports"), "9000-9010"));
   assert(!strcmp(cfg_get("rtp:staff.acl"), "staff"));
   // Reload removal must drop obsolete permits, not retain prior rule tails.
   FILE *fp = fopen(argv[1], "w"); assert(fp);
   fputs("[acl:private]\ndeny=all\n[listener:http]\nacl=private\n", fp); fclose(fp);
   assert(cfg_reload(argv[1]));
   assert(rrserver_acl_check("listener:http", "10.201.1.2", NULL, "guest") == ACL_DENY);
   fp = fopen(argv[1], "w"); assert(fp);
   fputs("[acl:broken]\nallow=10.0.0.0/99\n[listener:http]\nacl=broken\n", fp); fclose(fp);
   assert(cfg_reload(argv[1]));
   assert(!rrserver_acl_validate());
   assert(rrserver_acl_check("listener:http", "10.201.1.2", NULL, "guest") == ACL_DENY);
   cfg_fini();
   puts("PASS: named ACL dictionaries, repeated rule order, groups, save/load, peer references and fail-closed reload");
}
