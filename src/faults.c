/*
 * Manage faults, both minor and major.
 *
 * Log them and disable TX, if needed
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "logger.h"
#include "state.h"
#include "faults.h"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

struct fault_table fault_table[] = {
   // FAULT		 	Fatal?
   { FAULT_NONE,	 	false },
   { FAULT_STUCK_RELAY,	 	true },
   { FAULT_INLET_THERMAL,	true },
   { FAULT_TOO_HOT,      	true },
   { FAULT_HIGH_SWR,	 	true },
   { FAULT_FINAL_THERMAL, 	true },
   { FAULT_FINAL_LOW_CURRENT, 	true },
   { FAULT_FINAL_HIGH_CURRENT, 	true },
   { FAULT_FINAL_LOW_VOLT, 	true },
   { FAULT_FINAL_HIGH_VOLT, 	true },
   { FAULT_TOT_TIMEOUT,	  	true },
   { FAULT_UNKNOWN,	 	true },
};

int fault_priority(uint32_t code) {
   // XXX: We need to look this up in the fault table
   return code;
}

const char *fault_get_type_str(uint32_t fault) {
   return NULL;
}

// All faults trigger an alarm light, but only some are fatal and will cause a shutdown
bool fault_is_fatal(uint32_t code) {
   return false;
}

uint32_t set_fault(uint32_t fault) {
   // count the fault
   rig.faults++;

   const char *fault_type = fault_get_type_str(fault);

   if (fault_priority(fault) > fault_priority(rig.fault_code)) {
      Log(LOG_CRIT, "FAULT: New fault %s is higher priority than last (%d > %d), raised fault level!", fault_type, fault, rig.fault_code);
      rig.fault_code = fault;
   }

   // XXX: Determine if shutting down the PA and output should occur

   // XXX: Update the display & set fault LED
   return 0;
}

// This should return true if operation halted, otherwise false for alarms
bool check_faults(void) {
   if (rig.fault_code != 0) {
      // XXX: We should check if fatal or alarm
      if (fault_is_fatal(rig.fault_code)) {
         Log(LOG_CRIT, "Fault [%d] has occurred and we cannot continue! Halting to prevent damage! Total faults: %d", rig.fault_code, rig.faults);
//         protection_lockout("FAULT");
         return true;
      }
   }

   return false;
}
