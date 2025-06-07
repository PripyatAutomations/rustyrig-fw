//
// util.file.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "inc/config.h"
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
#if	defined(HOST_POSIX)
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include "inc/posix.h"
#include "inc/logger.h"

bool file_exists(const char *path) {
// Support for posix hosts
#if	defined(HOST_POSIX)
   struct stat sb;
   int rv = stat(path, &sb);

   // Skip file not found and only show other errors
   if (rv != 0) {
//      Log(LOG_WARN, "core", "file_exists: %s returned %d (%s)", path, errno, strerror(errno));
      return false;
   } else {
      return true;
   }
#endif

   return false;
}

bool is_dir(const char *path) {
// Support for posix hosts
#if	defined(HOST_POSIX)
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
#endif

   return false;
}

const char *expand_path(const char *path) {
   if (path[0] == '~') {
      const char *home = getenv("HOME");
      if (!home) {
         return NULL;
      }

      static char expanded[PATH_MAX];
      snprintf(expanded, sizeof(expanded), "%s%s", home, path + 1);
      return expanded;
   }
   return path;
}

const char *find_file_by_list(const char *files[], int file_count) {
   Log(LOG_DEBUG, "core", "find_file_by_list: We have %d entries in set", file_count);

   for (int i = 0; i < file_count; i++) {
      if (files[i] != NULL) {
         const char *realpath = expand_path(files[i]);
         if (!realpath) {
            continue;
         }

         if (file_exists(realpath)) {
            Log(LOG_INFO, "core", "ffbl: Returning \"%s\"", realpath);
            return realpath;
            break;
         }
         break;
      } else {
         fprintf(stderr, "ffbl: :( files[%d] is NULL in loop", i);
      }
   }

   return NULL;
}
