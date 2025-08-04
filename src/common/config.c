#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "common/dict.h"
#include "common/config.h"
#include "common/logger.h"
#include "common/util.file.h"
#include "common/posix.h"

// User configuration values from config file / ui
dict *cfg = NULL;

// Hard-coded defaults
dict *default_cfg = NULL;

// Holds a list of servers where applicable (client and fwdsp)
dict *servers = NULL;

int dict_merge(dict *dst, dict *src) {
   if (!dst || !src) {
      return -1;
   }

   int rank = 0;
   const char *key;
   char *val;
//   while ((rank = dict_enumerate(src, rank, &key, &val)) >= 0) {
   while ((rank = dict_enumerate(src, rank, &key, &val)) >= 0) {
      if (dict_add(dst, key, val) != 0) {
         continue;
      }
   }
   return 0;
}

dict *dict_merge_new(dict *a, dict *b) {
   dict *merged = dict_new();
   if (!merged) {
      return NULL;
   }

   const char *key;
   char *val;
   int rank = 0;

   // Copy from a
   while ((rank = dict_enumerate(a, rank, &key, &val)) >= 0) {
      if (dict_add(merged, key, val) != 0) {
         dict_free(merged);
         return NULL;
      }
   }

   rank = 0;
   // Copy from b (overwriting a’s entries if necessary)
   while ((rank = dict_enumerate(b, rank, &key, &val)) >= 0) {
      if (dict_add(merged, key, val) != 0) {
         dict_free(merged);
         return NULL;
      }
   }

   return merged;
}

bool cfg_set_default(dict *d, char *key, char *val) {
   if (!key || !d) {
      Log(LOG_DEBUG, "config", "cfg_set_default: dict:<%x> key:<%x> is not valid", d, key);
      return true;
   }

//   Log(LOG_CRAZY, "config", "Setting default for dict:<%x>/%s to '%s'", d, key, val);
   if (dict_add(d, key, val) != 0) {
      Log(LOG_WARN, "config", "defcfg dict:<%x> failed to set key %s to val at <%x>", d, key, val);
      return true;
   }

   return false;
}

bool cfg_set_defaults(dict *d, defconfig_t *defaults) {
   if (!d) {
      Log(LOG_CRIT, "config", "cfg_set_defaults: NULL dict");
      return true;
   }

   if (!defaults) {
      Log(LOG_CRIT, "config", "cfg_set_defaults: NULL input");
      return true;
   }

   Log(LOG_CRAZY, "config", "cfg_set_defaults: Loading defaults from <%x>", defaults);

   int i = 0;
   int warnings = 0;
   while (defaults[i].key) {
      Log(LOG_CRAZY, "config", "csd: key:<%x> val:<%x>", defaults[i].key, defaults[i].val);

      if (!defaults[i].val) {
         Log(LOG_DEBUG, "config", "cfg_set_defaults: Skipping key %s as its empty", defaults[i].key);
         i++;
         continue;
      }

      Log(LOG_CRAZY, "config", "cfg_set_defaults: %s => %s", defaults[i].key, defaults[i].val);
      if (cfg_set_default(d, defaults[i].key, defaults[i].val)) {
         Log(LOG_CRIT, "config", "cfg_set_defaults: Failed to set key: %s", defaults[i].key);
         warnings++;
      }

      i++;
   }

   Log(LOG_DEBUG, "config", "Imported %d default settings with %d warnings", i, warnings);
   return true;
}

bool cfg_init(dict *d, defconfig_t *defaults) {
   if (!d) {
      d = dict_new();
   }

   // If defaults supplied, apply them
   if (defaults) {
      return cfg_set_defaults(d, defaults);
   }
   return false;
}

