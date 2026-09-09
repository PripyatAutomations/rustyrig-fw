//
// rrclient/cat.pty.c: PTY-based CAT interface for rrclient
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// We create a pseudo-terminal and expose its slave as ./dev/ttyCAT0 (config:
// cat.pty.path).  External software such as hamlib/rigctl/WSJT-X opens the
// slave as if it were a serial port; everything written there is fed line by
// line through the CAT parsers in cat.c / cat.yaesu.c / cat.kpa500.c.
//
// The master side is watched from the glib main loop with a GIOChannel watch
// (g_io_add_watch), so no extra threads and no polling: we only wake up when
// the slave has data or the last writer hangs up.
//
// For posix_openpt/grantpt/unlockpt/ptsname_r
#ifndef _XOPEN_SOURCE
#define	_XOPEN_SOURCE	600
#endif
#ifndef _GNU_SOURCE
#define	_GNU_SOURCE
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <rrclient/cat.h>
#include <rrclient/cat.pty.h>

static int pty_master = -1;              // master side fd
static GIOChannel *pty_chan = NULL;      // glib wrapper around master
static guint pty_watch_id = 0;           // glib watch source id
static char pty_slave[128] = { 0 };      // ptsname() result
static char pty_link[256] = { 0 };       // ./dev/ttyCAT0 path
static char pty_buf[512] = { 0 };        // partial line accumulator
static size_t pty_buf_len = 0;

bool cat_pty_active(void) {
   return pty_master >= 0;
}

int cat_pty_fd(void) {
   return pty_master;
}

int cat_pty_printf(const char *fmt, ...) {
   if (pty_master < 0 || !fmt) {
      return -1;
   }

   char buf[512];
   va_list ap;
   va_start(ap, fmt);
   int len = vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);

   if (len < 0 || (size_t)len >= sizeof(buf)) {
      return -1;
   }

   ssize_t w = write(pty_master, buf, len);
   return (w == len) ? len : -1;
}

// Feed accumulated bytes through the CAT parser line by line.  Lines are
// terminated the way the Yaesu protocol expects: a ';' ends each command.
static void cat_pty_feed(char *data, size_t len) {
   for (size_t i = 0; i < len; i++) {
      char c = data[i];

      if (c == '\r' || c == '\n') {
         continue;   // ignore newlines; Yaesu CAT frames end with ';'
      }

      if (pty_buf_len + 1 >= sizeof(pty_buf)) {
         // Overrun: drop this line, resync at the next ';'
         pty_buf_len = 0;
         Log(LOG_CRIT, "cat.pty", "PTY line buffer overrun, dropping line");
         continue;
      }

      pty_buf[pty_buf_len++] = c;

      if (c == ';') {
         pty_buf[pty_buf_len - 1] = '\0';
         if (pty_buf_len > 1) {
            Log(LOG_CRAZY, "cat.pty", "Read PTY CAT cmd: %s", pty_buf);
            rr_cat_parse_line(pty_buf);
         }
         pty_buf_len = 0;
      }
   }
}

static gboolean cat_pty_readable(GIOChannel *chan, GIOCondition cond, gpointer user);
static gboolean cat_pty_rearm(gpointer user);
static bool pty_hup_logged = false;   // log the HUP transition only once

// Called when the last slave writer hangs up: stop watching (the HUP fires
// continuously otherwise, busy-looping the main loop) and re-arm after a
// short delay so a new opener is picked up.  NB: the HUP state persists
// until a new process opens the slave, so re-arms keep firing HUP; only
// the FIRST one is logged.
static void cat_pty_sleep(void) {
   if (pty_watch_id != 0) {
      g_source_remove(pty_watch_id);
      pty_watch_id = 0;
   }

   if (!pty_hup_logged) {
      Log(LOG_DEBUG, "cat.pty", "PTY slave closed (HUP), re-arming in 2s");
      pty_hup_logged = true;
   }
   g_timeout_add(2000, cat_pty_rearm, NULL);
}

static gboolean cat_pty_rearm(gpointer user) {
   if (pty_master < 0 || pty_watch_id != 0 || !pty_chan) {
      return G_SOURCE_REMOVE;
   }

   pty_watch_id = g_io_add_watch(pty_chan, G_IO_IN | G_IO_HUP | G_IO_ERR, cat_pty_readable, NULL);
   return G_SOURCE_REMOVE;
}

