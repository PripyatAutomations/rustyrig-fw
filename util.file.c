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
