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

// Hard-coded defaults (defcfg.c)
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
//      Log(LOG_CRAZY, "config", "csd: key:<%x> val:<%x>", defaults[i].key, defaults[i].val);

      if (!defaults[i].val) {
         Log(LOG_DEBUG, "config", "cfg_set_defaults: Skipping key %s as its empty", defaults[i].key);
         i++;
         continue;
      }

//      Log(LOG_CRAZY, "config", "cfg_set_defaults: %s => %s", defaults[i].key, defaults[i].val);
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
   char *end, *skip, *key, *val;
   char this_section[128];

   memset(this_section, 0, sizeof(this_section));

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

   fseek(fp, 0, SEEK_SET);
   servers = dict_new();

   bool in_comment = false;
   do {
      memset(buf, 0, sizeof(buf));
      if (!fgets(buf, sizeof(buf) - 1, fp)) {
         break;
      }
      line++;

      // skip leading spaces
      skip = buf;
      while (*skip == ' ') {
         skip++;
      }

      // trim trailing newlines
      end = buf + strlen(buf) - 1;
      while (end >= buf && (*end == '\r' || *end == '\n')) {
         *end-- = '\0';
      }

      if ((end - skip) < 0) {
         continue;
      }

      // Handle line continuations
      while (1) {
         // Trim trailing newlines / carriage returns
         while (end >= buf && (*end == '\r' || *end == '\n')) {
            *end-- = '\0';
         }

         // Trim trailing spaces/tabs before checking for '\'
         while (end >= buf && (*end == ' ' || *end == '\t')) {
            *end-- = '\0';
         }

         if (end < buf || *end != '\\') {
            // No continuation
            break;
         }

         // Check if space before '\'
         bool space_before = (end > buf && *(end - 1) == ' ');

         // Remove the backslash
         *end = '\0';
         end--;

         // Also remove trailing spaces before backslash if any remain
         while (end >= buf && (*end == ' ' || *end == '\t')) {
            *end-- = '\0';
         }

         // Read continuation line
         char contbuf[sizeof(buf)];
         if (!fgets(contbuf, sizeof(contbuf), fp)) {
            break;  // EOF or error
         }

         line++;

         // Trim leading whitespace on continuation line
         char *cont = contbuf;
         while (*cont == ' ' || *cont == '\t') {
            cont++;
         }

         // Trim trailing whitespace/newlines on continuation line
         char *e2 = cont + strlen(cont) - 1;
         while (e2 >= cont && (*e2 == '\r' || *e2 == '\n' || *e2 == ' ' || *e2 == '\t')) {
            *e2-- = '\0';
         }

         // Append a space if needed
         if (space_before && strlen(buf) > 0) {
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
         }

         // Append continuation content
         strncat(buf, cont, sizeof(buf) - strlen(buf) - 1);

         // Update end pointer for next loop iteration
         end = buf + strlen(buf) - 1;
      }

      // Now print the fully joined line once
//      fprintf(stderr, "mbi: %s\n", buf);

      /////////////////////////////
      // parse the line contents //
      /////////////////////////////
      if (*skip == '*' && *(skip + 1) == '/') {
         in_comment = false;
         continue;
      } else if (*skip == '/' && *(skip + 1) == '*') {
         in_comment = true;
         continue;
      } else if (in_comment) {
         continue;
      } else if ((*skip == '/' && *(skip + 1) == '/') || *skip == '#' || *skip == ';') {
         continue;
      } else if (*skip == '[' && *end == ']') {
         size_t section_len = sizeof(this_section);
         size_t skip_len = strlen(skip);
         size_t copy_len = ((skip_len - 1) > section_len ? section_len : (skip_len - 1));
         memset(this_section, 0, section_len);
         snprintf(this_section, copy_len, "%s", skip + 1);
         continue;
      }

      if (this_section[0] == '\0') {
         fprintf(stderr, "[Debug] config %s has line outside section header at line %d: %s\n", path, line, buf);
         errors++;
         continue;
      }

      if (strncasecmp(this_section, "general", 7) == 0 ||
          strncasecmp(this_section, "fwdsp", 5) == 0) {
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
            continue;
         }

         if (strncasecmp(this_section, "general", 7) != 0) {
            char keybuf[384];
            memset(keybuf, 0, sizeof(keybuf));
            snprintf(keybuf, sizeof(keybuf), "%s:%.s", this_section, key);
            dict_add(newcfg, keybuf, val);
         } else {
            dict_add(newcfg, key, val);
         }
      } else if (strncasecmp(this_section, "pipelines", 9) == 0) {
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
            continue;
         }

         char fullkey[256];
         memset(fullkey, 0, sizeof(fullkey));
         snprintf(fullkey, sizeof(fullkey), "pipeline:%s", key);
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
      Log(LOG_INFO, "config", "cfg loaded %d lines from %s with %d warnings/errors", line, path, errors);
   } else {
      Log(LOG_INFO, "config", "cfg loaded %d lines from %s with no errors", line, path);
   }

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
   return p;
}


