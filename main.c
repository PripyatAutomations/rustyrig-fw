#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "amp.h"
#include "atu.h"
#include "audio.h"
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
#if	defined(FEATURE_HTTP)
#include "http.h"
#include "websocket.h"
#endif
#if	defined(FEATURE_MQTT)
#include "mqtt.h"
#endif
#if	defined(FEATURE_MQTT) || defined(FEATURE_HTTP)
struct mg_mgr mg_mgr;
#endif

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

    host_cleanup();
    exit(signum);
}

int main(int argc, char **argv) {
   // Initialize some earl state
   now = time(NULL);
   srand((unsigned int)now);
   logfp = stdout;
   rig.log_level = LOG_DEBUG;	// startup in debug mode

   // On the stm32, main is not our entry point, so we can use this to help catch misbuilt images.
#if	!defined(HOST_POSIX)
   printf("This build is intended to be a firmware image for the radio, not run on a host PC. The fact that it even runs means your build environment is likely severely broken!\n");
   exit(1);
#else
   host_init();
#endif

   // Initialize subsystems
   Log(LOG_INFO, "core", "rustyrig radio firmware v%s starting...", VERSION);
   debug_init();
   initialize_state();			// Load default values

#if     defined(FEATURE_MQTT) || defined(FEATURE_HTTP)
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
      ptt_set_blocked(true);
   }

   if (io_init()) {
      Log(LOG_CRIT, "core", "*** Fatal error init i/o subsys ***");
//      fatal_error();
      exit(1);
   }

   if (backend_init()) {
      Log(LOG_CRIT, "core", "*** Failed init backend ***");
//      fatal_error();
      exit(1);
   }

   if (cat_init()) {
      Log(LOG_CRIT, "core", "*** Fatal error CAT ***");
//      fatal_error();
      exit(1);
   }

   rr_au_init();
   codec_init();

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
      now = time(NULL);
      char buf[512];

      // Check faults
      if (check_faults()) {
         Log(LOG_CRIT, "core", "Fault detected, see crash dump above");
      }

      // Check thermals
      if (are_we_on_fire()) {
         ptt_set_all_off();
         ptt_set_blocked(true);
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
      cat_parse_line(buf);
#endif
#if	defined(CAT_KPA500)
//      memset(buf, 0, PARSE_LINE_LEN);
//      io_read(&cons_io, &buf, PARSE_LINE_LEN - 1);
        cat_parse_amp_line(buf);
#endif
//      memset(buf, 0, PARSE_LINE_LEN);
//      io_read(&cons_io, &buf, PARSE_LINE_LEN - 1);
//      console_parse_line(buf);

      // Redraw the GUI virtual framebuffer
      gui_update();

      // XXX: Check if an LCD/OLED is configured and display it
      // XXX: Check if any mjpeg subscribers exist and prepare a frame for them

#if     defined(FEATURE_MQTT) || defined(FEATURE_HTTP)
      // Process Mongoose HTTP and MQTT events, this should be at the end of loop so all data is ready
      mg_mgr_poll(&mg_mgr, 1000);
#endif
   }

#if     defined(FEATURE_MQTT) || defined(FEATURE_HTTP)
   mg_mgr_free(&mg_mgr);
#endif
   return 0;
}
