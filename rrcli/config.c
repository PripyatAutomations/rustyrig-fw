#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "inc/dict.h"
#include "inc/logger.h"
#include "inc/util.file.h"

dict *cfg = NULL;

bool config_load(const char *path) {
   int line = 0, errors = 0;
   char buf[768];
   char *end, *skip,
        *key, *val,
        *this_section = 0;
   char *section = "general";

   if (cfg != NULL) {
      Log(LOG_CRIT, "config", "Config already loaded");
      return true;
   }

   if (!file_exists(path)) {
      Log(LOG_CRIT, "config", "Can't find config file %s", path);
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
      if ((end - skip) <= 0)
         continue;

      if ((*skip == '/' && *skip+1 == '/') ||		// comments
           *skip == '#' || *skip == ';')
         continue;
      else if (*skip == '[' && *end == ']') {		// section
         this_section = strndup(skip + 1, strlen(skip) - 2);
//         fprintf(stderr, "[Info]: cfg.section.open: '%s' [%lu]\n", this_section, strlen(this_section));
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
         fprintf(stderr, "[Debug] config %s has line outside section header at line %d: %s\n", path, line, buf);
         errors++;
         continue;
      }

      // skip sections not the one we are looking for
      if (strncasecmp(section, this_section, strlen(section)) != 0) {
         // Do nothing
         continue;
      }

      if (strncasecmp(this_section, "general", 7) == 0) {
         // Parse configuration line (XXX: GET RID OF STRTOK!)
         key = strtok(skip, "= \n");
         val = strtok(NULL, "= \n");

         // Store value
         dict_add(cfg, key, val);
      } else {
         fprintf(stderr, "[Debug] Unknown configuration section '%s' parsing '%s' at %s:%d\n", this_section, buf, path, line);
         errors++;
      }
   } while (!feof(fp));

   if (errors > 0) {
      fprintf(stderr, "cfg <%s> %d lines loaded with %d errors from %s\n", section, line, errors, path);
   }

//   dict_dump(cfg, stdout);
   return true;
}
   