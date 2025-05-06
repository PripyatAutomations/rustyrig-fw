#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "amp.h"
#include "atu.h"
#include "au.h"
#include "backend.h"
#include "cat.h"
#include "eeprom.h"
#include "faults.h"
#include "filters.h"
#include "gpio.h"
#include "gui.h"
#include "help.h"
#include "i2c.h"
#include "io.h"
#include "logger.h"
#include "network.h"
#include "posix.h"
#include "ptt.h"
#include "state.h"
#include "thermal.h"
#include "timer.h"
#include "usb.h"
#include "codec.h"
#include "dds.h"

//
// http ui support
//
#if	defined(FEATURE_HTTP)
#include "http.h"
#include "ws.h"
#endif
#if	defined(FEATURE_MQTT)
#include "mqtt.h"
#endif
#if	defined(USE_MONGOOSE)
struct mg_mgr mg_mgr;
#endif

#define	TS_ALPHA	0.1	// Weight for the moving average
int my_argc = -1;
char **my_argv = NULL;
bool dying = 0;                 // Are we shutting down?
struct GlobalState rig;         // Global state
time_t now = -1;		// time() called once a second in main loop to update
char latest_timestamp[64];	// Current printed timestamp
int auto_block_ptt = 0;		// Auto block PTT at boot?

// Set minimum defaults, til we have EEPROM available
static uint32_t load_defaults(void) {
   rig.faultbeep = 1;
   rig.bc_standby = 1;
   rig.tr_delay = 50;
   return 0;
}

// Zeroize our memory structures
static uint32_t initialize_state(void) {
   memset(&rig, 0, sizeof(struct GlobalState));

   for (int i = 0; i < RR_MAX_AMPS; i++) {
       memset(&rig.amps[i], 0, sizeof(struct AmpState));
   }

   for (int i = 0; i < RR_MAX_ATUS; i++) {
      memset(&rig.atus[i], 0, sizeof(struct ATUState));
   }

   for (int i = 0; i < RR_MAX_FILTERS; i++) {
      memset(&rig.filters[i], 0, sizeof(struct FilterState));
   }

   load_defaults();
   return 0;
}

void shutdown_rig(uint32_t signum) {
    if (signum >= 0) {
       Log(LOG_CRIT, "core", "Shutting down by signal %d", signum);
    } else {
       Log(LOG_CRIT, "core", "Shutting down due to internal error: %d", -signum);
    }

    dying = 1;
}

void restart_rig(void) {
   Log(LOG_CRIT, "core", "Restarting process...");

   host_cleanup();

   // Ensure argv is NULL-terminated (it should be, but defensively...)
   my_argv[my_argc] = NULL;

   execv(my_argv[0], my_argv);

   // If execv fails
   perror("execv");
   exit(127);
}