dict *cfg_load(const char *path) {
   int line = 0, errors = 0;
   char buf[32768];
   char *end, *skip,
        *key, *val;
   char this_section[128];

   memset(this_section, 0, sizeof(this_section));

   log_level = LOG_DEBUG;

   if (!file_exists(path)) {
      fprintf(stderr, "Can't find config file %s\n", path);
      return NULL;
   }

   dict *newcfg = dict_new();
   if (!newcfg) {
      Log(LOG_CRIT, "config", "cfg_load OOM creating dict");
      exit(1);
   }

   FILE *fp = fopen(path, "r");
   if (!fp) {
      free(newcfg);
      fprintf(stderr, "Failed to open config %s: %d:%s\n", path, errno, strerror(errno));
      return NULL;
   }

   // rewind configuration file
   fseek(fp, 0, SEEK_SET);

   servers = dict_new();

   // Support for C style comments
   bool in_comment = false;
   do {
      memset(buf, 0, sizeof(buf));
      fgets(buf, sizeof(buf) - 1, fp);
      line++;

      // delete prior whitespace...
      skip = buf;
      while(*skip == ' ')
        skip++;

      // Delete trailing newlines
      end = buf + strlen(buf);
      do {
        *end = '\0';
        end--;
      } while(*end == '\r' || *end == '\n');

      // did we eat the whole line?
      if ((end - skip) <= 0) {
         continue;
      }

      // Look for end of comment
      if (*skip == '*' && *skip+1== '/') {
         in_comment = false;
         Log(LOG_DEBUG, "config", "cfg.end_block_comment: %d", line);
         continue;
      // Look for start of comment
      } else if (*skip == '/' && *(skip + 1) == '*') {
         Log(LOG_DEBUG, "config", "cfg.start_block_comment: %d", line);
         in_comment = true;
         continue;
      // If we're in a comment still, there's no */ on this line, so continue ignoring input
      } else if (in_comment) {
         continue;
      } else if ((*skip == '/' && *(skip+1) == '/') || *skip == '#' || *skip == ';') {
         continue;
      } else if (*skip == '[' && *end == ']') {		// section
         size_t section_len = sizeof(this_section);
         size_t skip_len = strlen(skip);
         size_t copy_len = ((skip_len - 1) > section_len ? section_len : (skip_len - 1));
         memset(this_section, 0, section_len);
         snprintf(this_section, copy_len, "%s", skip + 1);
         Log(LOG_DEBUG, "config", "cfg.section.open: '%s' [%lu]", this_section, strlen(this_section));
         continue;
      } else if (*skip == '@') {			// preprocessor
         if (strncasecmp(skip + 1, "if ", 3) == 0) {
            /* XXX: finish this */
         } else if (strncasecmp(skip + 1, "endif", 5) == 0) {
            /* XXX: finish this */
         } else if (strncasecmp(skip + 1, "else ", 5) == 0) {
            /* XXX: finish this */
         }
      }

      // Configuration data *MUST* be inside of a section, no exceptions.
      if (this_section[0] == '\0') {
         fprintf(stderr, "[Debug] config %s has line outside section header at line %d: %s\n", path, line, buf);
         errors++;
         continue;
      }

      if (strncasecmp(this_section, "general", 7) == 0 ||
          strncasecmp(this_section, "fwdsp", 5) == 0) {
         // Parse configuration line (XXX: GET RID OF STRTOK!)
         key = NULL;
         val = NULL;
         char *eq = strchr(skip, '=');

         if (eq) {
            *eq = '\0';
            key = skip;
            val = eq + 1;

            while (*val == ' ') {
               val++;
            }
         }

         if (!key && !val) {
            Log(LOG_DEBUG, "config", "Skipping NULL KV for: %s", skip);
            continue;
         }


         // if this isn't general section, it needs a prefix on the key
         if (strncasecmp(this_section, "general", 7) != 0) {
            char keybuf[384];
//            Log(LOG_CRIT, "config", "section: %s", this_section);
            memset(keybuf, 0, sizeof(keybuf));
            snprintf(keybuf, sizeof(keybuf), "%s:%.s", this_section, key);
            Log(LOG_CRAZY, "config", "Set key: %s => %s", keybuf, val);
            dict_add(newcfg, keybuf, val);
         } else {
            Log(LOG_CRAZY, "config", "Set key: %s => %s", key, val);
            dict_add(newcfg, key, val);
         }
      } else if (strncasecmp(this_section, "pipelines", 9) == 0) {
         // Parse configuration line (XXX: GET RID OF STRTOK!)
         key = NULL;
         val = NULL;
         char *eq = strchr(skip, '=');

         if (eq) {
            *eq = '\0';
            key = skip;
            val = eq + 1;

            while (*val == ' ') {
               val++;
            }
         }

         if (!key && !val) {
            Log(LOG_DEBUG, "config", "Skipping NULL KV for: %s", skip);
            continue;
         }

         // Store value
         char fullkey[256];
         memset(fullkey, 0, sizeof(fullkey));
         snprintf(fullkey, sizeof(fullkey), "pipeline:%s", key);
         Log(LOG_CRAZY, "config", "Add pipeline %s => %s", fullkey, val);
         dict_add(newcfg, fullkey, val);
      } else if (strncasecmp(this_section, "server:", 7) == 0) {
         key = NULL;
         val = NULL;
         char *eq = strchr(skip, '=');
         char fullkey[256];

         if (eq) {
            *eq = '\0';
            key = skip;
            val = eq + 1;

            while (*val == ' ') {
               val++;
            }

            memset(fullkey, 0, sizeof(fullkey));
            snprintf(fullkey, sizeof(fullkey), "%s.%s", this_section + 7, key);
            // Store value
            dict_add(servers, fullkey, val);
         } else {
            Log(LOG_CRIT, "config", "Malformed line parsing '%s' at %s:%d", buf, path, line);
         }
      } else {
         Log(LOG_CRIT, "config", "Unknown configuration section '%s' parsing '%s' at %s:%d", this_section, buf, path, line);
         errors++;
      }
   } while (!feof(fp));

   if (errors > 0) {
      Log(LOG_INFO, "config", "cfg loaded %d lines from %s with %d warnings/errors",  line, path, errors);
   } else {
      Log(LOG_INFO, "config", "cfg loaded %d lines from %s with no errors", line, path);
   }

   fprintf(stderr, "**** Config Dump ****\n");
   dict_dump(newcfg, stderr);
   return newcfg;
}

