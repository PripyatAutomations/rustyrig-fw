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
#include "thermal.h"
#include "ant_tuner.h"
#include "cat.h"
#include "ptt.h"
#include "posix.h"

bool dying = 0;		// Are we shutting down?
struct GlobalState rig;	// Global state

// Current time, must be updated ONCE per second, used to save calls to gettimeofday()
time_t now = -1;
// Current timestamp
char latest_timestamp[64];
// last time we updated the timestamp by calling update_timestamp
time_t last_ts_update = -1;

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
   // On the stm32, main is not our entry point, so we can use this to help catch misbuilt images.
#if	!defined(HOST_POSIX)
   printf("This build is intended to be a firmware image for the radio, not run on a host PC. The fact that it even runs means your build environment is severely broken!\n");
   exit(1);
#else
   init_signals();
#endif

   // Initialize subsystems
   logger_init();
   Log(LOG_INFO, "Radio firmware v%s starting...", VERSION);
   initialize_state();			// Load default values

   // if able to connect to EEPROM, load and apply settings
   if (eeprom_init() == 0) {
      eeprom_load_config();
   }
   get_serial_number();

   // Initialize the i2c buses, if present (starts at 1) 
   I2CBus i2c_bus1 = { .bus_id = 1 };
   I2CBus i2c_bus2 = { .bus_id = 2 };

   if (i2c_init(&i2c_bus1, MY_I2C_ADDR) &&
      i2c_init(&i2c_bus2, MY_I2C_ADDR)) {
      uint8_t data[] = { 0x01, 0x02 };
      i2c_write(&i2c_bus1, 0x50, data, sizeof(data));
      uint8_t buffer[10];
      i2c_read(&i2c_bus2, 0x60, buffer, sizeof(buffer));
   } else {
      Log(LOG_CRIT, "Error initializing i2c");
   }

   atu_init();
   cat_init();

   Log(LOG_INFO, "Radio initialization completed. Enjoy!");

   // Main loop
   while(!dying) {
      char buf[512];
      
      if (are_we_on_fire()) {
         ptt_set(false);
         ptt_set_blocked(true);
         Log(LOG_CRIT, "Radio is on fire?! Halted TX!\n");
      }

      memset(buf, 0, 512);
      // read_serial(&buf, 512);
      cat_parse_line(buf);
      sleep(1);
   }

   return 0;
}
