#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "inc/logger.h"
#include "inc/http.h"
#include "inc/ws.h"
#include "inc/posix.h"

#define	RR_HOST		"chiraq.istabpeople.com:4420"

struct mg_mgr mg_mgr;

int my_argc = -1;
char **my_argv = NULL;
bool dying = 0;                 // Are we shutting down?
bool restarting = 0;            // Are we restarting?
time_t now = -1;                // time() called once a second in main loop to update
char latest_timestamp[64];      // Current printed timestamp

void shutdown_app(void) {
   //
}

int main(int argc, char **argv) {
   // save for restarting later
   my_argc = argc;
   my_argv = argv;

   // Initialize some earl state
   now = time(NULL);
   srand((unsigned int)now);
   logfp = stdout;
   debug_init();			// Initialize debug	
   mg_mgr_init(&mg_mgr);
//   timer_init();
//   gui_init();
   logger_init();

// Is mongoose http server enabled?
// Is extra mongoose debugging enabled?
#if	defined(MONGOOSE_DEBUG)
   mg_log_set(MG_LL_DEBUG);
#endif
//   http_init(&mg_mgr);
//   ws_init(&mg_mgr);
   Log(LOG_INFO, "core", "Client initialization completed. Enjoy!");

#define	PARSE_LINE_LEN	64

   // Main loop
   while(1) {
      now = time(NULL);

      char buf[512];
#if	defined(CAT_YAESU)
      memset(buf, 0, PARSE_LINE_LEN);
      // io_read(&cat_io, &buf, PARSE_LINE_LEN - 1);
//      rr_cat_parse_line(buf);
#endif
#if	defined(CAT_KPA500)
//      memset(buf, 0, PARSE_LINE_LEN);
//      io_read(&cons_io, &buf, PARSE_LINE_LEN - 1);
//        rr_cat_parse_amp_line(buf);
#endif
//      memset(buf, 0, PARSE_LINE_LEN);
//      io_read(&cons_io, &buf, PARSE_LINE_LEN - 1);
//      rr_cons_parse_line(buf);

      // Redraw the GUI virtual framebuffer, update nextion
//      gui_update();

      // Process Mongoose HTTP and MQTT events, this should be at the end of loop so all data is ready
      mg_mgr_poll(&mg_mgr, 1000);

      if (dying) {
         break;
      }
   }
   host_cleanup();

   mg_mgr_free(&mg_mgr);

   if (restarting) {
//       restart_rig();
   }
   
   return 0;
}
