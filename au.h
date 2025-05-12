//
// au.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_audio_h)
#define __rr_audio_h
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    AU_BACKEND_NULL_SINK = 0, // Null sink that discards all input but generates logging statistics
    AU_BACKEND_PIPEWIRE,      // Pipewire interface for Linux hosts
    AU_BACKEND_I2S,           // For ESP32 PCM5102, etc.
    AU_BACKEND_ALSA,          // ALSA backend
    AU_BACKEND_OSS,           // OSS backend
    AU_BACKEND_PIPE,          // PCM via pipe/socket
} rr_au_backend_t;

typedef struct rr_au_device_t rr_au_device_t;
typedef struct uint32_t rr_au_sample_t;

bool rr_au_init(void);
bool rr_au_write_samples(const void *samples, size_t size);
rr_au_sample_t **rr_au_read_samples(void);
void rr_au_cleanup(rr_au_device_t *dev);

#include "au.pipewire.h"
#include "au.pcm5102.h"

#endif	// !defined(__rr_audio_h)
