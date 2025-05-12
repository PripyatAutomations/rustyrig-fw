//
// au.alsa.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_au_alsa_h)
#define	__rr_au_alsa_h

#include <stdbool.h>

typedef struct {
    int device_handle;  // Handle for the ALSA device
    unsigned int channels;
    unsigned int rate;
    unsigned int format;
} rr_au_alsa_device_t;

bool alsa_init(rr_au_alsa_device_t *device, const char *device_name);
bool alsa_write_samples(rr_au_alsa_device_t *device, const void *samples, size_t size);
bool alsa_read_samples(rr_au_alsa_device_t *device, void *buffer, size_t size);
void alsa_cleanup(rr_au_alsa_device_t *device);

#endif	// !defined(__rr_au_alsa_h)
