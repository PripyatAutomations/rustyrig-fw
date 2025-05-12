//
// au.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we wrap around our supported audio interfaces
//
// Most of the ugly bits should go in the per-backend sources
//
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "inc/i2c.h"
#include "inc/state.h"
#include "inc/eeprom.h"
#include "inc/logger.h"
#include "inc/cat.h"
#include "inc/posix.h"
#include "inc/au.h"
#include "inc/au.alsa.h"
#include "inc/au.pipe.h"
#include "inc/au.pipewire.h"
#include <opus/opus.h>  // Used for audio compression

typedef struct {
    rr_au_backend_t backend_type;
    bool (*init)(void);
    bool (*write_samples)(const void *samples, size_t size);
    rr_au_sample_t **(*read_samples)(void);
    void (*cleanup)(rr_au_device_t *dev);
} rr_au_backend_interface_t;

// Backend-specific function mappings
static rr_au_backend_interface_t backend_interface;

bool pipewire_backend_init(void) {
    static rr_au_pw_data_t pw_data; // Static instance for initialization
    pipewire_init(&pw_data);
    return true;
}

bool rr_au_init(void) {
    // Select the backend based on configuration
#if defined(FEATURE_ALSA)
    backend_interface = (rr_au_backend_interface_t){
        .backend_type = AU_BACKEND_ALSA,
        .init = alsa_init,
        .write_samples = alsa_write_samples,
        .read_samples = alsa_read_samples,
        .cleanup = alsa_cleanup,
    };
#elif defined(FEATURE_PIPE)
    backend_interface = (rr_au_backend_interface_t){
        .backend_type = AU_BACKEND_PIPE,
        .init = pipe_init,
        .write_samples = pipe_write_samples,
        .read_samples = pipe_read_samples,
        .cleanup = pipe_cleanup,
    };
#elif defined(FEATURE_PIPEWIRE)
    backend_interface = (rr_au_backend_interface_t){
        .backend_type = AU_BACKEND_PIPEWIRE,
        .init = pipewire_backend_init,
        .write_samples = NULL, // Add if implemented
        .read_samples = NULL,  // Add if implemented
        .cleanup = NULL,       // Add if implemented
    };
#else
    backend_interface = (rr_au_backend_interface_t){
        .backend_type = AU_BACKEND_NULL_SINK,
        .init = NULL,
        .write_samples = NULL,
        .read_samples = NULL,
        .cleanup = NULL,
    };
#endif

    // Initialize the selected backend
    if (backend_interface.init) {
        return backend_interface.init();
    } else {
        return false;
    }
}

bool rr_au_write_samples(const void *samples, size_t size) {
    if (backend_interface.write_samples) {
        return backend_interface.write_samples(samples, size);
    }
    return false;
}

rr_au_sample_t **rr_au_read_samples(void) {
    if (backend_interface.read_samples) {
        return backend_interface.read_samples();
    }
    return NULL;
}

void rr_au_cleanup(rr_au_device_t *dev) {
    if (backend_interface.cleanup) {
        backend_interface.cleanup(dev);
    }
}
