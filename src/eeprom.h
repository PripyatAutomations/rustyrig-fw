#if	!defined(_eeprom_h)
#define	_eeprom_h

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "config.h"
#include "state.h"
#include "parser.h"
extern struct GlobalState *rig;	// Global state

extern int eeprom_init(void);
extern int eeprom_read(void *buf, size_t offset, size_t len);
extern int eeprom_write(void *buf, size_t offset, size_t len);

#endif	// !defined(_eeprom_h)
