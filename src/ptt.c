/*
 * Handle PTT and all interlocks preventing it's use
 *
 * we also deal with the PA_INHIBIT lines which allow momentarily stopping RF output without
 * powering down the PAs (such as for relay changes in tuning or filters).
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "state.h"
#include "ptt.h"
extern struct GlobalState rig;	// Global state

bool ptt_check_blocked(void) {
    if (rig.tx_blocked) {
       return true;
     }
     return false;
}

bool ptt_set_blocked(bool blocked) {
   rig.tx_blocked = blocked;
   return blocked;
}
