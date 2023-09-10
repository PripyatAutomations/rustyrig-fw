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
extern struct GlobalState rig;	// Global state

int set_fault(int fault) {
    rig.faults++;

    if (fault > rig.fault_code) {
       Log(CRIT, "Fault event [%d] code %d set exceeds last (%d), raising fault level!", rig.faults, fault, rig.fault_code);
       rig.fault_code = fault;
    } else {
       Log(WARN, "Fault event [%] code is lower priority than last (%d), not raising fault level!", rig.faults, fault, rig.fault_code);
    }

    // XXX: Update the display
    return 0;
}