const char *cfg_get(const char *key) {
   if (!key) {
      Log(LOG_CRIT, "config", "got cfg_get with NULL key!");
      return NULL;
   }

   const char *p = dict_get(cfg, key, NULL);

   // nope! try default
   if (!p) {
      if (!default_cfg) {
         Log(LOG_DEBUG, "config", "defcfg not found looking for key %s", key);
         return NULL;
      }
      p = dict_get(default_cfg, key, NULL);
      Log(LOG_DEBUG, "config", "returning default value '%s' for key '%s'", p, key);
   } else {
      Log(LOG_CRAZY, "config", "returning user value '%s' for key '%s", p, key);
   }
#if	0
   fprintf(stdout, "-----\n");
   dict_dump(c, stdout);
   fprintf(stdout, "-----\n");
#endif
   return p;
}

bool cfg_get_bool(const char *key, bool def) {
   if (!cfg || !key) {
      return def;
   }

   const char *s = cfg_get(key);
   if (!s) {
      return def;
   }
   if (strcasecmp(s, "true") == 0 ||
       strcasecmp(s, "yes") == 0 ||
       strcasecmp(s, "on") == 0 ||
       strcasecmp(s, "1") == 0) {
      return true;
   } else if (strcasecmp(s, "false") == 0 ||
              strcasecmp(s, "no") == 0 ||
              strcasecmp(s, "off") == 0 ||
              strcasecmp(s, "0") == 0) {
      return false;
   }
   // fallthrough, return default
   return def;
}

int cfg_get_int(const char *key, int def) {
   if (!key) {
      return def;
   }

   const char *s = cfg_get(key);
   if (s) {
      int val = atoi(s);
      return val;
   }
   return def;
}

unsigned int cfg_get_uint(const char *key, unsigned int def) {
   if (!key) {
      return def;
   }

   const char *s = cfg_get(key);
   if (s) {
      char *ep = NULL;
      unsigned int val = (uint32_t)strtoul(s, &ep, 10);
      // incomplete parse
      if (*ep != '\0') {
         return def;
      } else {
         return val;
      }
   }
   return def;
}

static void cfg_print_servers(dict *servers, FILE *fp) {
   if (!servers || !fp)
      return;

   const char *key;
   char *val;
   int rank = 0;
   dict *seen = dict_new();

   while ((rank = dict_enumerate(servers, rank, &key, &val)) >= 0) {
      // Find the prefix before the first '.'
      char *dot = strchr(key, '.');
      if (!dot)
         continue;

      size_t prefix_len = dot - key;
      char prefix[64];
      if (prefix_len >= sizeof(prefix))
         continue;

      strncpy(prefix, key, prefix_len);
      prefix[prefix_len] = '\0';

      // Skip if we've already emitted this prefix
      if (dict_get(seen, prefix, NULL))
         continue;
      dict_add(seen, prefix, "1");

      fprintf(fp, "[server:%s]\n", prefix);

      // Emit all keys with this prefix
      int inner_rank = 0;
      const char *inner_key;
      char *inner_val;
      while ((inner_rank = dict_enumerate(servers, inner_rank, &inner_key, &inner_val)) >= 0) {
         if (strncmp(inner_key, prefix, prefix_len) == 0 && inner_key[prefix_len] == '.') {
            fprintf(fp, "%s=%s\n", inner_key + prefix_len + 1, inner_val ? inner_val : "");
         }
      }

      fputc('\n', fp);
   }

   dict_free(seen);
}

bool cfg_save(dict *d, const char *path) {
   FILE *fp = fopen(path, "w");
   if (!fp) {
      Log(LOG_CRIT, "config", "Failed to open save file: '%s': %d:%s", path, errno, strerror(errno));
      return true;
   }

   dict *merged = NULL;

   // Right-side argument overrides defaults
   merged = dict_merge_new(default_cfg, d);

   // Dump general settings
   fprintf(fp, "[general]\n");
   dict_dump(merged, fp);

   // Release the memory used
   dict_free(merged);

   // Print the server sections
   cfg_print_servers(servers, fp);

   fflush(fp);
   fclose(fp);

   return false;
}

#if	0		// XXX: Not yet ready: config reload events
// This handles stuff like restarting audio pipelines, etc
struct {
   char *kev;
   bool (*callback)();
   char *note;
   struct reload_event *next;
} reload_event;
\
typedef struct reload_event reload_evt_t;

reload_evt_t *reload_events = NULL;
//extern reload_evt_t *reload_events;

bool run_reload_events(const char *key) {
   reload_evt_t *rl = reload_events;

   while (rl) {
      if (strcasecmp(rl->key, key) == 0) {
         Log(LOG_DEBUG, "event", "reload: run callback at <%x> for key '%s'", rl->callback, key);
         rl->callback(key);
      }
      rl = rl->next;
   }
   return false;
}

// XXX: This needs to compare changes and create a dict with the differences in it
bool cfg_apply_new(dict *oldcfg, dict *newcfg) {
   cfg = newcfg;
   return false;
}
#endif	// not yet
