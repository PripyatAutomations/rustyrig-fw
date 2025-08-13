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
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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
    if (!path) {
       return NULL;
    }

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>

    const char *home = getenv("USERPROFILE");
    if (!home || strlen(home) > 1024) {
       return NULL;
    }

    int home_allocated = 0;
    const char *drive = NULL;
    const char *path_part = NULL;

    // Handle '~' expansion
    if (path[0] == '~') {
        if (!home) {
            drive = getenv("HOMEDRIVE");
            path_part = getenv("HOMEPATH");
            if (drive && path_part) {
                size_t drive_len = strlen(drive);
                size_t path_len = strlen(path_part);
                char *temp_home = malloc(drive_len + path_len + 1);
                if (!temp_home) {
                   return NULL;
                }
                snprintf(temp_home, sizeof(temp_home), "%s%s", drive, path_part);
                home = temp_home;
                home_allocated = 1;
            } else {
                return NULL;
            }
        }

        // Skip '~' and optional slash
        const char *suffix = (path[1] == '/' || path[1] == '\\') ? path + 2 : path + 1;

        size_t len = strlen(home) + strlen(suffix) + 2;
        char *expanded = malloc(len);
        if (!expanded) {
            if (home_allocated) {
               free((void *)home);
            }
            return NULL;
        }
        snprintf(expanded, len, "%s\\%s", home, suffix);

        if (home_allocated) {
           free((void *)home);
        }
        return expanded;
    }

    // Handle %VAR% expansion
    char tmp[MAX_PATH];
    DWORD n = ExpandEnvironmentStringsA(path, tmp, MAX_PATH);
    if (n == 0 || n > MAX_PATH) {
        return NULL;
    }

    return strdup(tmp);
#else
    // POSIX: Handle ~
    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) {
           return NULL;
        }

        const char *suffix = (path[1] == '/') ? path + 2 : path + 1;
        size_t len = strlen(home) + strlen(suffix) + 2;
        char *expanded = malloc(len);
        if (!expanded) {
           return NULL;
        }

        snprintf(expanded, len, "%s/%s", home, suffix);
        return expanded;
    }

    return strdup(path);
#endif
}

// You MUST free this when done with it
char *find_file_by_list(const char *files[], int file_count) {
   Log(LOG_DEBUG, "core", "find_file_by_list: We have %d entries in set", file_count);

   for (int i = 0; i < file_count; i++) {
      if (files[i]) {
         char *fullpath = expand_path(files[i]);

         if (!fullpath) {
            continue;
         }

         Log(LOG_INFO, "core", "ffbl: Trying %s", fullpath);
         if (file_exists(fullpath)) {
            Log(LOG_INFO, "core", "ffbl: Returning \"%s\"", fullpath);
            return fullpath;
         } else {
            Log(LOG_INFO, "core", "ffbl: file_exists(%s) returns false", fullpath);
            free(fullpath);
 	    continue;
         }
      } else {
         fprintf(stderr, "ffbl: :( files[%d] is NULL in loop", i);
      }
   }

   Log(LOG_DEBUG, "core", "ffbl: Couldn't find a suitable file");
   return NULL;
}
