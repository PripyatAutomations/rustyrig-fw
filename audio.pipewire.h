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

#endif	// !defined(__rr_audio_pipewire_h)