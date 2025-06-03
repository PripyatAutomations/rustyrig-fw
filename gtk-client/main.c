#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "inc/logger.h"
#include "inc/dict.h"
#include "inc/posix.h"
#include "inc/mongoose.h"
#include "inc/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

// config.c
extern bool config_load(const char *path);
extern dict *cfg;

int my_argc = -1;
char **my_argv = NULL;
bool dying = 0;                 // Are we shutting down?
bool restarting = 0;            // Are we restarting?
time_t now = -1;                // time() called once a second in main loop to update
bool ptt_active = false;

void shutdown_app(int signum) {
   Log(LOG_INFO, "core", "Shutting down %s%d", (signum > 0 ? "with signal " : ""), signum);
}

int main(int argc, char *argv[]) {
   logfp = stdout;
   log_level = LOG_DEBUG;

   if (config_load("config/rrclient.cfg")) {
      exit(1);
   }

   ws_init();
   gtk_init(&argc, &argv);
   gui_init();
   gtk_main();

   ws_fini();
   return 0;
}
