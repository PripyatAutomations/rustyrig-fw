// Generic subprocess FD layout, independent of GStreamer and modem codec.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <libfwdspmgr/dsp-worker.h>

/* Describe ports independently of any particular codec or modem.
 * Only the declared PCM formats are checked here; scheduling and conversion
 * belong to the server's audio hub, not the subprocess launcher. */
static bool pcm_valid(rr_dsp_audio_format_t pcm) {
   return (pcm.format == RR_DSP_PCM_S16LE || pcm.format == RR_DSP_PCM_F32LE) &&
      pcm.sample_rate >= 8000 && pcm.sample_rate <= 192000 &&
      pcm.channels >= 1 && pcm.channels <= 2;
}
bool rr_dsp_worker_ports_valid(const rr_dsp_worker_ports_t *ports) {
   if (!ports || ports->application < RR_DSP_PORT_BYTES ||
       ports->application > RR_DSP_PORT_PCM || ports->radio != RR_DSP_PORT_PCM)
      return false;
   return pcm_valid(ports->radio_pcm) &&
      (ports->application != RR_DSP_PORT_PCM || pcm_valid(ports->application_pcm));
}
rr_dsp_worker_ports_t rr_dsp_worker_modem_ports(void) {
   return (rr_dsp_worker_ports_t) {
      .application = RR_DSP_PORT_BYTES,
      .radio = RR_DSP_PORT_PCM,
      .radio_pcm = {.format=RR_DSP_PCM_S16LE,.sample_rate=16000,.channels=1}
   };
}
static void child_reap(GPid pid, gint status, gpointer user) {
   (void)status; (void)user;
   g_spawn_close_pid(pid);
}
void rr_dsp_worker_init(rr_dsp_worker_t *w) {
   if (!w) return;
   *w = (rr_dsp_worker_t){.serial_fd=-1,.control_fd=-1,.rx_pcm_fd=-1,.tx_pcm_fd=-1};
}
static void close_pair(int fds[2]) {
   for (int i=0;i<2;i++) if (fds[i]>=0) {close(fds[i]); fds[i]=-1;}
}
static bool make_pair(int fds[2], int type) {
   if (socketpair(AF_UNIX, type, 0, fds)) return false;
   for (int i=0;i<2;i++) fcntl(fds[i], F_SETFD, FD_CLOEXEC);
   return true;
}
static void nonblock(int fd) {
   int flags = fcntl(fd, F_GETFL);
   if (flags>=0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
bool rr_dsp_worker_spawn_ports(rr_dsp_worker_t *w, const char *command,
   const rr_dsp_worker_ports_t *ports) {
   if (!rr_dsp_worker_ports_valid(ports)) return false;
   if (!w || !command || !*command || w->pid > 0) return false;
   int serial[2]={-1,-1}, control[2]={-1,-1}, rx[2]={-1,-1}, tx[2]={-1,-1};
   gchar **argv=NULL;
   gint argc=0;
   GError *error=NULL;
   if (!g_shell_parse_argv(command,&argc,&argv,&error) || !argc) {
      g_clear_error(&error); g_strfreev(argv); return false;
   }
   if (!make_pair(serial,SOCK_STREAM) || !make_pair(control,SOCK_SEQPACKET) ||
       !make_pair(rx,SOCK_STREAM) || !make_pair(tx,SOCK_STREAM)) goto fail;
   pid_t pid=fork();
   if (pid<0) goto fail;
   if (pid==0) {
      // Child serial stdin/stdout, control, RX audio input, TX audio output.
      // Dup source sockets to safe high FDs before remapping to avoid overlap.
      int src[5]={serial[1],serial[1],control[1],rx[1],tx[1]};
      int dest[5]={0,1,3,4,5};
      for (int i=0;i<5;i++) {
         int saved=fcntl(src[i], F_DUPFD_CLOEXEC, 16);
         if (saved<0) _exit(127);
         src[i]=saved;
      }
      close_pair(serial);close_pair(control);close_pair(rx);close_pair(tx);
      for (int i=0;i<5;i++) if (dup2(src[i],dest[i])<0) _exit(127);
      for (int i=0;i<5;i++) close(src[i]);
      execvp(argv[0],argv);
      _exit(127);
   }
   close(serial[1]);close(control[1]);close(rx[1]);close(tx[1]);
   w->pid=pid;
   w->ports=*ports;
   w->serial_fd=serial[0];w->control_fd=control[0];
   w->rx_pcm_fd=rx[0];w->tx_pcm_fd=tx[0];
   nonblock(w->serial_fd);nonblock(w->control_fd);
   nonblock(w->rx_pcm_fd);nonblock(w->tx_pcm_fd);
   g_child_watch_add(pid,child_reap,NULL);
   g_strfreev(argv);
   return true;
fail:
   close_pair(serial);close_pair(control);close_pair(rx);close_pair(tx);
   g_strfreev(argv);
   return false;
}
void rr_dsp_worker_stop(rr_dsp_worker_t *w) {
   if (!w) return;
   if (w->serial_fd>=0) close(w->serial_fd);
   if (w->control_fd>=0) close(w->control_fd);
   if (w->rx_pcm_fd>=0) close(w->rx_pcm_fd);
   if (w->tx_pcm_fd>=0) close(w->tx_pcm_fd);
   if (w->pid>0) kill(w->pid,SIGTERM);
   rr_dsp_worker_init(w);
}

bool rr_dsp_worker_spawn(rr_dsp_worker_t *w, const char *command) {
   rr_dsp_worker_ports_t ports = rr_dsp_worker_modem_ports();
   return rr_dsp_worker_spawn_ports(w, command, &ports);
}
