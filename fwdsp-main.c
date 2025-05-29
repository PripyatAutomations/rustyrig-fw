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

#define DEFAULT_SOCKET_PATH_TX "/tmp/rustyrig_tx.sock"
#define DEFAULT_SOCKET_PATH_RX "/tmp/rustyrig_rx.pipe"
static void send_audio_header(int fd, int rate, int format);

struct audio_header {
   uint8_t magic[2];      // e.g. "AU"
   uint8_t sample_rate;   // 0 = 16000, 1 = 44100
   uint8_t format;        // 0 = S16LE, 1 = FLAC, 2 = OPUS
};

struct audio_config {
   const char *template;
   const char *rx_path;
   const char *tx_path;
   int sample_rate;   // e.g. 16000 or 44100
   int format;        // 0 = S16LE, 1 = FLAC, 2 = OPUS
   bool rx_mode;
   bool tx_mode;
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

static void run_loop(const struct audio_config *cfg) {
   while (1) {
      int tx_fd = -1, rx_fd = -1;

      if (cfg->rx_mode) {
         while (rx_fd < 0) {
            rx_fd = connect_unix_socket(cfg->rx_path);
            if (rx_fd < 0) {
               fprintf(stderr, "Waiting for RX socket connection...\n");
               sleep(1);
            }
         }

         printf("connected rx_fd=%d\n", rx_fd);
         rx_pipeline = build_pipeline(cfg->template, rx_fd);

         if (!rx_pipeline) {
            fprintf(stderr, "Failed to build RX pipeline\n");
            cleanup_pipeline(&rx_pipeline);
            close(rx_fd);
            sleep(1);
            continue;
         }

         send_audio_header(rx_fd, cfg->sample_rate, cfg->format);

         gst_element_set_state(rx_pipeline, GST_STATE_PLAYING);
         GstBus *bus_rx = gst_element_get_bus(rx_pipeline);

         gboolean running = TRUE;
         while (running) {
            GstMessage *msg = gst_bus_timed_pop_filtered(bus_rx, 100 * GST_MSECOND,
               GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

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

         gst_object_unref(bus_rx);
         cleanup_pipeline(&rx_pipeline);
         close(rx_fd);
      }

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

   struct audio_config cfg = {
      .template = NULL,
      .rx_path = DEFAULT_SOCKET_PATH_RX,
      .tx_path = DEFAULT_SOCKET_PATH_TX,
      .sample_rate = 16000,
      .format = 0,
      .rx_mode = false,
      .tx_mode = false,
   };

   int opt;
   while ((opt = getopt(argc, argv, "c:s:R:T:p:rxh")) != -1) {
      switch (opt) {
         case 'c':
            if (strcmp(optarg, "flac") == 0) cfg.format = 1;
            else if (strcmp(optarg, "opus") == 0) cfg.format = 2;
            else cfg.format = 0;
            break;
         case 's':
            cfg.sample_rate = atoi(optarg);
            break;
         case 'R':
            cfg.rx_path = optarg;
            break;
         case 'T':
            cfg.tx_path = optarg;
            break;
         case 'p':
            cfg.template = optarg;
            break;
         case 'r':
            cfg.rx_mode = true;
            break;
         case 'x':
            cfg.tx_mode = true;
            break;
         case 'h':
         default:
            fprintf(stderr, "Usage: %s [-r] [-x] [-c pcm|flac|opus] [-s sample_rate] [-R rx_socket_path] [-T tx_socket_path] [-p tx_pipeline_template]\n", argv[0]);
            exit(1);
      }
   }

   if (!cfg.rx_mode && !cfg.tx_mode) cfg.rx_mode = true;

   if (cfg.rx_mode) {
      switch (cfg.format) {
         case 0:
            cfg.template = (cfg.sample_rate == 44100)
               ? "pulsesrc device=default ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=44100,channels=1 ! fdsink fd=%d"
               : "pulsesrc device=default ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=16000,channels=1 ! fdsink fd=%d";
            break;
         case 1:
            cfg.template = (cfg.sample_rate == 44100)
               ? "audiotestsrc wave=sine ! audio/x-raw,rate=44100,channels=1 ! flacenc ! fdsink fd=%d"
               : "audiotestsrc wave=sine ! audio/x-raw,rate=16000,channels=1 ! flacenc ! fdsink fd=%d";
            break;
         case 2:
            cfg.template = (cfg.sample_rate == 44100)
               ? "audiotestsrc wave=sine ! audio/x-raw,rate=44100,channels=1 ! opusenc ! fdsink fd=%d"
               : "audiotestsrc wave=sine ! audio/x-raw,rate=16000,channels=1 ! opusenc ! fdsink fd=%d";
            break;
      }
   }

   if (cfg.tx_mode) {
      // TX logic placeholder
   }

   run_loop(&cfg);
   return 0;
}
