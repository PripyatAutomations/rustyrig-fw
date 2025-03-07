#if	!defined(__rr_audio_h)
#define __rr_audio_h

typedef enum {
    AU_BACKEND_PIPEWIRE,
    AU_BACKEND_I2S,  // For ESP32 PCM5102
    AU_BACKEND_ALSA  // Optional for Linux
} au_backend_t;

typedef struct au_device_t au_device_t;
typedef struct uint32_t au_sample_t;

/* Initialize an audio device with a backend and name */
extern au_device_t *au_init(au_backend_t backend, const char *device_name);
/* Process audio events (non-blocking) */
extern void au_poll(au_device_t *dev);
/* Cleanup resources */
extern void au_cleanup(au_device_t *dev);

extern bool audio_init(void);
extern bool audio_write_samples(void);
extern au_sample_t **audio_read_samples(void);

#endif	// !defined(__rr_audio_h)
