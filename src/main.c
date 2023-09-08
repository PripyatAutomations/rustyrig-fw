#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "parser.h"

bool dying = 0;
struct GlobalState rig;	// Global state

// Zeroize our memory structures
static int initialize_state(void) {
   memset(&rig, 0, sizeof(struct GlobalState));
   memset(&rig.low_amp, 0, sizeof(struct AmpState));
   memset(&rig.high_amp, 0, sizeof(struct AmpState));
   memset(&rig.low_atu, 0, sizeof(struct ATUState));
   memset(&rig.high_atu, 0, sizeof(struct ATUState));
   memset(&rig.low_filters, 0, sizeof(struct FilterState));
   memset(&rig.high_filters, 0, sizeof(struct FilterState));

   return 0;
}

static int load_defaults(void) {
   // Set minimum defaults, til we have EEPROM available
   rig.faultbeep = 1;
   rig.bc_standby = 1;
   rig.tr_delay = 50;
   return 0;
}   

int main(int argc, char **argv) {
   // On the stm32, main is not our entry point, so we can use this
   // to help catch misbuilt images.
#if	!defined(HOST_BUILD)
   printf("This program is intended to be a firmware image for the radio, not run on a host PC. The fact that it even runs means your build environment is severely broken!\n");
   exit(1);
#endif

   // Initialize subsystems
   logger_init();
   Log(INFO, "Radio firmware v%s starting...", VERSION);
   initialize_state();			// Load default values

   // Initialize the i2c buses, if present
   i2c_init(0, MY_I2C_ADDR);
   i2c_init(1, MY_I2C_ADDR);

   // if able to connect to EEPROM, load and apply settings
   if (eeprom_init() == 0) {
      eeprom_load_config();
   } else { // Load defaults
      load_defaults();
   }

   cat_init();
   Log(INFO, "Ready to operate!");

   // Main loop
   while(!dying) {
      char buf[512];
      memset(buf, 0, 512);
      cat_parse_line(buf);
      sleep(1);
   }

   return 0;
}
