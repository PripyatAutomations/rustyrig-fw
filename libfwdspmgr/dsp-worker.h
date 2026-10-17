// Generic local DSP worker supervisor (serial/control/PCM process channels).
#ifndef RR_DSP_WORKER_H
#define RR_DSP_WORKER_H
#include <stdbool.h>
#include <sys/types.h>
typedef struct {
   pid_t pid;
   int serial_fd;       // duplex stdin/stdout of child
   int control_fd;      // SOCK_SEQPACKET, child fd 3
   int rx_pcm_fd;       // server writes, child reads fd 4
   int tx_pcm_fd;       // server reads, child writes fd 5
} rr_dsp_worker_t;
void rr_dsp_worker_init(rr_dsp_worker_t *worker);
bool rr_dsp_worker_spawn(rr_dsp_worker_t *worker, const char *command);
void rr_dsp_worker_stop(rr_dsp_worker_t *worker);
#endif
