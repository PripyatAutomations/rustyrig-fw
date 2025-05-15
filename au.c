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
#if	defined(FEATURE_OPUS)
#include <opus/opus.h>  // Used for audio compression
#endif

rr_au_backend_interface_t au_backend_null = {
    .backend_type = AU_BACKEND_NULL_SINK,
    .init = NULL,
    .write_samples = NULL,
    .read_samples = NULL,
    .cleanup = NULL
};

bool rr_au_init(rr_au_backend_interface_t *be) {
    // Initialize the selected backend
    if (be && be->init) {
        return be->init();
    }
    return true;
}

bool rr_au_write_samples(rr_au_backend_interface_t *be ,const void *samples, size_t size) {
    if (be->write_samples) {
        return be->write_samples(samples, size);
    }
    return false;
}

rr_au_sample_t **rr_au_read_samples(rr_au_backend_interface_t *be) {
    if (be->read_samples) {
        return be->read_samples();
    }
    return NULL;
}

void rr_au_cleanup(rr_au_backend_interface_t *be, rr_au_device_t *dev) {
    if (be->cleanup) {
        be->cleanup(dev);
    }
}
