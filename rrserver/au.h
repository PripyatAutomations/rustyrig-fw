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
#include "librustyaxe/codecneg.h"

typedef enum {
    AU_BACKEND_NULL_SINK = 0, // Null sink that discards all input but generates logging statistics
    AU_BACKEND_PIPEWIRE,      // Pipewire interface for Linux hosts
    AU_BACKEND_I2S,           // For ESP32 PCM5102, etc.
    AU_BACKEND_ALSA,          // ALSA backend
    AU_BACKEND_OSS,           // OSS backend
    AU_BACKEND_PIPE,          // PCM via pipe/socket
} rr_au_backend_t;

typedef float rr_au_sample_t;

typedef struct rr_au_device_t rr_au_device_t;

typedef struct {
    rr_au_backend_t backend_type;
    bool (*init)(void);
    bool (*write_samples)(const void *samples, size_t size);
    rr_au_sample_t **(*read_samples)(void);
    void (*cleanup)(rr_au_device_t *dev);
} rr_au_backend_interface_t;

extern bool rr_au_init(void);
extern bool rr_au_write_samples(rr_au_backend_interface_t *be, const void *samples, size_t size);
extern rr_au_sample_t **rr_au_read_samples(rr_au_backend_interface_t *be);
extern void rr_au_cleanup(rr_au_backend_interface_t *be, rr_au_device_t *dev);

//////////
struct au_recording_instance {
   int channel;			// Audio channel ID
};
typedef struct au_recording_instance au_recording_t;

// Initialize the UNIX domain socket server for receiving audio
extern void au_unix_socket_init(void);
// Cleanup the UNIX domain socket server and any client connections
extern void au_unix_socket_cleanup(void);
// Poll the UNIX socket server and client; handle new connections and incoming audio data
extern void au_unix_socket_poll(void);
extern const char *au_recording_start(int channel);
extern bool au_recording_stop(const char *id);

#include "rrserver/au.pcm5102.h"

#endif	// !defined(__rr_audio_h)
