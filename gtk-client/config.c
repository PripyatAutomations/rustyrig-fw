#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "rustyrig/dict.h"
#include "rustyrig/logger.h"
#include "rustyrig/util.file.h"

dict *cfg = NULL;
dict *servers = NULL;

bool config_load(const char *path) {
   int line = 0, errors = 0;
   char buf[768];
   char *end, *skip,
        *key, *val,
        *this_section = 0;

   if (!file_exists(path)) {
      Log(LOG_CRIT, "config", "Can't find config file %s", path);
      return true;
   }

   if (cfg != NULL || servers != NULL) {
      Log(LOG_CRIT, "config", "Config already loaded");
      return true;
   }

   FILE *fp = fopen(path, "r");
   if (fp == NULL) {
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
