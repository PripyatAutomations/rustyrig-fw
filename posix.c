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
#include "inc/config.h"
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
#include "inc/posix.h"
#include "inc/logger.h"

extern bool dying;

#if	defined(__RRCLIENT) || defined(__FWDSP)
extern void shutdown_app(int signum);
#else
#include "inc/state.h"
#endif

// This gets called by our atexit() handler to make sure we clean up temporary files...
void host_cleanup(void) {
    printf("Goodbye!\n");
    // Unlink the cat pipe
#if	!defined(__RRCLIENT) && !defined(__FWDSP)
    if (rig.catpipe_fd >= 0) {
       close(rig.catpipe_fd);
       rig.catpipe_fd = -1;
    }
#endif
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
//         dying = true;
#if	defined(__RRCLIENT) || defined(__FWDSP)
         shutdown_app(0);
#else
         shutdown_rig(0);
#endif
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

#endif	// HOST_POSIX
