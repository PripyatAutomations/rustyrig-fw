#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "cat_control.h"
#include "posix.h"

bool dying = 0;
struct GlobalState rig;	// Global state

static int load_defaults(void) {
   // Set minimum defaults, til we have EEPROM available
   rig.faultbeep = 1;
   rig.bc_standby = 1;
   rig.tr_delay = 50;
   return 0;
}   

// Zeroize our memory structures
static int initialize_state(void) {
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

void shutdown_rig(int signum) {
    if (signum >= 0) {
       Log(CRIT, "Shutting down by signal %d", signum);
    } else {
       Log(CRIT, "Shutting down due to internal error: %d", -signum);
    }

    host_cleanup();
    exit(signum);
}

int main(int argc, char **argv) {
   // On the stm32, main is not our entry point, so we can use this to help catch misbuilt images.
#if	!defined(HOST_POSIX)
   printf("This build is intended to be a firmware image for the radio, not run on a host PC. The fact that it even runs means your build environment is severely broken!\n");
   exit(1);
#else
   init_signals();
#endif

   // Initialize subsystems
   logger_init();
   Log(INFO, "Radio firmware v%s starting...", VERSION);
   get_serial_number();
   initialize_state();			// Load default values

   // Initialize the i2c buses, if present
   i2c_init(0, MY_I2C_ADDR);
   i2c_init(1, MY_I2C_ADDR);

   // if able to connect to EEPROM, load and apply settings
   if (eeprom_init() == 0) {
      eeprom_load_config();
   }

   cat_init();
   Log(INFO, "Radio initialization completed. Enjoy!");

   // Main loop
   while(!dying) {
      char buf[512];
      memset(buf, 0, 512);
      // read_serial(&buf, 512);
      cat_parse_line(buf);
      sleep(1);
   }

   return 0;
}
