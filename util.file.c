#include "config.h"
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

#include "state.h"
#include "posix.h"
#include "logger.h"

bool file_exists(const char *path) {
// Support for posix hosts
#if	defined(HOST_POSIX)
   struct stat sb;
   int rv = stat(path, &sb);

   // Skip file not found and only show other errors
   if (rv != 0) {
      Log(LOG_WARN, "core", "file_exists: %s returned %d (%s)", path, errno, strerror(errno));
      return false;
   } else {
      return true;
   }
#endif

   return false;
}
