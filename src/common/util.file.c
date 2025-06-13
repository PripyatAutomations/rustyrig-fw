//
// util.file.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "common/posix.h"
#include "common/logger.h"

bool file_exists(const char *path) {
// Support for posix hosts
   struct stat sb;
   int rv = stat(path, &sb);

   // Skip file not found and only show other errors
   if (rv != 0) {
//      Log(LOG_WARN, "core", "file_exists: %s returned %d (%s)", path, errno, strerror(errno));
      return false;
   } else {
      return true;
   }
}

bool is_dir(const char *path) {
// Support for posix hosts
   struct stat sb;
   int rv = stat(path, &sb);

   // Skip file not found and only show other errors
   if (rv != 0) {
      Log(LOG_WARN, "core", "is_dir: %s returned %d (%s)", path, errno, strerror(errno));
      return false;
   } else {
      if ((sb.st_mode & S_IFMT) == S_IFDIR) {
         return true;
      }
   }

   return false;
}

char *expand_path(const char *path) {
   if (!path) return NULL;

   if (path[0] == '~') {
      const char *home = getenv("HOME");
      if (!home) {
         return NULL;
      }

      size_t home_len = strlen(home);
      size_t path_len = strlen(path);

      // Skip '~' and optional '/'
      const char *suffix = (path[1] == '/') ? path + 2 : path + 1;

      char *expanded = malloc(home_len + strlen(suffix) + 2);
      if (!expanded) {
         return NULL;
      }

      sprintf(expanded, "%s/%s", home, suffix);
      return expanded;
   }

   return strdup(path);
}

// You MUST free this when done with it
char *find_file_by_list(const char *files[], int file_count) {
   Log(LOG_DEBUG, "core", "find_file_by_list: We have %d entries in set", file_count);

   for (int i = 0; i < file_count; i++) {
      if (files[i]) {
         char *realpath = expand_path(files[i]);
         if (!realpath) {
            continue;
         }

         Log(LOG_INFO, "core", "ffbl: Trying %s", realpath);
         if (file_exists(realpath)) {
            Log(LOG_INFO, "core", "ffbl: Returning \"%s\"", realpath);
            return realpath;
         } else {
            Log(LOG_INFO, "core", "ffbl: file_exists(%s) returns false", realpath);
            free(realpath);
         }
         break;
      } else {
         fprintf(stderr, "ffbl: :( files[%d] is NULL in loop", i);
      }
   }

   Log(LOG_DEBUG, "core", "ffbl: Couldn't find a suitable file");
   return NULL;
}
