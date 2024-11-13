/*
 * Here we implement the Yaesu 891/991a style CAT protocol for control of the rig
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
#include "cat.h"
#include "cat_yaesu.h"
#include "cat_control.h"
#include "state.h"

extern struct GlobalState rig;	// Global state

#if	defined(CAT_YAESU)

struct cat_cmd cmd_yaesu[] = {
   // Commands for Yaesu mode
};


#endif