// You *MUST* free the return value
const char *cfg_get_exp(const char *key) {
   if (!key) {
      Log(LOG_CRIT, "config", "got cfg_get_exp with NULL key!");
      return NULL;
   }

   const char *p = cfg_get(key);
   if (!p) {
      Log(LOG_DEBUG, "config", "key '%s' not found", key);
      return NULL;
   }

   char *buf = malloc(MAX_CFG_EXP_STRLEN);
   if (!buf) {
      fprintf(stderr, "OOM in cfg_get_exp!\n");
      return NULL;
   }

   strncpy(buf, p, MAX_CFG_EXP_STRLEN - 1);
   buf[MAX_CFG_EXP_STRLEN - 1] = '\0';

   for (int depth = 0; depth < MAX_CFG_EXP_RECURSION; depth++) {
      char tmp[MAX_CFG_EXP_STRLEN];
      char *dst = tmp;
      const char *src = buf;
      int changed = 0;

      while (*src && (dst - tmp) < MAX_CFG_EXP_STRLEN - 1) {
         if (src[0] == '$' && src[1] == '{') {
            const char *end = strchr(src + 2, '}');

            if (end) {
               size_t klen = end - (src + 2);
               char keybuf[256];

               if (klen >= sizeof(keybuf)) {
                  klen = sizeof(keybuf) - 1;
               }

               memcpy(keybuf, src + 2, klen);
               keybuf[klen] = '\0';

               const char *val = cfg_get(keybuf);
               if (val) {
                  size_t vlen = strlen(val);

                  if ((dst - tmp) + vlen >= MAX_CFG_EXP_STRLEN - 1) {
                     vlen = MAX_CFG_EXP_STRLEN - 1 - (dst - tmp);
                  }

                  memcpy(dst, val, vlen);
                  dst += vlen;
                  changed = 1;
               }
               src = end + 1;
               continue;
            }
         }
         *dst++ = *src++;
      }
      *dst = '\0';

      if (!changed) {
         break; // No more expansions
      }

      strncpy(buf, tmp, MAX_CFG_EXP_STRLEN - 1);
      buf[MAX_CFG_EXP_STRLEN - 1] = '\0';
   }

   // Shrink the allocation down to it's actual size
   size_t final_len = strlen(buf) + 1;
   char *shrunk = realloc(buf, final_len);

   if (shrunk) {
      buf = shrunk;
   }

   fprintf(stderr, "exp: returning %lu bytes for key %s => %s\n", (unsigned long)final_len, key, buf);

   return buf; // Caller must free
}


bool cfg_get_bool(const char *key, bool def) {
   if (!cfg || !key) {
      return def;
   }

   const char *s = cfg_get_exp(key);
   bool rv = def;

   if (!s) {
      return rv;
   }

   if (strcasecmp(s, "true") == 0 ||
       strcasecmp(s, "yes") == 0 ||
       strcasecmp(s, "on") == 0 ||
       strcasecmp(s, "1") == 0) {
      rv = true;
   } else if (strcasecmp(s, "false") == 0 ||
              strcasecmp(s, "no") == 0 ||
              strcasecmp(s, "off") == 0 ||
              strcasecmp(s, "0") == 0) {
      rv = false;
   }

   free((void *)s);
   return rv;
}

int cfg_get_int(const char *key, int def) {
   if (!key) {
      return def;
   }

   const char *s = cfg_get_exp(key);
   if (s) {
      int val = atoi(s);
      free((void *)s);
      return val;
   }
   return def;
}

unsigned int cfg_get_uint(const char *key, unsigned int def) {
   if (!key) {
      return def;
   }

   const char *s = cfg_get_exp(key);
   if (s) {
      char *ep = NULL;
      unsigned int val = (uint32_t)strtoul(s, &ep, 10);
      free((void *)s);

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
   if (!servers || !fp) {
      return;
   }

   const char *key;
   char *val;
   int rank = 0;
   dict *seen = dict_new();

   while ((rank = dict_enumerate(servers, rank, &key, &val)) >= 0) {
      // Find the prefix before the first '.'
      char *dot = strchr(key, '.');
      if (!dot) {
         continue;
      }

      size_t prefix_len = dot - key;
      char prefix[64];
      if (prefix_len >= sizeof(prefix)) {
         continue;
      }

      strncpy(prefix, key, prefix_len);
      prefix[prefix_len] = '\0';

      // Skip if we've already emitted this prefix
      if (dict_get(seen, prefix, NULL)) {
         continue;
      }
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

reload_event_t *reload_events = NULL;

bool run_reload_events(const char *key) {
   if (!reload_events) {
      return true;
   }

   reload_event_t *rl = reload_events;

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

// XXX: Make a function to return a dict of ONLY changes from a -> b
dict *dict_diff(dict *a, dict *b) {
   // XXX: Make this work
   return NULL;
}
