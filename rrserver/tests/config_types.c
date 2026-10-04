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
   for (size_t i = 0; defcfg[i].key; i++) {
      assert(defcfg[i].help && defcfg[i].help[0]);
      if (defcfg[i].type == DEFCONFIG_ENUM) {
         assert(defcfg[i].choices && defcfg[i].choices[0]);
      }
      for (size_t j = i + 1; defcfg[j].key; j++) {
         assert(strcmp(defcfg[i].key, defcfg[j].key) != 0);
      }
   }

   /* Logging controls use log.* consistently; these old debug aliases must
    * not silently reappear in the generated configuration editor. */
   assert(cfg_defconfig_find("debug.http") == NULL);
   assert(cfg_defconfig_find("debug.http.crazy") == NULL);
   assert(cfg_defconfig_find("debug.mongoose") == NULL);
   assert(cfg_defconfig_find("debug.show-ts") == NULL);

   const defconfig_t *test_mode = cfg_defconfig_find("audio.test-mode");
   assert(test_mode);
   assert(test_mode->type == DEFCONFIG_BOOL);
   assert(strcmp(test_mode->val, "true") == 0);

   assert(cfg_set_value("rig:rig0.backend", "hamlib"));
   assert(!cfg_set_value("rig:rig0.backend", "invalid"));
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
