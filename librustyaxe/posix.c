//
// posix.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * This contains stuff for when we live on a posix host
 *
 * Namely we use optionally use pipes instead of real serial ports
 * and deal with POSIX signals
 */
#include "librustyaxe/config.h"
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
#ifndef _WIN32
#include <signal.h>
#endif
#include <stdio.h>
#include <errno.h>
#include "librustyaxe/posix.h"
#include "librustyaxe/logger.h"

extern bool dying;

extern void shutdown_app(int signum);

// This gets called by our atexit() handler to make sure we clean up temporary files...
void host_cleanup(void) {
    printf("Goodbye!\n");
    // Unlink the cat pipe
//#if	!defined(__RRCLIENT) && !defined(__FWDSP)
//    if (rig.catpipe_fd >= 0) {
//       close(rig.catpipe_fd);
//       rig.catpipe_fd = -1;
//    }
//    unlink(HOST_CAT_PIPE);
//#endif
}

#ifndef _WIN32
static void sighandler(int32_t signum) {
   switch(signum) {
      // Convenience signals
      case SIGHUP:
         Log(LOG_CRIT, "core", "Caught SIGHUP, reloading");
         // XXX: Reload from eeprom
         break;
      case SIGUSR1:
         break;
      case SIGUSR2:
         break;
      // Fatal signals
      case SIGINT:
      case SIGTERM:
//#if	defined(__RRCLIENT) || defined(__FWDSP)
//         shutdown_app(0);
//#else
//         shutdown_rig(0);
//#endif
         exit(1);
        break;
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

   // User signals
   signal(SIGHUP, sighandler);
   signal(SIGUSR1, sighandler);
   signal(SIGUSR2, sighandler);
}
#endif

bool host_init(void) {
#ifndef _WIN32
   init_signals();
#endif
   return false;
}
