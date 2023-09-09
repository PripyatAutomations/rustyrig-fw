/*
 * This contains stuff for when we live on a posix host
 *
 * Namely we use optionally use pipes instead of real serial ports
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "state.h"
#include "parser.h"
#include "posix.h"

extern struct GlobalState rig;	// Global state
