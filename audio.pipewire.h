#if	!defined(__rr_audio_pipewire_h)
#define	__rr_audio_pipewire_h
struct au_device_t {
    au_backend_t backend;
    struct pw_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_stream *rx_stream;
    struct pw_stream *tx_stream;
};

struct audio_data {
   struct pw_loop *loop;
   struct pw_stream *capture_stream;
   struct pw_stream *playback_stream;
};

extern void pipewire_init(struct audio_data *aud);
extern void pipewire_init_playback(struct audio_data *aud);
extern bool au_pw_runloop(void);
extern struct audio_data aud;
#endif	// !defined(__rr_audio_pipewire_h)
