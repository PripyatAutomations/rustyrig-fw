//
// state.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// This file contains the structures used for statistics and state
#if     !defined(__rr_state_h)
#define __rr_state_h
#include <time.h>
#include <stdbool.h>
#include "build_config.h"
#include <librustyaxe/cat.h>

#define PARSE_LINE_LEN 512

// State of the all tunings: PA & Matching Units
enum TuningState {
   TS_UNKNOWN = 0,
   TS_TUNING,
   TS_TUNE_FAILED,
   TS_TX_READY
};

extern void shutdown_rig(uint32_t signum);       // main.c
extern void restart_rig(void);

////////////////////
extern int my_argc;              // in main.c
extern char **my_argv;           // in main.c
extern bool dying;               // in main.c
extern bool restarting;          // in main.c
extern time_t now;               // in main.c
extern struct timespec last_rig_poll;    // in main.c
extern struct timespec loop_start;

#endif // !defined(__rr_state_h)
