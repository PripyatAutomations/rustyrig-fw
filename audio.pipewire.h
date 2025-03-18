#if	!defined(__rr_audio_pipewire_h)
#define	__rr_audio_pipewire_h

struct rr_au_pw_dev {
    rr_au_backend_t backend;
    struct pw_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_stream *rx_stream;
    struct pw_stream *tx_stream;
};
typedef struct rr_au_pw_dev rr_au_pw_dev_t;

struct rr_au_pw_data {
   struct pw_loop *loop;
   struct pw_stream *capture_stream;
   struct pw_stream *playback_stream;
};
typedef struct rr_au_pw_data rr_au_pw_data_t;

extern void pipewire_init(rr_au_pw_data_t *aud);
extern void pipewire_init_playback(rr_au_pw_data_t *aud);
extern bool rr_au_pw_runloop_all(void);

#endif	// !defined(__rr_audio_pipewire_h)
