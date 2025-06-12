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

static defconfig_t defcfg[] = {
   { "audio.debug",				":*3" },
   { "audio.pipeline.rx",			"" },
   { "audio.pipeline.rx.format",		"" },
   { "audio.pipeline.tx",			"" },
   { "audio.pipeline.tx.format",		"" },
   { "audio.pipeline.rx.pcm16", 		"" },
   { "audio.pipeline.tx.pcm16",			"" },
   { "audio.pipeline.rx.pcm44",			"" },
   { "audio.pipeline.tx.pcm44",			"" },
   { "audio.pipeline.rx.opus",			"" },
   { "audio.pipeline.tx.opus",			"" },
   { "audio.pipeline.rx.flac",			"" },
   { "audio.pipeline.tx.flac",			"" },
   { "audio.volume.rx",				"30" },
   { "audio.volume.tx",				"20" },
   { "cat.poll-blocking",			"2" },
   { "debug.http",				"false" },
   { "default.volume.rx",			"30" },
   { "default.tx.power",			"30" },
   { "server.auto-connect",			"" },
   { "ui.main.height",				"600" },
   { "ui.main.width",				"800" },
   { "ui.main.x",				"0" },
   { "ui.main.y",				"200" },
   { "ui.main.on-top",				"false" },
   { "ui.main.raised",				"true" },
   { "ui.userlist.height",			"300" },
   { "ui.userlist.width",			"250" },
   { "ui.userlist.x",				"0" },
   { "ui.userlist.y",				"0" },
   { "ui.userlist.on-top",			"false" },
   { "ui.userlist.raised",			"true" },
   { "ui.userlist.hidden",			"false" },
   { "ui.show-pings",				"true" },
   { NULL,					NULL }
};

bool cfg_set_default(char *key, char *val) {
   if (!key) {
      Log(LOG_DEBUG, "config", "cfg_set_default: key:<%x> NULL", key);
      return true;
   }

   if (default_cfg == NULL) {
      default_cfg = dict_new();

      if (!default_cfg) {
         Log(LOG_CRIT, "config", "OOM in cfg_set_default");
         shutdown_app(1);
         return true;
      }
   }

   Log(LOG_CRAZY, "config", "Set default for %s to '%s'", key, val);
   dict_add(default_cfg, key, val);

   return false;
}

bool cfg_set_defaults(defconfig_t *defaults) {
   if (!defaults) {
      Log(LOG_CRIT, "config", "cfg_set_defaults: NULL input");
      return true;
   }

   Log(LOG_CRAZY, "config", "cfg_set_defaults: Loading defaults from <%x>", defaults);

   int i = 0;
   while (defaults[i].key) {
      if (cfg_set_default(defaults[i].key, defaults[i].val)) {
         Log(LOG_CRIT, "config", "cfg_set_defaults: Failed to set key: %s", defaults[i].key);
      }

      i++;
   }

   Log(LOG_CRAZY, "config", "Imported %d items", i);
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

   // Load the default settings early
   cfg_set_defaults(defcfg);
   cfg = dict_new();

   FILE *fp = fopen(path, "r");
   if (!fp) {
      Log(LOG_CRIT, "config", "Failed to open config %s: %d:%s", path, errno, strerror(errno));
      return true;
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
      if (*skip == '*' && *skip == '/') {
         in_comment = false;
         Log(LOG_DEBUG, "config", "cfg.end_block_comment: %d", line);
         continue;
      // Look for start of comment
      } else if (*skip == '/' && *skip + 1 == '*') {
         Log(LOG_DEBUG, "config", "cfg.start_block_comment: %d", line);
         in_comment = true;
         continue;
      // If we're in a comment still, there's no */ on this line, so continue ignoring input
      } else if (in_comment) {
         continue;
      } else if ((*skip == '/' && *skip+1 == '/') || *skip == '#' || *skip == ';') {
         continue;
      } else if (*skip == '[' && *end == ']') {		// section
         this_section = strndup(skip + 1, strlen(skip) - 2);
//         fprintf(stderr, "[Debug]: cfg.section.open: '%s' [%lu]\n", this_section, strlen(this_section));
         Log(LOG_CRAZY, "config", "cfg.section.open: '%s' [%lu]", this_section, strlen(this_section));
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

         if (!key && !val) {
            Log(LOG_DEBUG, "config", "Skipping NULL KV for: %s", skip);
            continue;
         }

         // Store value
         Log(LOG_CRAZY, "config", "Set key: %s => %s", key, val);
         dict_add(cfg, key, val);
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
         }

         memset(fullkey, 0, sizeof(fullkey));
         snprintf(fullkey, sizeof(fullkey), "%s.%s", this_section + 7, key);
//         fprintf(stderr, "fullkey: %s\n", fullkey);

         // Store value
         dict_add(servers, fullkey, val);
      } else {
//         fprintf(stderr, "[Debug] Unknown configuration section '%s' parsing '%s' at %s:%d\n", this_section, buf, path, line);
         Log(LOG_CRIT, "config", "Unknown configuration section '%s' parsing '%s' at %s:%d", this_section, buf, path, line);
         errors++;
      }
   } while (!feof(fp));

   if (errors > 0) {
//      fprintf(stderr, "cfg loaded %d lines from %s with %d warnings/errors\n",  line, path, errors);
      Log(LOG_INFO, "config", "cfg loaded %d lines from %s with %d warnings/errors",  line, path, errors);
   } else {
//      fprintf(stderr, "cfg loaded %d lines from %s with no errors\n", line, path);
      Log(LOG_INFO, "config", "cfg loaded %d lines from %s with no errors", line, path);
   }
   return false;
}

const char *cfg_get(char *key) {
   if (!key) {
      Log(LOG_CRIT, "config", "got cfg_get with NULL key!");
      return NULL;
   }

   const char *p = dict_get(cfg, key, NULL);

   // nope! try default
   if (!p) {
      p = dict_get(default_cfg, key, NULL);
      Log(LOG_DEBUG, "config", "returning default value '%s' for key '%s'", p, key);
   } else {
      Log(LOG_CRAZY, "config", "returning user value '%s' for key '%s", p, key);
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

static void cfg_print_servers(dict *servers, FILE *fp) {
   if (!servers || !fp)
      return;

   char *key, *val;
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
      char *inner_key, *inner_val;
      while ((inner_rank = dict_enumerate(servers, inner_rank, &inner_key, &inner_val)) >= 0) {
         if (strncmp(inner_key, prefix, prefix_len) == 0 && inner_key[prefix_len] == '.') {
            fprintf(fp, "%s=%s\n", inner_key + prefix_len + 1, inner_val ? inner_val : "");
         }
      }

      fputc('\n', fp);
   }

   dict_free(seen);
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