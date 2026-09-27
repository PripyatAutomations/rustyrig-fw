// Verify that typed client defaults reject malformed values before they reach
// the live configuration dictionary. The GTK editor uses the same metadata.
#include <assert.h>
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
   /* The authoritative room identity belongs to rrserver. */
   assert(cfg_defconfig_find("station.name") == NULL);
   /* Logging controls use the log.* namespace; old debug.* aliases are gone. */
   assert(cfg_defconfig_find("debug.loglevel") == NULL);
   assert(cfg_defconfig_find("debug.show-ts") == NULL);
   assert(cfg_defconfig_find("debug.http") == NULL);
   assert(cfg_defconfig_find("debug.http.crazy") == NULL);
   assert(cfg_defconfig_find("debug.audio") == NULL);

   assert(cfg_set_value("log.http", "true"));
   assert(strcmp(cfg_get("log.http"), "true") == 0);
   assert(!cfg_set_value("log.http", "maybe"));

   assert(cfg_set_value("default.tx.power", "12.5"));
   assert(!cfg_set_value("default.tx.power", "12watts"));

   assert(cfg_set_value("log.show-ts", "false"));
   assert(!cfg_set_value("log.show-ts", "sometimes"));

   assert(cfg_set_value("ui.userlist-width", "320"));
   assert(!cfg_set_value("ui.userlist-width", "320px"));

   assert(cfg_set_value("ui.shared-input-history", "false"));
   assert(!cfg_set_value("ui.shared-input-history", "sometimes"));

   assert(cfg_set_value("audio.test-mode", "false"));
   assert(!cfg_set_value("audio.test-mode", "sometimes"));

   dict_free(cfg);
   cfg = NULL;
   puts("PASS: typed configuration values and enum choices");
   return 0;
}
