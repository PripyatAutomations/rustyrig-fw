//
// rrserver/cfg.fwdsp.c: config section callbacks for the [fwdsp] and
// [pipeline] sections. Part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Sections other than [general] and [server:*] are dropped by cfg_load()
// unless a callback is registered for them (see cfg_add_callback()).
//
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <librustyaxe/core.h>

// [fwdsp] keys are stored as fwdsp.<key> -- matching the defconfig names
// (fwdsp.subproc.max, fwdsp.hangtime, subproc.debug, ...)
bool config_fwdsp_section_cb(const char *path, int line, const char *section, const char *buf) {
   if (!buf || section == NULL || strncasecmp(section, "fwdsp", 5) != 0) {
      return true;
   }
   char *tmpbuf = strdup(buf);

   if (!tmpbuf) {
      Log(LOG_CRIT, "cfg.fwdsp", "OOM in config_fwdsp_section_cb!");
      return true;
   }
   char *val = strchr(tmpbuf, '=');

   if (!val || !val[1]) {
      Log(LOG_CRIT, "cfg.fwdsp", "config error at %s:%d: missing value: %s", path, line, buf);
      free(tmpbuf);
      return false;
   }
   *val++ = '\0';   // split at '='

   while (*val == ' ' || *val == '\t') {
      val++;
   }
   // trim trailing whitespace
   char *end = val + strlen(val) - 1;

   while (end >= val && (*end == ' ' || *end == '\t')) {
      *end-- = '\0';
   }
   // trim trailing whitespace on key too
   char *kend = tmpbuf + strlen(tmpbuf) - 1;

   while (kend >= tmpbuf && (*kend == ' ' || *kend == '\t')) {
      *kend-- = '\0';
   }
   char fullkey[128];

   if (strncmp(tmpbuf, "fwdsp.", 6) == 0) {
      // Already prefixed
      snprintf(fullkey, sizeof(fullkey), "%s", tmpbuf);
   } else {
      snprintf(fullkey, sizeof(fullkey), "fwdsp.%s", tmpbuf);
   }
   dict_add(cfg, fullkey, val);
   Log(LOG_DEBUG, "cfg.fwdsp", "Loaded %s=%s from %s:%d", fullkey, val, path, line);
   free(tmpbuf);

   return false;
}

// [pipeline] keys are stored as pipeline:<codec>.<dir> -- the format bin/fwdsp
// looks up with cfg_get() (see fwdsp/fwdsp.c)
bool config_pipeline_section_cb(const char *path, int line, const char *section, const char *buf) {
   (void)line;
   (void)path;

   if (!buf || section == NULL || strncasecmp(section, "pipeline", 8) != 0) {
      return true;
   }
   char *tmpbuf = strdup(buf);

   if (!tmpbuf) {
      Log(LOG_CRIT, "cfg.fwdsp", "OOM in config_pipeline_section_cb!");
      return true;
   }
   char *val = strchr(tmpbuf, '=');

   if (!val || !val[1]) {
      Log(LOG_CRIT, "cfg.fwdsp", "config error: pipeline entry missing value: %s", buf);
      free(tmpbuf);
      return false;
   }
   *val++ = '\0';

   while (*val == ' ' || *val == '\t') {
      val++;
   }
   // trim trailing whitespace
   char *end = val + strlen(val) - 1;

   while (end >= val && (*end == ' ' || *end == '\t')) {
      *end-- = '\0';
   }
   // trim key whitespace
   char *kend = tmpbuf + strlen(tmpbuf) - 1;

   while (kend >= tmpbuf && (*kend == ' ' || *kend == '\t')) {
      *kend-- = '\0';
   }
   // Accept both "pc16.rx" and "pipeline:pc16.rx" spellings
   const char *id = tmpbuf;

   if (strncmp(id, "pipeline:", 9) == 0) {
      id += 9;
   }
   char fullkey[128];

   snprintf(fullkey, sizeof(fullkey), "pipeline:%s", id);
   dict_add(cfg, fullkey, val);
   Log(LOG_DEBUG, "cfg.fwdsp", "Loaded %s from config", fullkey);
   free(tmpbuf);

   return false;
}
