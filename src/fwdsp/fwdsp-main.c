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
#include "build_config.h"
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
#include "common/config.h"
#include "common/fwdsp-shared.h"
#include "common/logger.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "common/codecneg.h"
#include "rustyrig/config-paths.h"
const char *config_file = "config/rrserver.cfg";
const char *config_codec = "PC16";
bool codec_tx_mode = false;
bool dying = false;
bool empty_config;
static GstElement *pipeline = NULL;
time_t now = -1;                // time() called once a second in main loop to update

defconfig_t defcfg[] = {
  { "audio.debug",	"false",	"gstreamer debug level" },
  { "log.level",	"debug",	"main log level" }, 
  { "log.show-ts",	"false",	"show timestamps in logs" },
  { "test.key",		" 1",		"Test key" },
  { NULL,		NULL,		NULL }
};

static void send_format_header(int fd, struct audio_config *cfg);

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

static GstElement *build_pipeline(const char *pipeline_str, int fd) {
   char pipe_desc[1024];

   snprintf(pipe_desc, sizeof(pipe_desc), pipeline_str, fd);
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
      pipeline = build_pipeline(cfg->pipeline, sock_fd);

      if (!pipeline) {
         fprintf(stderr, "fwdsp: Failed to build pipeline\n");
         cleanup_pipeline(&pipeline);
         close(sock_fd);
         sleep(1);
         continue;
      }

/* XXX: Implement bus signals instead of polling
GstBus *bus;

[..]

bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
gst_bus_add_signal_watch (bus);
g_signal_connect (bus, "message::error", G_CALLBACK (cb_message_error), NULL);
g_signal_connect (bus, "message::eos", G_CALLBACK (cb_message_eos), NULL);
*/

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

// XXX: Finish this
static void send_format_header(int fd, struct audio_config *cfg) {
   struct audio_header hdr = {
      .magic = {'M', 'U', '1', '6'},
//      .sample_rate = (cfg->sample_rate == 44100) ? 1 : 0,
      .sample_rate = (cfg->sample_rate),
//      .format = (uint8_t)cfg->format,
      .channel_id = (uint8_t)cfg->channel_id
   };
   write(fd, &hdr, sizeof(hdr));
}

int main(int argc, char *argv[]) {
   bool empty_config = true;
   printf("Starting fwdsp v.%s\n", VERSION);
   host_init();
   logfp = stdout;
   log_level = LOG_DEBUG;

   int opt;
   while ((opt = getopt(argc, argv, "c:f:ht")) != -1) {
      switch (opt) {
         case 'c':
            size_t clen = strlen(optarg);
            if (clen < 0 || clen > 4) {
               fprintf(stderr, "Codec magic (-c) '%s' *must* be exactly 4 characters\n", optarg);
               exit(1);
            } else {
               fprintf(stdout, "Setting codec magic to %s\n", optarg);
               config_codec = strdup(optarg);
            }
            break;
         case 'f':
            config_file = strdup(optarg);
            break;
         case 't':
            codec_tx_mode = true;
            break;
         case 'h':
         default:
            fprintf(stderr, "Usage: %s [-f config file] [-c codec-string] [-t]\n", argv[0]);
            fprintf(stderr, "  -c\t\t\tIs the codec id such as PCM16 or MU44\n");
            fprintf(stderr, "  -f\t\t\tFile name of config\n");
            fprintf(stderr, "  -t\t\t\tTransmit mode\n");
            exit(1);
      }
   }

   // codec_mapping_t *au_codec_find_by_magic(magic);
   // Find and load the configuration file
   int cfg_entries = (sizeof(configs) / sizeof(char *));

   cfg_init(default_cfg, defcfg);

   if (config_file) {
      if (!(cfg = cfg_load(config_file))) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", config_file);
      } else {
         Log(LOG_DEBUG, "config", "Loaded config from '%s'", config_file);
      }
   } else {
      char *realpath = find_file_by_list(configs, cfg_entries);
      if (realpath) {
         config_file = strdup(realpath);
         if (!(cfg = cfg_load(realpath))) {
            Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", realpath);
         } else {
            Log(LOG_DEBUG, "config", "Loaded config from '%s'", realpath);
         }
         empty_config = false;
         free(realpath);
      } else {
        // Use default settings and save it to ~/.config/rrclient.cfg
        cfg = default_cfg;
        empty_config = true;
        fprintf(stderr, "No config found :(\n");
        exit(1);
      }
      // unneeded unless new code added between here and inner else
//      free(realpath);
   }
#if	0
   fprintf(stderr, "----- cfg -----\n");
   dict_dump(cfg, stderr);
   fprintf(stderr, "----- @<%x> -----\n", cfg);
#endif
   logger_init();

   // Set up some debugging
   setenv("GST_DEBUG_DUMP_DOT_DIR", ".", 0);
   const char *cfg_audio_debug = cfg_get("audio.debug");
   if (cfg_audio_debug) {
      setenv("GST_DEBUG", cfg_audio_debug, 0);
   }

   struct audio_config au_cfg = {
      .pipeline = NULL,
      .sample_rate = 16000,
      .format = 0,
      .tx_mode = false,
      .channel_id = -1
   };

   // set sane defaults
   if (au_cfg.tx_mode) {
      au_cfg.sock_path = DEFAULT_SOCKET_PATH_TX;
   } else {
      au_cfg.sock_path = DEFAULT_SOCKET_PATH_RX;
   }


   if (au_cfg.channel_id < 0) {
      au_cfg.channel_id = 0;
   }

   char keybuf[256];
   memset(keybuf, 0, sizeof(keybuf));
   snprintf(keybuf, sizeof(keybuf), "pipeline:%s.%s", config_codec, (codec_tx_mode ? "tx" : "rx"));
   Log(LOG_DEBUG, "codec", "Selecting pipeline '%s' from config --", keybuf);

   const char *cfg_pipeline = cfg_get(keybuf);
   if (cfg_pipeline) {
      Log(LOG_DEBUG, "codec", "# %s", cfg_pipeline);
      au_cfg.pipeline = cfg_pipeline;
   } else {
      Log(LOG_CRIT, "fwdsp", "No pipeline configured");
      exit(1);
   }

   gst_init(&argc, &argv);
   gst_debug_add_log_function(gst_log_handler, NULL, NULL);

   time_t last_run = 0;
   do {
      now = time(NULL);
      run_loop(&au_cfg);
      printf("Run took %li sec", (now - last_run));
      last_run = now;
   } while(au_cfg.persistent);

   host_cleanup();
   return 0;
}

void shutdown_app(int signum) {
   exit(signum);
}
