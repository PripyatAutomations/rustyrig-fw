//
// au.c: Handle receiving/sending audio between this service and audio backends such as fwdsp for gstreamer
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
#include "build_config.h"
#include "librustyaxe/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>
#include <sys/un.h>
#include <fcntl.h>
#include "../ext/libmongoose/mongoose.h"
#include "librustyaxe/logger.h"
#include "rrserver/i2c.h"
#include "rrserver/state.h"
#include "rrserver/eeprom.h"
#include "librustyaxe/cat.h"
#include "librustyaxe/posix.h"
#include "librustyaxe/codecneg.h"
#include "rrserver/au.h"
#include "rrserver/au.pipe.h"
#include "rrserver/ws.h"
#include "librustyaxe/fwdsp-shared.h"

// XXX: This needs moved to config/${profile}.fwdsp.json:fwdsp.channels.name['rx'].path

rr_au_backend_interface_t au_backend_null = {
    .backend_type = AU_BACKEND_NULL_SINK,
    .init = NULL,
    .write_samples = NULL,
    .read_samples = NULL,
    .cleanup = NULL
};

bool rr_au_init(void) {
    rr_au_backend_interface_t *be = &au_backend_null;

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

