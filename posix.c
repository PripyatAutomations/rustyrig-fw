/*
 * This contains stuff for when we live on a posix host
 *
 * Namely we use optionally use pipes instead of real serial ports
 * and deal with POSIX signals
 */
#include "config.h"
#if	!defined(HOST_POSIX)
#error "This is only valid on host posix, please check the GNUmakefile!"
#else
#include <sys/stat.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include "state.h"
#include "posix.h"
#include "logger.h"

// This gets called by our atexit() handler to make sure we clean up temporary files...
void host_cleanup(void) {
    printf("Goodbye!\n");
    // Unlink the cat pipe
    if (rig.catpipe_fd >= 0) {
       close(rig.catpipe_fd);
       rig.catpipe_fd = -1;
    }
    unlink(HOST_CAT_PIPE);
}

static void sighandler(int32_t signum) {
   switch(signum) {
      // Convenience signals
      case SIGHUP:
         Log(LOG_INFO, "core", "Caught SIGHUP");
         // XXX: Reload from eeprom
         break;
      case SIGUSR1:
         break;
      case SIGUSR2:
         break;
      // Fatal signals
      case SIGINT:
      case SIGTERM:
      case SIGKILL:
         shutdown_rig(0);
      default:
         Log(LOG_CRIT, "core", "Caught unknown signal %d", signum);
         break;
   }
}

// set up signal handlers
void init_signals(void) {
   // Fatal signals
   signal(SIGINT, sighandler);
   signal(SIGTERM, sighandler);
   signal(SIGKILL, sighandler);
   // User signals
   signal(SIGHUP, sighandler);
   signal(SIGUSR1, sighandler);
   signal(SIGUSR2, sighandler);
}

bool host_init(void) {
   init_signals();
   return false;
}

bool file_exists(const char *path) {
   struct stat sb;
   int rv = stat(path, &sb);

   // Skip file not found and only show other errors
   if (rv != 0) {
      Log(LOG_DEBUG, "core", "file_exists: %s returned %d (%s)", path, errno, strerror(errno));
      return false;
   } else {
      return true;
   }

   return false;
}

#endif	// HOST_POSIX
