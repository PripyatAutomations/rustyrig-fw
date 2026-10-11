#include <assert.h>
#include <unistd.h>
static char site_path[512];
#define RRCLIENT_SYSTEM_CONFIG_PATH site_path
#include <rrclient/config.layers.c>
int main(void) {
   char directory[] = "/tmp/rrclient-config-layers-XXXXXX";
   assert(mkdtemp(directory));
   char site[512], user[512];
   snprintf(site, sizeof(site), "%s/site.cfg", directory);
   snprintf(user, sizeof(user), "%s/user.cfg", directory);
   FILE *file = fopen(site, "w");
   assert(file);
   fputs("[general]\nlog.level=info\n[server:site]\nserver.url=ws://example.test/ws/\nserver.user=site-user\n", file);
   fclose(file);
   file = fopen(user, "w");
   assert(file);
   fputs("[server:site]\nserver.pass=personal-secret\nserver.user=personal-user\n", file);
   fclose(file);
   dict *merged = read_layers(site, user);
   assert(merged);
   assert(!strcmp(dict_get(merged, "log.level", ""), "info"));
   assert(!strcmp(dict_get(merged, "server:site.server.url", ""), "ws://example.test/ws/"));
   assert(!strcmp(dict_get(merged, "server:site.server.user", ""), "personal-user"));
   assert(!strcmp(dict_get(merged, "server:site.server.pass", ""), "personal-secret"));
   cfg = merged;
   layered = true;
   config_file = strdup(user);
   snprintf(site_path, sizeof(site_path), "%s", site);
   assert(rrclient_config_reload(config_file));
   assert(!strcmp(dict_get(cfg, "server:site.server.pass", ""), "personal-secret"));
   unlink(user);
   assert(rrclient_config_reload(config_file));
   assert(!strcmp(dict_get(cfg, "server:site.server.user", ""), "site-user"));
   assert(!dict_get(cfg, "server:site.server.pass", NULL));
   dict_free(cfg);
   cfg = NULL;
   free((void *)config_file);
   config_file = NULL;
   merged = read_layers(site, user);
   assert(merged && !strcmp(dict_get(merged, "server:site.server.user", ""), "site-user"));
   assert(!dict_get(merged, "server:site.server.pass", NULL));
   dict_free(merged);
   unlink(site);
   rmdir(directory);
   puts("PASS: system settings survive user overlays; user credentials override and removal restores site values");
}