static gboolean cat_pty_readable(GIOChannel *chan, GIOCondition cond, gpointer user) {
   if (pty_master < 0) {
      return G_SOURCE_REMOVE;
   }

   if (cond & (G_IO_HUP | G_IO_ERR)) {
      // Last slave writer closed; stop watching and re-arm shortly
      cat_pty_sleep();
      return G_SOURCE_REMOVE;
   }

   char buf[256];

   for (;;) {
      ssize_t r = read(pty_master, buf, sizeof(buf));

      if (r > 0) {
         pty_hup_logged = false;   // slave is active again
         cat_pty_feed(buf, (size_t)r);
         continue;
      }
      if (r < 0 && (errno == EAGAIN || errno == EINTR)) {
         break;   // drained what's available; wait for the next wakeup
      }
      // r == 0 or a real error (EIO once the last slave fd is closed).  The
      // master then stays PERMANENTLY readable, so we must detach the watch
      // here too or the main loop spins at 100% CPU.  Same recovery as HUP.
      cat_pty_sleep();
      return G_SOURCE_REMOVE;
   }

   // EAGAIN: no more data right now. Keep the watch armed.
   return G_SOURCE_CONTINUE;
}

static void cat_pty_set_raw(int fd) {
   struct termios tio;

   if (tcgetattr(fd, &tio) < 0) {
      return;
   }

   cfmakeraw(&tio);
   // Preserve sane defaults for those; hamlib sets what it wants on the slave
   tio.c_cflag |= CLOCAL | CREAD;
   tio.c_cc[VMIN] = 1;
   tio.c_cc[VTIME] = 0;
   tcsetattr(fd, TCSANOW, &tio);
}

void cat_pty_shutdown(void) {
   if (pty_watch_id != 0) {
      g_source_remove(pty_watch_id);
      pty_watch_id = 0;
   }

   if (pty_chan) {
      g_io_channel_unref(pty_chan);
      pty_chan = NULL;
   }

   if (pty_master >= 0) {
      close(pty_master);
      pty_master = -1;
   }

   if (pty_link[0]) {
      unlink(pty_link);
      pty_link[0] = '\0';
   }

   pty_buf_len = 0;
   Log(LOG_INFO, "cat.pty", "CAT PTY shut down");
}

bool cat_pty_init(void) {
   if (!cfg_get_bool("cat.pty.enable", true)) {
      return true;
   }

   const char *path = cfg_get_exp("cat.pty.path");

   if (!path || !path[0]) {
      path = "./dev/ttyCAT0";
   }

   // Create ./dev (or whatever parent dir the path names) if needed
   char dir[256];
   snprintf(dir, sizeof(dir), "%s", path);
   char *slash = strrchr(dir, '/');

   if (slash && slash != dir) {
      *slash = '\0';
      if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
         Log(LOG_CRIT, "cat.pty", "Can't create %s: %d (%s)", dir, errno, strerror(errno));
         return false;
      }
   }

   int m = posix_openpt(O_RDWR | O_NOCTTY);

   if (m < 0) {
      Log(LOG_CRIT, "cat.pty", "posix_openpt() failed: %d (%s)", errno, strerror(errno));
      return false;
   }

   if (grantpt(m) < 0 || unlockpt(m) < 0) {
      Log(LOG_CRIT, "cat.pty", "grantpt/unlockpt failed: %d (%s)", errno, strerror(errno));
      close(m);
      return false;
   }

   if (ptsname_r(m, pty_slave, sizeof(pty_slave)) != 0) {
      Log(LOG_CRIT, "cat.pty", "ptsname_r() failed: %d (%s)", errno, strerror(errno));
      close(m);
      return false;
   }

   // Non-blocking master so reads never stall the main loop
   int flags = fcntl(m, F_GETFL, 0);

   if (flags >= 0) {
      fcntl(m, F_SETFL, flags | O_NONBLOCK);
   }

   // Set the slave raw so readers get exactly what we write
   int sfd = open(pty_slave, O_RDWR | O_NOCTTY);

   if (sfd >= 0) {
      cat_pty_set_raw(sfd);
      close(sfd);
   }

   // Expose the slave at the configured path as a symlink
   snprintf(pty_link, sizeof(pty_link), "%s", path);
   unlink(pty_link);

   if (symlink(pty_slave, pty_link) < 0) {
      Log(LOG_CRIT, "cat.pty", "symlink(%s => %s) failed: %d (%s)",
         pty_link, pty_slave, errno, strerror(errno));
      close(m);
      pty_link[0] = '\0';
      return false;
   }

   // Hook the master into the glib main loop. Keep our own reference in
   // pty_chan: when the watch is removed on HUP we still need the channel
   // around to re-arm it later.
   pty_chan = g_io_channel_unix_new(m);

   pty_watch_id = g_io_add_watch(pty_chan, G_IO_IN | G_IO_HUP | G_IO_ERR, cat_pty_readable, NULL);

   pty_master = m;
   pty_buf_len = 0;

   Log(LOG_INFO, "cat.pty", "CAT PTY up: %s => %s", pty_link, pty_slave);
   return true;
}
