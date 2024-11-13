#ifdef HOST_STM32
#include "stm32f4xx_hal.h"

// External declarations for multiple I2C handles
extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2; // Example for a second I2C bus

static I2C_HandleTypeDef *get_i2c_handle(uint32_t bus_id) {
    switch (bus_id) {
        case 1:
            return &hi2c1;
        case 2:
            return &hi2c2;
        // Add more cases as needed
        default:
            return NULL;
    }
}

bool i2c_init(I2CBus *bus, uint32_t my_addr) {
    I2C_HandleTypeDef *handle = get_i2c_handle(bus->bus_id);

    // Addresses below 0 aren't valid
    if (my_addr > 0) {
       bus->my_addr = my_addr;
    } else {
       bus->my_addr = 0;
    }

    return handle && (HAL_I2C_Init(handle) == HAL_OK);
}

bool i2c_write(I2CBus *bus, uint8_t device_address, uint8_t *data, size_t length) {
    I2C_HandleTypeDef *handle = get_i2c_handle(bus->bus_id);
    return handle && (HAL_I2C_Master_Transmit(handle, (device_address << 1), data, length, HAL_MAX_DELAY) == HAL_OK);
}

bool i2c_read(I2CBus *bus, uint8_t device_address, uint8_t *buffer, size_t length) {
    I2C_HandleTypeDef *handle = get_i2c_handle(bus->bus_id);
    return handle && (HAL_I2C_Master_Receive(handle, (device_address << 1), buffer, length, HAL_MAX_DELAY) == HAL_OK);
}
#endif // HOST_STM32
