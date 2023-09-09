#if	!defined(_eeprom_h)
#define	_eeprom_h

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "state.h"
#include "eeprom_layout.h"

extern int eeprom_init(void);
extern int eeprom_write(size_t offset, u_int8_t data);
extern u_int8_t eeprom_read(size_t offset);
extern int eeprom_read_block(u_int8_t *buf, size_t offset, size_t len);
extern int eeprom_write_block(void *buf, size_t offset, size_t len);
extern int eeprom_load_config(void);
extern int eeprom_write_config(int force);
extern int get_serial_number(void);

#endif	// !defined(_eeprom_h)
