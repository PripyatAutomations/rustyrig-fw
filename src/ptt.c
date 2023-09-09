/*
 * Handle PTT and all interlocks preventing it's use
 *
 * we also deal with the PA_INHIBIT lines which allow momentarily stopping RF output without
 * powering down the PAs (such as for relay changes in tuning or filters).
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "state.h"
#include "parser.h"
extern struct GlobalState rig;	// Global state

