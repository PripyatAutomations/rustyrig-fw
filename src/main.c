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
   memset(&rig.low_amp, 0, sizeof(struct AmpState));
   memset(&rig.high_amp, 0, sizeof(struct AmpState));
   memset(&rig.low_atu, 0, sizeof(struct ATUState));
   memset(&rig.high_atu, 0, sizeof(struct ATUState));
   memset(&rig.low_filters, 0, sizeof(struct FilterState));
   memset(&rig.high_filters, 0, sizeof(struct FilterState));

   load_defaults();
   return 0;
}

void shutdown_rig(uint32_t signum) {
    if (signum >= 0) {
       Log(LOG_CRIT, "Shutting down by signal %d", signum);
    } else {
       Log(LOG_CRIT, "Shutting down due to internal error: %d", -signum);
    }

    host_cleanup();
    exit(signum);
}

int main(int argc, char **argv) {
   now = time(NULL);
   update_timestamp();
   logfp = stdout;
   // On the stm32, main is not our entry point, so we can use this to help catch misbuilt images.
#if	!defined(HOST_POSIX)
   printf("This build is intended to be a firmware image for the radio, not run on a host PC. The fact that it even runs means your build environment is likely severely broken!\n");
   exit(1);
#else
   host_init();
#endif

   // Initialize subsystems
   Log(LOG_INFO, "rustyrig radio firmware v%s starting...", VERSION);
   initialize_state();			// Load default values

   gpio_init();

   // if able to connect to EEPROM, load and apply settings
   if (eeprom_init() == 0) {
      eeprom_load_config();
   }

   logger_init();

   // Print the serial #
   rig.serial = get_serial_number();
   Log(LOG_INFO, "Device serial number: %lu", rig.serial);

   atu_init_all();
   filter_init_all();
   amp_init_all();

   // Network connectivity
   show_network_info();
   show_pin_info();

   // Start up CAT interfaces
//   io_init();
   cat_init();
   auto_block_ptt = eeprom_get_bool("features/auto-block-ptt");

   if (auto_block_ptt) {
      Log(LOG_INFO, "*** Enabling PTT block at startup - change features/auto-block-ptt to false to disable ***");
      ptt_set_blocked(true);
   }

   Log(LOG_INFO, "Radio initialization completed. Enjoy!");

   // Main loop
   while(!dying) {
      now = time(NULL);
      update_timestamp();

      char buf[512];

      // Check faults
      if (check_faults()) {
         Log(LOG_CRIT, "Fault detected, see crash dump above");
      }

      // Check thermals
      if (are_we_on_fire()) {
         ptt_set(false);
         ptt_set_blocked(true);
         Log(LOG_CRIT, "Radio is on fire?! Halted TX!\n");
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
      sleep(1);
   }

   return 0;
}
