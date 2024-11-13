#ifdef HOST_LINUX
#include "i2c.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <unistd.h>
#include <stdio.h>

bool i2c_init(I2CBus *bus, uint32_t my_addr) {
    char i2c_bus_path[20];
    snprintf(i2c_bus_path, sizeof(i2c_bus_path), "/dev/i2c-%d", bus->bus_id);
    bus->fd = open(i2c_bus_path, O_RDWR);

    // Addresses below 0 aren't valid
    if (my_addr >= 0) {
       bus->my_addr = my_addr;
    } else {
       bus->my_addr = 0;
    }

    return (bus->fd >= 0);
}

bool i2c_write(I2CBus *bus, uint8_t device_address, uint8_t *data, size_t length) {
    if (ioctl(bus->fd, I2C_SLAVE, device_address) < 0) {
        perror("Failed to set I2C slave address");
        return false;
    }
    return (write(bus->fd, data, length) == (ssize_t)length);
}

bool i2c_read(I2CBus *bus, uint8_t device_address, uint8_t *buffer, size_t length) {
    if (ioctl(bus->fd, I2C_SLAVE, device_address) < 0) {
        perror("Failed to set I2C slave address");
        return false;
    }
    return (read(bus->fd, buffer, length) == (ssize_t)length);
}
#endif // HOST_LINUX
