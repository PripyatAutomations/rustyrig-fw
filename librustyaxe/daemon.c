#include <librustyaxe/core.h>
#include <signal.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int pidfd = -1;
const char *pidfile = NULL;
extern char *progname;
extern bool dying;

// Detach from the console and go into background
int daemonize(void) {
   struct stat sb;

   pidfile = cfg_get("path.pid-file");

   if (pidfile == NULL) {
      Log(LOG_CRIT, "daemon", "no pidfile specified in path.pid-file");
      exit(255);
   }

   if (stat(pidfile, &sb) == 0) {
      Log(LOG_CRIT, "daemon", "pidfile %s already exists, bailing!", pidfile);
      exit(1);
   }

   bool daemonize = cfg_get_bool("core.daemonize", false);

   // are we configured to daemonize?
   if (daemonize) {
      Log(LOG_CRIT, "daemon", "going to the background...");
      pid_t pid = fork();

      if (pid < 0) {
         Log(LOG_CRIT, "daemon", "daemonize: Unable to fork(): %d (%s)", errno, strerror(errno));
         exit(EXIT_FAILURE);
      } else if (pid > 0) {
         // parent exiting
         exit(EXIT_SUCCESS);
      }

      // set umask for created files
      umask(0);

      // attempt to fork our own session id
      pid_t sid = setsid();
      if (sid < 0) {
         Log(LOG_CRIT, "daemon", "daemonize: Unable to create new SID for child process: %d (%s)", errno, strerror(errno));
         exit(EXIT_FAILURE);
      }
   }

   // save pid file
   pidfd = open(pidfile, O_RDWR | O_CREAT | O_SYNC, 0600);
   if (pidfd == -1) {
      Log(LOG_CRIT, "daemon", "daemonize: opening pid file %s failed: %d (%s)", pidfile, errno, strerror(errno));
      exit(EXIT_FAILURE);
   }

   // try to lock the pid file, so we can ensure only one instance runs
   if (lockf(pidfd, F_TLOCK, 0) != 0) {
      Log(LOG_CRIT, "daemon", "daemonize: failed to lock pid file %s: %d (%s)", pidfile, errno, strerror(errno));
      unlink(pidfile);
      exit(EXIT_FAILURE);
   }

   // Print the process id to pidfd
   char buf[10];
   memset(buf, 0, 10);
   sprintf(buf, "%d", getpid());
   write(pidfd, buf, strlen(buf));

   // only close stdio if daemonizing
   if (daemonize) {
      // close stdio
      close(STDIN_FILENO);
      close(STDOUT_FILENO);
      close(STDERR_FILENO);
   }

   return 0;
}
