// Generic external DSP worker: process isolation and typed media ports.
// MIT license. This header is independent of GStreamer and GTK.
#ifndef RR_DSP_WORKER_H
#define RR_DSP_WORKER_H
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/* The application stream may contain arbitrary binary data, discrete packets, or PCM speech. Radio-facing streams always carry PCM. The control socket is a
 * separate message-oriented channel. */
typedef enum {
   RR_DSP_PORT_DISABLED = 0,
   RR_DSP_PORT_BYTES,
   RR_DSP_PORT_PACKETS,
   RR_DSP_PORT_PCM
} rr_dsp_port_type_t;

typedef enum {
   RR_DSP_PCM_S16LE = 1,
   RR_DSP_PCM_F32LE
} rr_dsp_pcm_format_t;

typedef struct {
   rr_dsp_pcm_format_t format;
   uint32_t sample_rate;
   uint8_t channels;
} rr_dsp_audio_format_t;

/* One channel in each direction on each side of a worker. Application: fd 0 (input) and fd 1 (output). Rig RX: fd 4 (input from rig); rig TX: fd 5 (output to
 * rig). Workers can be serial modems (BYTES/PCM) or speech codecs (PCM/PCM). */
typedef struct {
   rr_dsp_port_type_t application;
   rr_dsp_port_type_t radio;
   rr_dsp_audio_format_t application_pcm;
   rr_dsp_audio_format_t radio_pcm;
} rr_dsp_worker_ports_t;

typedef struct {
   pid_t pid;
   int serial_fd;               // application-side duplex stream (child fd 0/1)
   int control_fd;              // SOCK_SEQPACKET (child fd 3)
   int rx_pcm_fd;               // server writes rig RX PCM; child reads fd 4
   int tx_pcm_fd;               // child writes rig TX PCM; server reads fd 5
   rr_dsp_worker_ports_t ports;
} rr_dsp_worker_t;

/* Validation is deliberately side-effect-free and usable by plugin loaders. */
bool rr_dsp_worker_ports_valid(const rr_dsp_worker_ports_t *ports);
rr_dsp_worker_ports_t rr_dsp_worker_modem_ports(void);
void rr_dsp_worker_init(rr_dsp_worker_t *worker);
bool rr_dsp_worker_spawn_ports(rr_dsp_worker_t *worker, const char *command, const rr_dsp_worker_ports_t *ports);
/* Compatibility wrapper for existing modem callers. */
bool rr_dsp_worker_spawn(rr_dsp_worker_t *worker, const char *command);
void rr_dsp_worker_stop(rr_dsp_worker_t *worker);
#endif
