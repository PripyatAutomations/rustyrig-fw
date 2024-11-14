#if	!defined(_eeprom_h)
#define	_eeprom_h

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "state.h"
#include "eeprom_types.h"

struct eeprom_layout {
    char 		*key;
    size_t		offset;		// Offset into rom
    size_t 		size;		// Size in bytes
    ee_data_type 	type;
};

// Support for deal with eeprom by name instead of addresses
extern uint32_t eeprom_offset_index(const char *key);

// Direct (by address) reading of one or more bytes
// These should only be used internally...
extern void *eeprom_read(size_t offset);
extern uint32_t eeprom_write(size_t offset, u_int8_t data);
extern uint32_t eeprom_read_block(u_int8_t *buf, size_t offset, size_t len);
extern uint32_t eeprom_write_block(void *buf, size_t offset, size_t len);

// This is the API intended to be used, to abstract out platform differences
extern uint32_t eeprom_init(void);
extern uint32_t eeprom_load_config(void);
extern uint32_t eeprom_write_config(uint32_t force);
extern uint32_t eeprom_get_int(uint32_t idx);
extern const char *eeprom_get_str(uint32_t idx);
extern uint32_t eeprom_get_int_i(const char *key);
extern const char *eeprom_get_str_i(const char *key);
extern char *get_serial_number(void);

#endif	// !defined(_eeprom_h)
