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

// Fault Table contains the known faults and their text strings
struct fault_table fault_table[] = {
   // FAULT		 	Fatal?		String
   { FAULT_NONE,	 	false,		"None" },
   { FAULT_STUCK_RELAY,	 	true,		"RELAY" },
   { FAULT_INLET_THERMAL,	true,		"THERM_IN" },
   { FAULT_TOO_HOT,      	true,		"THERM_BOX" },
   { FAULT_HIGH_SWR,	 	true,		"SWR" },
   { FAULT_FINAL_THERMAL, 	true,		"THERM_FIN" },
   { FAULT_FINAL_LOW_CURRENT, 	true,		"F. Lo Curr" },
   { FAULT_FINAL_HIGH_CURRENT, 	true,		"F. Hi Curr" },
   { FAULT_FINAL_LOW_VOLT, 	true,		"F. Lo Volt" },
   { FAULT_FINAL_HIGH_VOLT, 	true,		"F. Hi Volt" },
   { FAULT_TOT_TIMEOUT,	  	true,		"TIMEOUT" },
   { FAULT_WARMING_UP,		false,		"Warming Up" },
   { FAULT_UNKNOWN,	 	true,		"Unknown" },
};

int fault_priority(uint32_t code) {
   // XXX: We need to look this up in the fault table and figure out the priority
   int items = (sizeof(fault_table) / sizeof(struct fault_table));
   if (items > 0) {
      for (int i = 0; i < items; i++) {
         if (fault_table[i].code == code) {
            return fault_table[i].code;
         }
      }
   }
   return code;
}

const char *fault_get_type_str(uint32_t code) {
   int items = (sizeof(fault_table) / sizeof(struct fault_table));
   if (items > 0) {
      for (int i = 0; i < items; i++) {
         if (fault_table[i].code == code) {
            return fault_table[i].msg;
         }
      }
   }
   return NULL;
}

// All faults trigger an alarm light, but only some are fatal and will cause a shutdown
bool fault_is_fatal(uint32_t code) {
   int items = (sizeof(fault_table) / sizeof(struct fault_table));
   if (items > 0) {
      for (int i = 0; i < items; i++) {
         if (fault_table[i].code == code) {
            return fault_table[i].fatal;
         }
      }
   }
   return false;
}

uint32_t set_fault(uint32_t fault) {
   // count the fault
   rig.faults++;

   const char *fault_type = fault_get_type_str(fault);

   if (fault_priority(fault) > fault_priority(rig.fault_code)) {
      Log(LOG_CRIT, "faults", "FAULT: New fault %s is higher priority than last (%d > %d), raised fault level!", fault_type, fault, rig.fault_code);
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
         Log(LOG_CRIT, "faults", "Fault [%d] has occurred and we cannot continue! Halting to prevent damage! Total faults: %d", rig.fault_code, rig.faults);
//         protection_lockout("FAULT");
         return true;
      }
   }

   return false;
}
