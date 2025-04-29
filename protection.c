//
// Here we deal with SWR protection
//
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
#include "protection.h"

// Warmup protection
static bool rr_protect_warmup_pending(int amp_idx) {
   if (amp_idx < 0) {
      return false;
   }

   // Lookup warmup-required and warmup-time for the passed amp

   // Return no, no warmup needed
   return false;
}
