// Verify that server defaults expose type metadata and reject malformed values.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/config.h>
#include <librustyaxe/dict.h>

extern dict *cfg;
extern defconfig_t defcfg[];
time_t now;

int main(void) {
   cfg = dict_new();
   assert(cfg);

   const defconfig_t *test_mode = cfg_defconfig_find("audio.test-mode");
   assert(test_mode);
   assert(test_mode->type == DEFCONFIG_BOOL);
   assert(strcmp(test_mode->val, "true") == 0);

   assert(cfg_set_value("backend.active", "hamlib"));
   assert(!cfg_set_value("backend.active", "invalid"));
   assert(cfg_set_value("recording.codec", "flac"));
   assert(!cfg_set_value("recording.codec", "opus"));
   assert(cfg_set_value("net.http.port", "9000"));
   assert(!cfg_set_value("net.http.port", "9000/tcp"));
   assert(cfg_set_value("net.http.enabled", "false"));
   assert(!cfg_set_value("net.http.enabled", "sometimes"));
   assert(cfg_set_value("path.db.master", "~/master.db"));

   dict_free(cfg);
   cfg = NULL;
   puts("PASS: typed server configuration values and enum choices");
   return 0;
}
