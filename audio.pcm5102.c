//
// Support for TI PCM5102 DAC
//
#include "config.h"
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "state.h"
#include "logger.h"
#include "eeprom.h"
#include "i2c.h"
#include "audio.h"
#if	defined(HOST_ESP32)
#include <driver/i2s.h>
#endif

struct au_device_t {
    au_backend_t backend;
    int i2s_port;
};

au_device_t *au_init(au_backend_t backend, const char *device_name) {
#if	defined(HOST_ESP32)
    if (backend != AU_BACKEND_I2S) return NULL;

    au_device_t *dev = calloc(1, sizeof(au_device_t));
    dev->backend = backend;
    dev->i2s_port = I2S_NUM_0;

    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = 44100,
        .bits_per_sample = 16,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .dma_buf_count = 4,
        .dma_buf_len = 512,
    };

    i2s_driver_install(dev->i2s_port, &i2s_config, 0, NULL);
    return dev;
#else
    return NULL;
#endif
}

void au_poll(au_device_t *dev) {
    // I2S doesn’t need a poll function, but could handle buffer refill here
}

void au_cleanup(au_device_t *dev) {
#if	defined(HOST_ESP32)
    if (dev->backend == AU_BACKEND_I2S) {
        i2s_driver_uninstall(dev->i2s_port);
    }
    free(dev);
#endif
}
