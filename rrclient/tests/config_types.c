// Verify that typed client defaults reject malformed values before they reach
// the live configuration dictionary. The GTK editor uses the same metadata.
#include <assert.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/config.h>
#include <librustyaxe/dict.h>

extern dict *cfg;
time_t now;

int main(void) {
   cfg = dict_new();
   assert(cfg);
   /* The authoritative room identity belongs to rrserver. */
   assert(cfg_defconfig_find("station.name") == NULL);

   assert(cfg_set_value("debug.http", "true"));
   assert(strcmp(cfg_get("debug.http"), "true") == 0);
   assert(!cfg_set_value("debug.http", "maybe"));

   assert(cfg_set_value("default.tx.power", "12.5"));
   assert(!cfg_set_value("default.tx.power", "12watts"));

   assert(cfg_set_value("debug.loglevel", "warn"));
   assert(!cfg_set_value("debug.loglevel", "verbose"));

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
