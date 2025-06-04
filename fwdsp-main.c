// fwdsp-main.c: firmware DSP bridge This is part of rustyrig-fw. 
// 	https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
//
// Here we handle moving audio between gstreamer and the firmwre.
// You will typically have two instances running of fwdsp.bin
// One with the -t argument and another with -r, for TX and RX, respectively
//
// Eventually configuration will be moved to config/$PROFILE.fwdsp.json
//
// XXX: We need to try our best to stay running after errors
// XXX: - Auto-reconnect, with increasing backoff
//
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
#include <stdbool.h>
#if	!defined(__FWDSP)
#define	__FWDSP
#endif
#include "inc/config.h"
#include "inc/fwdsp-shared.h"
#include "inc/logger.h"
#include "inc/posix.h"
#include "inc/util.file.h"

static void send_format_header(int fd, struct audio_config *cfg);

bool dying = false;
static GstElement *pipeline = NULL;
time_t now = -1;                // time() called once a second in main loop to update

static int connect_unix_socket(const char *path) {
   int fd = socket(AF_UNIX, SOCK_STREAM, 0);

   if (fd < 0) {
      return -1;
   }

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

static void run_loop(struct audio_config *cfg) {
   while (1) {
      int sock_fd = -1;

      while (sock_fd < 0) {
         sock_fd = connect_unix_socket(cfg->sock_path);

         if (sock_fd < 0) {
            fprintf(stderr, "fwdsp: Waiting for socket connection...\n");
            sleep(1);
         }
      }

      printf("connected %s sock_fd=%d\n", (cfg->tx_mode ? "TX" : "RX"), sock_fd);
      pipeline = build_pipeline(cfg->template, sock_fd);

      if (!pipeline) {
         fprintf(stderr, "fwdsp: Failed to build pipeline\n");
         cleanup_pipeline(&pipeline);
         close(sock_fd);
         sleep(1);
         continue;
      }

//      send_format_header(sock_fd, cfg);
      GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
      if (ret == GST_STATE_CHANGE_FAILURE) {
         g_printerr("Failed to set pipeline to PLAYING state.\n");
         GstMessage *msg = gst_bus_poll(gst_element_get_bus(pipeline), GST_MESSAGE_ERROR, 0);
         if (msg) {
            GError *err;
            gchar *debug_info;
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
            g_printerr("Debugging info: %s\n", debug_info ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
            gst_message_unref(msg);
            exit(1);
         }
      }

      GstBus *bus = gst_element_get_bus(pipeline);
      GstMessage *msg = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
          GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED);

      if (msg != NULL) {
         GError *err;
         gchar *debug_info;

         switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR:
               gst_message_parse_error(msg, &err, &debug_info);
               g_printerr("Error from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
               g_printerr("Debug info: %s\n", debug_info ? debug_info : "none");
               g_clear_error(&err);
               g_free(debug_info);
               break;

            case GST_MESSAGE_EOS:
               g_print("End-Of-Stream reached.\n");
               break;

            case GST_MESSAGE_STATE_CHANGED:
               if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
                  GstState old_state, new_state, pending_state;
                  gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                  g_print("Pipeline state changed from %s to %s.\n",
                          gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
               }
               break;

            default:
               // Not expected
               break;
         }

         gst_message_unref(msg);
      }
      gst_object_unref(bus);

      while (!dying) {
         GstMessage *msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
            GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

         if (msg) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
               GError *err;
               gchar *dbg;
               gst_message_parse_error(msg, &err, &dbg);
               fprintf(stderr, "fwdsp: GStreamer error: %s\n", err->message);
               g_error_free(err);
               g_free(dbg);
            } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
               fprintf(stderr, "fwdsp: GStreamer EOS received\n");
            }
            dying = true;
            gst_message_unref(msg);
         }
      }

      gst_object_unref(bus);
      cleanup_pipeline(&pipeline);
      close(sock_fd);
   }

   sleep(1);
}

static void gst_log_handler(GstDebugCategory *category, GstDebugLevel level,
                            const gchar *file, const gchar *function,
                            gint line, GObject *object, GstDebugMessage *message,
                            gpointer user_data) {
   g_printerr("GST %s: %s\n", gst_debug_level_get_name(level), gst_debug_message_get(message));
}

static void send_format_header(int fd, struct audio_config *cfg) {
   struct audio_header hdr = {
      .magic = {'A', 'U'},
//      .sample_rate = (cfg->sample_rate == 44100) ? 1 : 0,
      .sample_rate = (cfg->sample_rate),
      .format = (uint8_t)cfg->format,
      .channel_id = (uint8_t)cfg->channel_id
   };
   write(fd, &hdr, sizeof(hdr));
}

int main(int argc, char *argv[]) {
   printf("Starting fwdsp v.%s\n", VERSION);
   host_init();
   logfp = stdout;
   logger_init();
   gst_init(&argc, &argv);
   gst_debug_add_log_function(gst_log_handler, NULL, NULL);

   struct audio_config cfg = {
      .template = NULL,
      .sample_rate = 16000,
      .format = 0,
      .tx_mode = false,
      .channel_id = -1
   };

   // set sane defaults
   if (cfg.tx_mode) {
      cfg.sock_path = DEFAULT_SOCKET_PATH_TX;
   } else {
      cfg.sock_path = DEFAULT_SOCKET_PATH_RX;
   }

   int opt;
   while ((opt = getopt(argc, argv, "c:i:s:P:p:hrtx")) != -1) {
      switch (opt) {
         case 'P':
            cfg.sock_path = optarg;
            break;
         case 'c':
            if (strcmp(optarg, "flac") == 0) {
               cfg.format = 1;
            } else if (strcmp(optarg, "opus") == 0) {
               cfg.format = 2;
            } else {
               cfg.format = 0;
            }
            break;
         case 'i':
            cfg.channel_id = atoi(optarg);
            break;
         case 'p':
            cfg.template = optarg;
            break;
         case 's':
            cfg.sample_rate = atoi(optarg);
            break;
         case 't':
            cfg.tx_mode = true;
            break;
         case 'x':
            cfg.persistent = true;
            printf("We are running in persistent mode, we will reconnect until receive fatal signal such as SIGKILL!\n");
            break;
         case 'h':
         default:
            fprintf(stderr, "Usage: %s [-t] [-x] [-c pcm|flac|opus] [-s sample_rate] [-P socket] [-p pipeline]\n", argv[0]);
            exit(1);
      }
   }

   if (cfg.tx_mode) {
      if (cfg.channel_id < 0) {
         cfg.channel_id = 1;
      }

      cfg.template = "fdsrc fd=%d ! audio/x-raw,rate=16000,format=S16LE,channels=1 ! audioconvert ! volume volume=1.0 ! pipewiresink stream-properties=\"props,target.node.name='fwdsp TX'\"";
   } else {
      if (cfg.channel_id < 0) {
         cfg.channel_id = 0;
      }

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

   time_t last_run = -1;
   do {
      now = time(NULL);
      run_loop(&cfg);
      printf("Run took %li sec", (now - last_run));
      last_run = now;
   } while(cfg.persistent);

   host_cleanup();
   return 0;
}

void shutdown_app(int signum) {
   exit(signum);
}
