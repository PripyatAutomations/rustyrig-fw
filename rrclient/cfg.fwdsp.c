// librrprotocol/cfg.fwdsp.c: Config section [fwdsp]
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// librrprotocol/cfg.fwdsp.c: config section callbacks for the [fwdsp] and
// [pipelines] sections. Part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Sections other than [general] and [server:*] are dropped by cfg_load()
// unless a callback is registered for them (see cfg_add_callback()).
//
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/cfg.fwdsp.h>

bool config_fwdsp_init(void) {
   static bool initialized = false;

   if (initialized) {
      return true;
   }

   if (!cfg_add_callback(NULL, "fwdsp", config_fwdsp_section_cb)) {
      return false;
   }

   if (!cfg_add_callback(NULL, "pipelines", config_pipeline_section_cb)) {
      return false;
   }

   initialized = true;

   return true;
}

// [fwdsp] keys are stored as fwdsp:<key>.
bool config_fwdsp_section_cb(const char *path, int line, const char *section, const char *buf) {
   if (!buf || section == NULL || strncasecmp(section, "fwdsp", 5) != 0) {
      return false;
   }
   char *tmpbuf = strdup(buf);

   if (!tmpbuf) {
      Log(LOG_CRIT, "cfg.fwdsp", "OOM in config_fwdsp_section_cb!");

      return false;
   }
   char *val = strchr(tmpbuf, '=');

   if (!val || !val[1]) {
      Log(LOG_CRIT, "cfg.fwdsp", "config error at %s:%d: missing value: %s", path, line, buf);
      free(tmpbuf);

      return false;
   }
   *val++ = '\0';               // split at '='

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

   int n = strncmp(tmpbuf, "fwdsp:", 6) == 0 ? snprintf(fullkey, sizeof(fullkey), "%s", tmpbuf) : snprintf(fullkey, sizeof(fullkey), "fwdsp:%s", tmpbuf);
   bool stored = n > 0 && (size_t)n < sizeof(fullkey) && dict_add(cfg, fullkey, val) == 0;
   Log(LOG_CRAZY, "cfg.fwdsp", "Loaded %s=%s from %s:%d", fullkey, val, path, line);
   free(tmpbuf);

   return stored;
}

// [pipelines] keys are stored as pipeline:<codec>.<dir> -- the format bin/fwdsp
// looks up with cfg_get() (see fwdsp/fwdsp.c)
bool config_pipeline_section_cb(const char *path, int line, const char *section, const char *buf) {
   if (!buf || section == NULL || strncasecmp(section, "pipeline", 8) != 0) {
      return false;
   }
   char *tmpbuf = strdup(buf);

   if (!tmpbuf) {
      Log(LOG_CRIT, "cfg.fwdsp", "OOM in config_pipeline_section_cb!");

      return false;
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

   int n = snprintf(fullkey, sizeof(fullkey), "pipeline:%s", id);
   bool stored = n > 0 && (size_t)n < sizeof(fullkey) && dict_add(cfg, fullkey, val) == 0;
   Log(LOG_CRAZY, "cfg.fwdsp", "Loaded %s=%s from config", fullkey, val);
   free(tmpbuf);

   return stored;
}
