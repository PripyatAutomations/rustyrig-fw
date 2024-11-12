#if	!defined(_i2c_h)
#define	_i2c_h

#include <stdint.h>
#include <stdbool.h>

#if	defined(HOST_POSIX)
// XXX: Include host i2c support
#else
// Include uc specific i2c
#endif

typedef struct {
    int fd; 		// File descriptor (Linux) or pointer/handle (STM32)
    int bus_id;		// Bus identifier, if applicable
    int my_addr; 	// My address on the bus
} I2CBus;

bool i2c_init(I2CBus *bus, int my_addr);
bool i2c_write(I2CBus *bus, uint8_t device_address, uint8_t *data, size_t length);
bool i2c_read(I2CBus *bus, uint8_t device_address, uint8_t *buffer, size_t length);

#endif	// !defined(_i2c_h)

