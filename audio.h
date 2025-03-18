#if	!defined(__rr_audio_h)
#define __rr_audio_h

typedef enum {
    AU_BACKEND_NULL_SINK = 0,	// Null sink that discards all input but generates logging statistics
    AU_BACKEND_PIPEWIRE,	// Pipewire interface for linux hosts
    AU_BACKEND_I2S, 		// For ESP32 PCM5102, etc
    AU_BACKEND_ALSA,		// Perhaps someone will write the ALSA backend?
    AU_BACKEND_OSS,		// And maybe even OSS for those heathens
} rr_au_backend_t;

typedef struct rr_au_device_t rr_au_device_t;
typedef struct uint32_t rr_au_sample_t;

struct rr_au_interface {
   rr_au_backend_t		be_type;		// Backend type
};
typedef struct rr_au_interface rr_au_interface_t;

/* Initialize an audio device with a backend and name */
//extern rr_au_device_t *rr_au_init(rr_au_backend_t backend, const char *device_name);
extern bool rr_au_init(void);
/* Process audio events (non-blocking) */
extern void rr_au_poll(rr_au_device_t *dev);
/* Cleanup resources */
extern void rr_au_cleanup(rr_au_device_t *dev);
extern bool rr_au_write_samples(void);
extern rr_au_sample_t **rr_au_read_samples(void);

#include "audio.pipewire.h"
#include "audio.pcm5102.h"

#endif	// !defined(__rr_audio_h)
