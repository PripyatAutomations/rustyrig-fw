/*
 * subprocess management:
 *	Here we deal with keeping subprocesses alive.
 * Steps (performed on all subprocesses)
 *	* Start process
 *	* Watch for process to exit
 *	* If zero (success) exit status, immediately restart
 *	* If non-zero (failure) exit status, delay randomly up to 15 seconds before restarting process
 *	* If a process has crashed more than cfg:supervisor/max-crashes in the last cfg:supervisor/max-crash-time then don't bother respawning it..
 * XXX: Add more error checking!
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdbool.h>
//#include <termbox2/termbox2.h>
#include <ev.h>
#include <librustyaxe/core.h>

//extern TextArea *msgbox;
//extern int y;			// from ui.c
static int max_subprocess = 0;
//struct ev_loop *loop = NULL;

// this shouldn't be exported as we'll soon provide utility functions for it
static subproc_t *children[MAX_SUBPROC];

static void subproc_cb(EV_P_ ev_child *w, int revents) {
   // remove the watcher
   ev_child_stop(EV_A_ w);

   // log the event
   log_send(mainlog, LOG_CRIT, "subproc: process %d exited with status %d\n", w->rpid, w->rstatus);

   // Find subproc in children[] that matches
   int i;
   subproc_t *p = NULL;

   for (i = 0; i < MAX_SUBPROC; i++) {
      if (children[i] == NULL) {
         continue;
      }

      // is this our match?
      if (children[i]->pid == w->rpid) {
         p = children[i];
         break;
      }
   }

   // This setting will cause the periodic timer to restart the process soon...
   if (p != NULL) {
      p->restart_time = 0;
      p->needs_restarted = true;
   }
}

static void stdin_cb(EV_P_ ev_io *w, int revents) {
    if (EV_ERROR & revents) {
        log_send(mainlog, LOG_CRIT, "Error event in stdin watcher");
        return;
    }
    log_send(mainlog, LOG_DEBUG, "stdin: write waiting");
//    ev_io_stop(loop, w);
}

static void stdout_cb(EV_P_ ev_io *w, int revents) {
    if (EV_ERROR & revents) {
        log_send(mainlog, LOG_CRIT, "Error event in stdout watcher");
        return;
    }
    log_send(mainlog, LOG_DEBUG, "stdout: read waiting");
//    ev_io_stop(loop, w);
}

static void stderr_cb(EV_P_ ev_io *w, int revents) {
    if (EV_ERROR & revents) {
        log_send(mainlog, LOG_CRIT, "Error event in stderr watcher");
        return;
    }
    log_send(mainlog, LOG_DEBUG, "stderr: read waiting");
//    ev_io_stop(loop, w);
}

bool subproc_start(int slot) {
   subproc_t *p = NULL;

   if (slot < 0 || slot > max_subprocess) {
      log_send(mainlog, LOG_CRIT, "subproc_start called with slot %d that isn't between 0 and max_subprocess (%d), cancelling!", slot, max_subprocess);
      return false;
   }


   if ((p = children[slot]) == NULL) {
      log_send(mainlog, LOG_CRIT, "subproc_start %d failed: no such subprocess in table main");
      return false;
   }

   // setup evloop pointer if not done yet
   if (loop == NULL) {
//      loop = EV_DEFAULT;
   }

   // well look here! we have a commandline to execute!
   if ((p->argc > 0) && (p->argv[0] != NULL)) {
      int saved_errno = 0;
      int pid = -1;

      // XXX: we'll play with this a bit but if it doesn't work out, we'll teach callsign-lookupd about sockets.
      // XXX: it shouldn't be too awful as we have libev to save us the dreadful bits...

      // create the pipes for stdio
      if (pipe(p->_stdin) == -1 || pipe(p->_stdout) == -1 || pipe(p->_stderr) == -1) {
         log_send(mainlog, LOG_CRIT, "subproc_start(%d): pipe() failed: %d: %s", slot, errno, strerror(errno));
         return false;
      }
      pid = fork();
      saved_errno = errno;

      // if it was successful, store it in the process
      if (pid < 0) {
         log_send(mainlog, LOG_CRIT, "error forking subprocess %d: %s - %d: %s", slot, children[slot]->name, saved_errno, strerror(saved_errno));
         return false;
      } else if (pid == 0) {
         // XXX: Confirm/debug this magic when i'm not sleep deprived ;)
         // do some magic...
         close(p->_stdin[1]);		// close write end of stdin of child
         dup2(p->_stdin[0], STDOUT_FILENO);

         close(p->_stdout[0]);		// close read end of stdout of child
         dup2(p->_stdout[1], STDIN_FILENO);

         close(p->_stderr[0]);		// close read end of stderr of child
         dup2(p->_stderr[1], STDERR_FILENO);

         // spawn the process
         if (execv(p->path, p->argv) == -1) {
            saved_errno = errno;
            log_send(mainlog, LOG_CRIT, "subproc_start(%d): error in execv(%s,%p): %d: %s", slot, p->path, p->argv, saved_errno, strerror(saved_errno));
         }
         exit(1);
      } else if (pid == -1) {
         log_send(mainlog, LOG_CRIT, "subproc_start(%d): error in fork(): %d: %s", slot, errno, strerror(errno));
         return false;
      } else {
         // XXX: Confirm/debug this magic when i'm not sleep deprived ;)
         close(p->_stdin[0]);		// close read end of stdin of parent
         close(p->_stdout[1]);		// close write end of stdout of parent
         close(p->_stderr[1]);		// close write end of stderr of parent

#if	0
         // setup ev_io watchers
//         ev_io_init(&p->stdin_watcher, stdin_cb, p->_stdin[1], EV_WRITE);
         ev_io_init(&p->stdout_watcher, stdout_cb, p->_stdout[0], EV_READ);
         ev_io_init(&p->stderr_watcher, stderr_cb, p->_stderr[0], EV_READ);
//         ev_io_start(loop, &p->stdin_watcher);
         ev_io_start(loop, &p->stdout_watcher);
         ev_io_start(loop, &p->stderr_watcher);
#endif

         // succesful start, disable pending restarts
         p->pid = pid;
         p->needs_restarted = 0;
         p->restart_time = 0;

#if	0
         // enable child process watcher in libev, so we can track when it dies...
         ev_child_init(&p->watcher, subproc_cb, pid, 0);
         ev_child_start(loop, &p->watcher);
#endif
      }
   }
   return true;
}

// Create a new subprocess, based on the details provided and start it
int subproc_create(const char *name, const char *path, const char **argv, int argc) {
   subproc_t *sp = NULL;

   if (path == NULL || argv == NULL || argc <= 0) {
      log_send(mainlog, LOG_CRIT, "subproc_create: got invalid (NULL) (argc: %d) arguments.", argc);
      return -1;
   }

   // figure out our subprocess slot...
   int myslot = -1;

   if ((sp = malloc(sizeof(subproc_t))) == NULL) {
      fprintf(stderr, "subproc_create: out of memory!\n");
      exit(ENOMEM);
   }
   // and zero it!
   memset(sp, 0, sizeof(subproc_t));

   // store the name of the process (for PSLIST command, etc)
   size_t name_len = strlen(name);
   memcpy(sp->name, name, name_len);

   // store the path of the process
   size_t path_len = strlen(path);
   memcpy(sp->path, path, path_len);

   // and duplicate the arguments
   sp->argc = argc;
   for (int i = 0; i < sp->argc; i++) {
      if (argv[i] != NULL) {
         sp->argv[i] = strdup(argv[i]);
      }
   }

   // insert it into a free slot...
   for (myslot = 0; myslot <= MAX_SUBPROC; myslot++) {
      if (children[myslot] == NULL) {
         if (myslot > max_subprocess) {
            max_subprocess = myslot;
         }

         children[myslot] = sp;
         log_send(mainlog, LOG_DEBUG, "subproc_create: using slot %d for %s", myslot, name);
         break;
      } else {
         log_send(mainlog, LOG_DEBUG, "subproc_create: skipping slot %d, it points to %p, while storing %s", myslot, children[myslot], name);
      }
   }

   // and start the process slot we created above!
   if (children[myslot] != NULL) {
      subproc_start(myslot);
   } else {
      log_send(mainlog, LOG_CRIT, "couldnt start subprocess, slot init failed. aborting!");
      abort();
   }
   return myslot;
}

static void subproc_delete(int i) {
   if (children[i] == NULL) {
//      ta_printf(msgbox, "$RED$subproc_delete: invalid child process %i (NULL) requested for deletion", i);
      return;
   }

   // if there's a commandline stored, free it
   for (int x = 0; x < children[i]->argc; x++) {
      subproc_t *sp = children[i];
      if (sp->argv[x] != NULL) {
         free(sp->argv[x]);
      }

      free(sp);
      sp = NULL;

      if (max_subprocess > 0)
         max_subprocess--;
   }
}

int subproc_killall(int signum) {
   char *signame = NULL;
   int rv = 0;

   if (signum == SIGTERM) {
      signame = "SIGTERM";
   } else if (signum == SIGKILL) {
      signame = "SIGKILL";
   } else if (signum == SIGHUP) {
      signame = "SIGHUP";
   } else if (signum == SIGUSR1) {
      signame = "SIGUSR1";
   } else if (signum == SIGUSR2) {
      signame = "SIGUSR2";
   } else {
      signame = "INVALID";
   }

   if (max_subprocess > MAX_SUBPROC) {
//      ta_printf(msgbox, "$RED$subproc_killall: max_subprocess (%d) > MAX_SUBPROC (%d), this is wrong!", max_subprocess, MAX_SUBPROC);
      log_send(mainlog, LOG_CRIT, "subproc_killall: max_subprocess (%d) > MAX_SUBPROC (%d), this is wrong!", max_subprocess, MAX_SUBPROC);
//      tb_present();
      exit(200);
   }

   int i = 0;
   for (i = 0; i <= MAX_SUBPROC; i++) {
      subproc_t *sp = children[i - 1];

      if (sp == NULL) {
         continue;
      }

      // catch obvious errors (init is pid 1)
      if (sp->pid <= 1) {
         continue;
      }

//      ta_printf(msgbox, "$YELLOW$sending %s (%d) to child process %s <%d>...", signame, signum, sp->name, sp->pid);
      log_send(mainlog, LOG_NOTICE, "sending %s (%d) to child process %s <%d>...", signame, signum, sp->name, sp->pid);
//      tb_present();

      // disable watchdog / pending restarts
      sp->restart_time = 0;
      sp->needs_restarted = 0;
      sp->watchdog_start = 0;
      sp->watchdog_events = 0;

      // if successfully sent signal, increment rv, so we'll sleep if called from subproc_shutdown()
      if (kill(sp->pid, signum) == 0)
         rv++;

      // Try waitpid(..., WNOHANG) to see if the pid is still alive
      time_t wstart = time(NULL);
      int wstatus;
      int pid = waitpid(sp->pid, &wstatus, WNOHANG);

      // An error occured
      if (pid == -1) {
        continue;
      } else if (pid == 0) {
        // The process is still running
//        ta_printf(msgbox, "$YELLOW$-- no response, sleeping 3 seconds before next attempt...");
        sleep(3);
        continue;
      } else if (pid == sp->pid) {
        // the process has terminated
        // we can delete the subproc and avoid sending further signals to a dead PID...
        subproc_delete(i);
      }
   }

   // return > 0, if we sent any kill signals
   return rv;
}

// Shut down subprocesses and throw a message to the console to let the operator know what's happening
void subproc_shutdown_all(void) {
   dying = true;
   if (subproc_killall(SIGTERM) > 0)
      if (subproc_check_all() > 0)
         sleep(2);

   if (subproc_killall(SIGKILL) > 0)
      if (subproc_check_all() > 0)
         sleep(1);
}

static time_t get_random_interval(int min, int max) {
   unsigned int  seed;
   time_t rs_time;
   seed = arc4random();
   srand(seed);
   rs_time = (rand() % (max - min) + min);

   return rs_time;
}

// Check all subprocesses to make sure they're all still alive or start them as needed...
int subproc_check_all(void) {
   int rv = 0;

   if (dying) {
      return 0;
   }

   // scan the child process table and see if any have died
   for (int i = 0; i < MAX_SUBPROC; i++) {
      subproc_t *sp = children[i];

      if (sp == NULL) {
         continue;
      }

      // check if the process is still alive
      time_t wstart = time(NULL);
      int wstatus;
      int pid = waitpid(sp->pid, &wstatus, WNOHANG);

      if (pid == -1) {
        // an error occured
        log_send(mainlog, LOG_DEBUG, "subproc_check_all: waitpid on subprocess %d returned %d (%s)", i, errno, strerror(errno));
        continue;
      } else if (pid == 0) {
        // process is still alive, count it
        rv++;
        continue;
      } else if (sp->pid == pid) {
        // process has exited
        // mark the PID as invalid and needs_restarted
        sp->pid = -1;
        sp->needs_restarted = 1;
        int watchdog_expire = cfg_get_int(cfg, "supervisor/max-crash-time");
        int watchdog_max_events = cfg_get_int(cfg, "supervisor/max-crashes");

        // are we already performing watchdog on this subproc due to previous crashes??
        if (sp->watchdog_start == 0) {
           // nope, start the watchdog
           sp->watchdog_start = now;
           sp->watchdog_events = 0;
        } else if (sp->watchdog_start > 0) {
           // has the watchdog expired?
           if (sp->watchdog_start + watchdog_expire <= now) {
              // has there been a reasonable number of watchdog events?
              if (sp->watchdog_events < watchdog_max_events) {
                 log_send(mainlog, LOG_NOTICE, "subprocess %d (%s) has restored normal operation. It crashed %d times in %lu seconds.", i, sp->name, sp->watchdog_events, (now - sp->watchdog_start));
                 sp->watchdog_start = 0;
                 sp->watchdog_events = 0;
              } else { // disable the service
                 log_send(mainlog, LOG_CRIT, "subprocess %d (%s) has crashed %d times in %lu seconds. disabling restarts", i, sp->name, sp->watchdog_events, (now - sp->watchdog_start));
              }

              // either way, we don't need a restart...
              sp->needs_restarted = 0;
              sp->restart_time = 0;
           }
        } else { // watchdog is still active
           sp->needs_restarted = 1;
           sp->watchdog_events++;
           log_send(mainlog, LOG_DEBUG, "subprocess %d (%s) has crashed. This is the %d time in %lu seconds. It will be disabled after %d times.", i, sp->name, sp->watchdog_events, (now - sp->watchdog_start), watchdog_max_events);
        }

      } else {
        log_send(mainlog, LOG_DEBUG, "unexpected return value %d from waitpid(%d) for subproc %d", pid, sp->pid, i);
      }

      // schedule 3-15 seconds in the future, if not already set...
      if (!dying && sp->needs_restarted && (sp->restart_time == 0)) {
         sp->restart_time = get_random_interval(3, 15) + now;
         log_send(mainlog, LOG_CRIT, "subprocess %d (%s) exited, registering it for restart in %lu seconds", i, sp->name, (sp->restart_time - now));
      }
   }
   return rv;
}

bool subproc_init(void) {
   memset(children, 0, sizeof(children));
   return true;
}
