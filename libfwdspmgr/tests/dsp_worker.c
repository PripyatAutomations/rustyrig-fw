// Focused tests for the generic external DSP worker API.
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <libfwdspmgr/dsp-worker.h>

int main(void) {
   rr_dsp_worker_ports_t modem = rr_dsp_worker_modem_ports();
   assert(rr_dsp_worker_ports_valid(&modem));
   modem.radio_pcm.sample_rate = 0;
   assert(!rr_dsp_worker_ports_valid(&modem));
   modem = rr_dsp_worker_modem_ports();

   rr_dsp_worker_ports_t voice = modem;
   voice.application = RR_DSP_PORT_PCM;
   voice.application_pcm = (rr_dsp_audio_format_t){
      .format=RR_DSP_PCM_S16LE,.sample_rate=8000,.channels=1
   };
   assert(rr_dsp_worker_ports_valid(&voice));
   voice.application_pcm.channels = 3;
   assert(!rr_dsp_worker_ports_valid(&voice));

   rr_dsp_worker_t worker;
   rr_dsp_worker_init(&worker);
   assert(rr_dsp_worker_spawn_ports(&worker, "/bin/cat", &modem));
   assert(worker.serial_fd >= 0 && worker.control_fd >= 0);
   assert(worker.rx_pcm_fd >= 0 && worker.tx_pcm_fd >= 0);
   assert(worker.ports.application == RR_DSP_PORT_BYTES);
   const char msg[] = "modem-stream-test";
   assert(write(worker.serial_fd, msg, sizeof(msg)) == sizeof(msg));
   struct pollfd pfd = {.fd=worker.serial_fd,.events=POLLIN};
   assert(poll(&pfd, 1, 2000) == 1);
   char echo[sizeof(msg)];
   assert(read(worker.serial_fd, echo, sizeof(echo)) == sizeof(echo));
   assert(memcmp(msg, echo, sizeof(msg)) == 0);
   rr_dsp_worker_stop(&worker);
   assert(worker.pid == 0 && worker.serial_fd == -1);
   return 0;
}
