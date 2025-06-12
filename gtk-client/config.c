#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "rustyrig/dict.h"
#include "rrclient/config.h"
#include "rustyrig/logger.h"
#include "rustyrig/util.file.h"
#include "rustyrig/posix.h"

dict *cfg = NULL;
dict *default_cfg = NULL;
dict *servers = NULL;

bool cfg_set_default(char *key, char *val) {
   if (!key || !val) {
      printf("cfg_set_default: key:<%x> or val:<%x> NULL\n", key, val);
      return true;
   }

   if (default_cfg == NULL) {
      default_cfg = dict_new();

      if (!default_cfg) {
         Log(LOG_CRIT, "config", "OOM in cfg_set_default");
         shutdown_app(1);
      }
      return true;
   }

   printf("Set default for %s to '%s'\n", key, val);
   dict_add(default_cfg, key, val);

   return false;
}

bool cfg_set_defaults(defconfig_t *defaults) {
   if (!defaults) {
      printf("cfg_set_defaults: NULL input\n");
      return true;
   }

   int i = 0;
   while (defaults[i].key) {
      if (cfg_set_default(defaults[i].key, defaults[i].val)) {
         fprintf(stderr, "Failed to set key: %s\n", defaults[i].key);
      }

      i++;
   }

   printf("Imported %d items\n", i);
   return true;
}

bool cfg_load(const char *path) {
   int line = 0, errors = 0;
   char buf[768];
   char *end, *skip,
        *key, *val,
        *this_section = 0;

   if (!file_exists(path)) {
      Log(LOG_CRIT, "config", "Can't find config file %s", path);
      return true;
   }

   if (cfg || servers) {
      Log(LOG_CRIT, "config", "Config already loaded");
      return true;
   }

   FILE *fp = fopen(path, "r");
   if (!fp) {
      Log(LOG_CRIT, "config", "Failed to open config %s: %d:%s", path, errno, strerror(errno));
      return true;
   }

   // rewind configuration file
   fseek(fp, 0, SEEK_SET);

   cfg = dict_new();
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
      if (*skip == '*' && *skip == '/') {
         in_comment = false;
         fprintf(stderr, "[Debug]: cfg.end_block_comment: %d", line);
         continue;
      // Look for start of comment
      } else if (*skip == '/' && *skip + 1 == '*') {
         fprintf(stderr, "\n[Debug]: cfg.start_block_comment: %d\n", line);
         in_comment = true;
         continue;
      // If we're in a comment still, there's no */ on this line, so continue ignoring input
      } else if (in_comment) {
         fprintf(stderr, ".");
         continue;
      } else if ((*skip == '/' && *skip+1 == '/') ||	// comments
           *skip == '#' || *skip == ';') {
         continue;
      } else if (*skip == '[' && *end == ']') {		// section
         this_section = strndup(skip + 1, strlen(skip) - 2);
//         fprintf(stderr, "[Debug]: cfg.section.open: '%s' [%lu]\n", this_section, strlen(this_section));
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
      if (!this_section) {
//         fprintf(stderr, "[Debug] config %s has line outside section header at line %d: %s\n", path, line, buf);
//         errors++;
         continue;
      }

      if (strncasecmp(this_section, "general", 7) == 0) {
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

         // Store value
         dict_add(cfg, key, val);
      } else if (strncasecmp(this_section, "server:", 7) == 0) {
        // XXX: parse server and add it to list
//         fprintf(stderr, "parse server: %s\n", skip);
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
         }

         memset(fullkey, 0, sizeof(fullkey));
         snprintf(fullkey, sizeof(fullkey), "%s.%s", this_section + 7, key);
//         fprintf(stderr, "fullkey: %s\n", fullkey);

         // Store value
         dict_add(servers, fullkey, val);
      } else {
         fprintf(stderr, "[Debug] Unknown configuration section '%s' parsing '%s' at %s:%d\n", this_section, buf, path, line);
         errors++;
      }
   } while (!feof(fp));

   if (errors > 0) {
      fprintf(stderr, "cfg loaded %d lines from %s with %d warnings/errors\n",  line, path, errors);
   } else {
      fprintf(stderr, "cfg loaded %d lines from %s with no errors\n", line, path);
   }
   return false;
}

const char *cfg_get(char *key) {
   const char *p = dict_get(cfg, key, NULL);

   // nope! try default
   if (!p) {
      p = dict_get(default_cfg, key, NULL);
   }
   return p;
}

int dict_merge(dict *dst, dict *src) {
   if (!dst || !src) {
      return -1;
   }

   int rank = 0;
   char *key, *val;
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

   char *key, *val;
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

bool cfg_save(const char *path) {
   FILE *fp = fopen(path, "w");
   if (!fp) {
      Log(LOG_CRIT, "config", "Failed to open save file: '%s': %d:%s", path, errno, strerror(errno));
      return true;
   }

   dict *merged = NULL;

   // Right-side argument overrides defaults
   merged = dict_merge_new(default_cfg, cfg);
   dict_dump(merged, fp);
   dict_free(merged);

   fflush(fp);
   fclose(fp);

   return false;
}