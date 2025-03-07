#if	!defined(__rr_audio_h)
#define __rr_audio_h

typedef struct uint32_t au_sample_t;

extern bool audio_init(void);
extern bool audio_write_samples(void);
extern au_sample_t **audio_read_samples(void);

#endif	// !defined(__rr_audio_h)
