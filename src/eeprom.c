#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include "config.h"
#include "state.h"
#include "parser.h"
extern struct GlobalState *rig;	// Global state

int eeprom_init(void) {
#if	defined(HOST_BUILD)
   int tmp = open(HOST_EEPROM_FILE, O_RDONLY);
   if (tmp == -1) {
      return -1;
   }
   rig->eeprom_fd  = tmp;
#else
#endif
   rig->eeprom_ready = 1;
   return 0;
}

int eeprom_read(void *buf, size_t offset, size_t len) {
#if	defined(HOST_BUILD)
#else
#endif
   return 0;
}

int eeprom_write(void *buf, size_t offset, size_t len) {
#if	defined(HOST_BUILD)
#else
#endif
   return 0;
}
