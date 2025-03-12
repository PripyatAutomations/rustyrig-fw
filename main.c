#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "posix.h"
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "thermal.h"
#include "atu.h"
#include "amp.h"
#include "cat.h"
#include "ptt.h"
#include "gpio.h"
#include "network.h"
#include "help.h"
#include "faults.h"
#include "filters.h"
#include "gui.h"
#include "io.h"
#include "audio.h"
#include "usb.h"
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

bool dying = 0;		// Are we shutting down?
struct GlobalState rig;	// Global state

// Current time, must be updated ONCE per second, used to save calls to gettimeofday()
time_t now = -1;
// Current timestamp
char latest_timestamp[64];

// should we automatically block PTT at startup?
int auto_block_ptt = 0;

static uint32_t load_defaults(void) {
   // Set minimum defaults, til we have EEPROM available
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
   gpio_init();

   // if able to connect to EEPROM, load and apply settings
   if (eeprom_init() == 0) {
      eeprom_load_config();
   }

   // i2c init
//   i2c_init();

   // GUI init
   gui_init();

   logger_init();

   // Print the serial #
   rig.serial = get_serial_number();
   Log(LOG_INFO, "core", "Device serial number: %lu", rig.serial);

   // Initialize add-in cards
   // XXX: This should be done by enumerating the bus eventually
   filter_init_all();
   amp_init_all();
   atu_init_all();

   // Network connectivity
   show_network_info();
   show_pin_info();

#if	defined(FEATURE_HTTP)
   http_init(&mg_mgr);
   ws_init(&mg_mgr);
#endif
#if	defined(FEATURE_MQTT)
   mqtt_init(&mg_mgr);
#endif

   // apply some configuration from the eeprom
   auto_block_ptt = eeprom_get_bool("features/auto-block-ptt");

   if (auto_block_ptt) {
      Log(LOG_INFO, "core", "*** Enabling PTT block at startup - change features/auto-block-ptt to false to disable ***");
      ptt_set_blocked(true);
   }

   if (io_init()) {
      Log(LOG_CRIT, "core", "*** Fatal error init i/o subsys ***");
//      fatal_error();
   }

   if (cat_init()) {
      Log(LOG_CRIT, "core", "*** Fatal error CAT ***");
//      fatal_error();
   }

   audio_init();

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
         ptt_set(false);
         ptt_set_blocked(true);
         Log(LOG_CRIT, "core", "Radio is on fire?! Halted TX!\n");
      }

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
      // Process Mongoose HTTP and MQTT events, this should be here because the mjpeg could be queried
      mg_mgr_poll(&mg_mgr, 1000);
#endif
   }

#if     defined(FEATURE_MQTT) || defined(FEATURE_HTTP)
   mg_mgr_free(&mg_mgr);
#endif
   return 0;
}
