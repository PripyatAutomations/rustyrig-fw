#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "config.h"
#include "state.h"
#include "logger.h"
#include "parser.h"

struct GlobalState rig;	// Global state

int main(int argc, char **argv) {
#if	!defined(HOST_BUILD)
   printf("This program is intended to be a firmware image for the radio, not run on a host PC. The fact that it even runs means your build environment is severely broken!\n");
   exit(1);
#endif
   logger_init();
   Log(INFO, "Radio firmware v%s starting...", VERSION);
   Log(INFO, "Ready to transmit!");
   return 0;
}