int main(int argc, char **argv) {
   // save for restarting later
   my_argc = argc;
   my_argv = argv;

   // loop time calculation
#if	defined(USE_PROFILING)
   struct timespec loop_start = { .tv_sec = 0, .tv_nsec = 0 };
   struct timespec loop_end = { .tv_sec = 0, .tv_nsec = 0 };
   double loop_runtime = 0.0, current_time;
#endif // defined(USE_PROFILING)

   // Initialize some earl state
   now = time(NULL);
   srand((unsigned int)now);
   logfp = stdout;
   rig.log_level = LOG_DEBUG;	// startup in debug mode
   host_init();

   Log(LOG_INFO, "core", "rustyrig radio firmware v%s starting...", VERSION);
   debug_init();			// Initialize debug	
   initialize_state();			// Load default values

#if	defined(USE_MONGOOSE)
   mg_mgr_init(&mg_mgr);
#endif
   timer_init();
   gpio_init();

   // if able to connect to EEPROM, load and apply settings
   if (eeprom_init() == 0) {
      eeprom_load_config();
   }

//   i2c_init();
   gui_init();
   logger_init();

   // Print the serial #
   rig.serial = get_serial_number();
   Log(LOG_INFO, "core", "Device serial number: %lu", rig.serial);

   // Initialize add-in cards
   // XXX: This should be done by enumerating the bus eventually
   filter_init_all();
   rr_amp_init_all();
   rr_atu_init_all();

   // apply some configuration from the eeprom
   auto_block_ptt = eeprom_get_bool("features/auto_block_ptt");

   if (auto_block_ptt) {
      Log(LOG_INFO, "core", "*** Enabling PTT block at startup - change features/auto-block-ptt to false to disable ***");
      rr_ptt_set_blocked(true);
   }

   if (rr_io_init()) {
      Log(LOG_CRIT, "core", "*** Fatal error init i/o subsys ***");
//      fatal_error();
      exit(1);
   }

   if (rr_backend_init()) {
      Log(LOG_CRIT, "core", "*** Failed init backend ***");
//      fatal_error();
      exit(1);
   }

   if (rr_cat_init()) {
      Log(LOG_CRIT, "core", "*** Fatal error CAT ***");
//      fatal_error();
      exit(1);
   }

   rr_au_init();
   codec_init();
   dds_init();

   // Network connectivity
   show_network_info();
   show_pin_info();

// Is mongoose http server enabled?
#if	defined(FEATURE_HTTP)
// Is extra mongoose debugging enabled?
#if	defined(MONGOOSE_DEBUG)
   mg_log_set(MG_LL_DEBUG);
#endif
   http_init(&mg_mgr);
   ws_init(&mg_mgr);
#endif
#if	defined(FEATURE_MQTT)
   mqtt_init(&mg_mgr);
#endif
   Log(LOG_INFO, "core", "Radio initialization completed. Enjoy!");

   // Main loop
   while(!dying) {
#if	defined(USE_PROFILING)
      clock_gettime(CLOCK_MONOTONIC, &loop_start);
#endif // defined(USE_PROFILING)
      now = time(NULL);

      char buf[512];

      // Check faults
      if (check_faults()) {
         Log(LOG_CRIT, "core", "Fault detected, see crash dump above");
      }

      // Check thermals
      if (are_we_on_fire()) {
         rr_ptt_set_all_off();
         rr_ptt_set_blocked(true);
         Log(LOG_CRIT, "core", "Radio is on fire?! Halted TX!");
      }

      rr_au_pw_runloop_all();

      // XXX: we need to pass io structs
      /// XXX: Determine which (pipes|devices|sockets) are needing read from
      // XXX: Iterate over them: console, amp, rig
      // We limit line length to 512
#if	defined(CAT_YAESU)
      memset(buf, 0, PARSE_LINE_LEN);
      // io_read(&cat_io, &buf, PARSE_LINE_LEN - 1);
      rr_cat_parse_line(buf);
#endif
#if	defined(CAT_KPA500)
//      memset(buf, 0, PARSE_LINE_LEN);
//      io_read(&cons_io, &buf, PARSE_LINE_LEN - 1);
        rr_cat_parse_amp_line(buf);
#endif
//      memset(buf, 0, PARSE_LINE_LEN);
//      io_read(&cons_io, &buf, PARSE_LINE_LEN - 1);
//      rr_cons_parse_line(buf);

      // Redraw the GUI virtual framebuffer, update nextion
      gui_update();

      // Send pings, drop dead connections, etc
//      http_expire_sessions();

      // XXX: Check if an LCD/OLED is configured and update it
      // XXX: Check if any mjpeg subscribers exist and prepare a frame for them

#if	defined(USE_MONGOOSE)
      // Process Mongoose HTTP and MQTT events, this should be at the end of loop so all data is ready
      mg_mgr_poll(&mg_mgr, 1000);
#endif

      // If enabled, calculate loop run time
#if	defined(USE_PROFILING)
      clock_gettime(CLOCK_MONOTONIC, &loop_end);
      current_time = (loop_end.tv_sec - loop_start.tv_sec) + 
                     (loop_end.tv_nsec - loop_start.tv_nsec) / 1e9;

      if (loop_runtime == 0.0)
         loop_runtime = current_time;
      else
         loop_runtime = (TS_ALPHA * current_time) + (1 - TS_ALPHA) * loop_runtime;
#endif // defined(USE_PROFILING)
   }

#if	defined(USE_PROFILING)
   // XXX: Every 5 minutes we should save the loop runtime
//   Log(LOG_INFO, "loop", "Average mainloop runtime: %.6f seconds", loop_runtime);
#endif // defined(USE_PROFILING)
   host_cleanup();

#if	defined(USE_MONGOOSE)
   mg_mgr_free(&mg_mgr);
#endif
   return 0;
}
