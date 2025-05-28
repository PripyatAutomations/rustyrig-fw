#include "inc/config.h"
#include <stdbool.h>
#include <stdint.h>
#include <gst/gst.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#define SOCKET_PATH_TX "/tmp/rustyrig_tx.sock"
#define SOCKET_PATH_RX "/tmp/rustyrig_rx.pipe"

struct audio_header {
   uint8_t magic[2];      // e.g. "AU"
   uint8_t sample_rate;   // 0 = 16000, 1 = 44100
   uint8_t format;        // 0 = S16LE, 1 = FLAC, 2 = OPUS
};

static GstElement *tx_pipeline = NULL;
static GstElement *rx_pipeline = NULL;

static int connect_unix_socket(const char *path) {
   int fd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (fd < 0) return -1;

   struct sockaddr_un addr = {0};
   addr.sun_family = AF_UNIX;
   strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

   if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      close(fd);
      return -1;
   }

   return fd;
}

static void cleanup_pipeline(GstElement **pipe) {
   if (*pipe) {
      gst_element_set_state(*pipe, GST_STATE_NULL);
      gst_object_unref(*pipe);
      *pipe = NULL;
   }
}

static GstElement *build_pipeline(const char *template_str, int fd) {
   char pipe_desc[1024];
   snprintf(pipe_desc, sizeof(pipe_desc), template_str, fd);
   return gst_parse_launch(pipe_desc, NULL);
}

static void run_loop(const char *rx_template, const char *tx_template) {
   while (1) {
      int tx_fd = -1, rx_fd = -1;

      while (/*tx_fd < 0 ||*/ rx_fd < 0) {
//         if (tx_fd < 0) tx_fd = connect_unix_socket(SOCKET_PATH_TX);
         if (rx_fd < 0) rx_fd = connect_unix_socket(SOCKET_PATH_RX);
         if (/*tx_fd < 0 ||*/ rx_fd < 0) {
//            if (tx_fd >= 0) { close(tx_fd); tx_fd = -1; }
            if (rx_fd >= 0) { close(rx_fd); rx_fd = -1; }
            fprintf(stderr, "Waiting for socket connections...\n");
            sleep(1);
         }
      }

      printf("connected tx_fd=%d rx_fd=%d\n", tx_fd, rx_fd);

//      tx_pipeline = build_pipeline(tx_template, tx_fd);
      rx_pipeline = build_pipeline(rx_template, rx_fd);

      if (/*!tx_pipeline ||*/ !rx_pipeline) {
         fprintf(stderr, "Failed to build one or both pipelines\n");
//         cleanup_pipeline(&tx_pipeline);
         cleanup_pipeline(&rx_pipeline);
//         close(tx_fd);
         close(rx_fd);
         sleep(1);
         continue;
      }

//      gst_element_set_state(tx_pipeline, GST_STATE_PLAYING);
      gst_element_set_state(rx_pipeline, GST_STATE_PLAYING);

//      GstBus *bus_tx = gst_element_get_bus(tx_pipeline);
      GstBus *bus_rx = gst_element_get_bus(rx_pipeline);

      gboolean running = TRUE;
      while (running) {
         GstMessage *msg /*
         = gst_bus_timed_pop_filtered(bus_tx, 100 * GST_MSECOND,
            GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
         if (!msg)
             msg
             */
              = gst_bus_pop_filtered(bus_rx, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

         if (msg) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
               GError *err;
               gchar *dbg;
               gst_message_parse_error(msg, &err, &dbg);
               fprintf(stderr, "GStreamer error: %s\n", err->message);
               g_error_free(err);
               g_free(dbg);
            } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
               fprintf(stderr, "GStreamer EOS received\n");
            }
            running = FALSE;
            gst_message_unref(msg);
         }
      }

//      gst_object_unref(bus_tx);
      gst_object_unref(bus_rx);
//      cleanup_pipeline(&tx_pipeline);
      cleanup_pipeline(&rx_pipeline);
//      close(tx_fd);
      close(rx_fd);
      sleep(1);
   }
}

static void gst_log_handler(GstDebugCategory *category, GstDebugLevel level,
                            const gchar *file, const gchar *function,
                            gint line, GObject *object, GstDebugMessage *message,
                            gpointer user_data) {
   g_printerr("GST %s: %s\n", gst_debug_level_get_name(level), gst_debug_message_get(message));
}

static void send_audio_header(int fd, int rate, int format) {
   struct audio_header hdr = {
      .magic = {'A', 'U'},
      .sample_rate = (rate == 44100) ? 1 : 0,
      .format = (uint8_t)format,
   };
   write(fd, &hdr, sizeof(hdr));
}

int main(int argc, char *argv[]) {
   printf("Starting fwdsp v.%s\n", VERSION);
   gst_init(&argc, &argv);
   gst_debug_add_log_function(gst_log_handler, NULL, NULL);

//   const char *rx_template = "pipewiresrc device=alsa_input.usb-Creative_Sound_Blaster_X-Fi_Go_Pro-00.analog-stereo ! audioconvert ! bandpass frequency=1700 width=2700 ! volume volume=2.0 ! audio/x-raw,rate=16000,format=S16LE,channels=1 ! fdsink fd=%d";
//   const char *rx_template = "alsasrc device=hw:1,0 ! audioconvert ! bandpass frequency=1700 width=2700 ! volume volume=2.0 ! audio/x-raw,rate=16000,format=S16LE,channels=1 ! fdsink fd=%d";
//   const char *rx_template = "alsasrc device=hw:1,0 ! audio/x-raw,format=S16LE,channels=2,rate=44100 ! fdsink fd=%d";
//   const char *rx_template = "audiotestsrc wave=sine ! audio/x-raw,rate=16000,format=S16LE,channels=1 ! fdsink fd=%d";
   int use_format = 0; // 0 = S16LE, 1 = FLAC, 2 = OPUS
   int sample_rate = 16000;

   const char *rx_template = NULL;
   switch (use_format) {
      case 0:
         rx_template = (sample_rate == 44100)
            ? "audiotestsrc wave=sine ! audio/x-raw,rate=44100,format=S16LE,channels=1 ! fdsink fd=%d"
            : "audiotestsrc wave=sine ! audio/x-raw,rate=16000,format=S16LE,channels=1 ! fdsink fd=%d";
         break;
      case 1:
         rx_template = "audiotestsrc wave=sine ! audio/x-raw,rate=44100,channels=1 ! flacenc ! fdsink fd=%d";
         break;
      case 2:
         rx_template = "audiotestsrc wave=sine ! audio/x-raw,rate=16000,channels=1 ! opusenc ! fdsink fd=%d";
         break;
   }
   const char *tx_template = "fdsrc fd=%d ! audio/x-raw,rate=16000,format=S16LE,channels=1 ! audioconvert ! volume volume=1.0 ! pipewiresink stream-properties=\"props,target.node.name='fwdsp TX'\"";

   run_loop(rx_template, tx_template);
   return 0;
}
