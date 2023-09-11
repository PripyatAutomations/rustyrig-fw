#if	!defined(_eeprom_h)
#define	_eeprom_h

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "state.h"

struct eeprom_layout {
    char *key;
    size_t offset;		// Offset into rom
    size_t size;		// Size in bytes
    enum {			// Type of the data
       EE_NONE = 0,
       EE_BYTES,		// raw bytes
       EE_STRING,		// string
       EE_INTEGER,		// numeric
       EE_FLOAT,		// floating point
       EE_IP4,			// ipv4 address
       EE_LCLASS,		// license class
    } type;
};

// Support for deal with eeprom by name instead of addresses
extern int eeprom_offset_index(const char *key);

// Direct (by address) reading of one or more bytes
// These should only be used internally...
extern u_int8_t eeprom_read(size_t offset);
extern int eeprom_write(size_t offset, u_int8_t data);
extern int eeprom_read_block(u_int8_t *buf, size_t offset, size_t len);
extern int eeprom_write_block(void *buf, size_t offset, size_t len);

// This is the API intended to be used, to abstract out platform differences
extern int eeprom_init(void);
extern int eeprom_load_config(void);
extern int eeprom_write_config(int force);
extern int eeprom_get_int(int idx);
extern const char *eeprom_get_str(int idx);
extern int eeprom_get_int_i(const char *key);
extern const char *eeprom_get_str_i(const char *key);
extern char *get_serial_number(void);

#endif	// !defined(_eeprom_h)
