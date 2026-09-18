// fwdsp-main.c: firmware DSP bridge This is part of rustyrig-fw.
//    https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
//
// Here we handle moving audio between gstreamer and the firmwre.
// You will typically have two instances running of fwdsp.bin
// One with the -t argument and another without, for TX and RX, respectively
//
// XXX: We need to try our best to stay running after errors
// XXX: - Auto-reconnect, with increasing backoff
//
// BUGS: A gstreamer wizard could certainly make this a lot better.. Feel free
// to jump in! ;)
//
// src/rrserver/fwdsp-mgr.c handles spawning and stopping (en|de)coders as
// needed
//
#include <stdint.h>
#include <limits.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/stat.h>
#if     !defined(__FWDSP)
#define	__FWDSP
#endif
#include <librustyaxe/core.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <fwdsp/fwdsp-shared.h>

extern const char **configs;
extern const int num_configs;
extern defconfig_t defcfg[];
extern bool log_stdout;

const char *config_file = NULL;
const char *config_codec = "pc16";
static int control_fd = -1;
const char *logfile = "./fwdsp.log";

// Store [pipeline] section keys as pipeline:<codec>.<dir> -- the format
// looked up by cfg_get() below. Mirrors rrserver/cfg.fwdsp.c
// config_pipeline_section_cb().
static bool config_pipeline_section_cb(const char *path, int line, const char *section, const char *buf) {
   if (!buf || section == NULL || strncasecmp(section, "pipeline", 8) != 0) {
      return true;
   }

   char *tmpbuf = strdup(buf);
   if (!tmpbuf) {
      Log(LOG_CRIT, "cfg.fwdsp", "OOM in config_pipeline_section_cb!");
      return true;
   }

   char *val = strchr(tmpbuf, '=');
   if (!val || !val[1]) {
      Log(LOG_CRIT, "cfg.fwdsp", "config error: pipeline entry missing value: %s", buf);
      free(tmpbuf);
      return false;
   }
   *val++ = '\0';
   while (*val == ' ' || *val == '\t') {
      val++;
   }

   // trim trailing whitespace
   char *end = val + strlen(val) - 1;
   while (end >= val && (*end == ' ' || *end == '\t')) {
      *end-- = '\0';
   }

   // trim key whitespace
   char *kend = tmpbuf + strlen(tmpbuf) - 1;
   while (kend >= tmpbuf && (*kend == ' ' || *kend == '\t')) {
      *kend-- = '\0';
   }

   // Accept both "pc16.rx" and "pipeline:pc16.rx" spellings
   const char *id = tmpbuf;
   if (strncmp(id, "pipeline:", 9) == 0) {
      id += 9;
   }

   char fullkey[128];
   snprintf(fullkey, sizeof(fullkey), "pipeline:%s", id);
   dict_add(cfg, fullkey, val);
   Log(LOG_DEBUG, "cfg.fwdsp", "Loaded %s from config", fullkey);
   free(tmpbuf);
   return false;
}

bool codec_tx_mode = false;
bool config_video = false;               // is this audio or video stream?
bool dying = false;
bool empty_config = true;
static GstElement *pipeline = NULL;
time_t now = -1;                 // time() called once a second in main loop to
                                 // update

static void cleanup_pipeline(GstElement **pipe) {
   if (*pipe) {
      gst_element_set_state(*pipe, GST_STATE_NULL);
      gst_object_unref(*pipe);
      *pipe = NULL;
   }
}

static GstElement *build_pipeline(const char *pipeline_str) {
   return gst_parse_launch(pipeline_str, NULL);
}

static bool write_all(int fd, const uint8_t *data, size_t len) {
   while (len > 0) {
      ssize_t written = write(fd, data, len);

      if (written > 0) {
         data += written;
         len -= (size_t)written;
      } else if (written < 0 && errno == EINTR) {
         continue;
      } else {
         return false;
      }
   }
   return true;
}

#define FWDSP_FRAME_HEADER_SIZE 4
#define FWDSP_MAX_FRAME_SIZE (64U * 1024U * 1024U)
#define FWDSP_READER_MAX_BUFFER (FWDSP_MAX_FRAME_SIZE + 4096U)

static void frame_length_encode(uint8_t header[FWDSP_FRAME_HEADER_SIZE], uint32_t len) {
   header[0] = (uint8_t)(len >> 24);
   header[1] = (uint8_t)(len >> 16);
   header[2] = (uint8_t)(len >> 8);
   header[3] = (uint8_t)len;
}

static uint32_t frame_length_decode(const uint8_t header[FWDSP_FRAME_HEADER_SIZE]) {
   return ((uint32_t)header[0] << 24) |
          ((uint32_t)header[1] << 16) |
          ((uint32_t)header[2] << 8) |
          (uint32_t)header[3];
}

static bool write_frame(int fd, const uint8_t *data, size_t len) {
   uint8_t header[FWDSP_FRAME_HEADER_SIZE];

   if (!data || len == 0 || len > UINT32_MAX || len > FWDSP_MAX_FRAME_SIZE) {
      return false;
   }

   frame_length_encode(header, (uint32_t)len);

   if (!write_all(fd, header, sizeof(header))) {
      return false;
   }

   return write_all(fd, data, len);
}

struct fwdsp_frame_reader {
   uint8_t *buf;
   size_t len;
   size_t cap;
};

static void frame_reader_clear(struct fwdsp_frame_reader *reader) {
   if (!reader) {
      return;
   }

   free(reader->buf);
   reader->buf = NULL;
   reader->len = 0;
   reader->cap = 0;
}

static bool frame_reader_append(struct fwdsp_frame_reader *reader,
   const uint8_t *data, size_t len) {
   if (!reader || !data || len == 0) {
      return false;
   }

   if (len > FWDSP_READER_MAX_BUFFER ||
       reader->len > FWDSP_READER_MAX_BUFFER - len) {
      return false;
   }

   size_t needed = reader->len + len;

   if (needed > reader->cap) {
      size_t newcap = reader->cap ? reader->cap : 4096;

      while (newcap < needed) {
         if (newcap > FWDSP_READER_MAX_BUFFER / 2) {
            newcap = FWDSP_READER_MAX_BUFFER;
            break;
         }
         newcap *= 2;
      }

      uint8_t *newbuf = realloc(reader->buf, newcap);
      if (!newbuf) {
         return false;
      }

      reader->buf = newbuf;
      reader->cap = newcap;
   }

   memcpy(reader->buf + reader->len, data, len);
   reader->len += len;
   return true;
}

static bool frame_reader_push_appsrc(struct fwdsp_frame_reader *reader,
   GstAppSrc *appsrc) {
   while (reader->len >= FWDSP_FRAME_HEADER_SIZE) {
      uint32_t frame_len = frame_length_decode(reader->buf);

      if (frame_len == 0 || frame_len > FWDSP_MAX_FRAME_SIZE) {
         Log(LOG_CRIT, "fwdsp", "Invalid framed input length %u", frame_len);
         return false;
      }

      size_t total_len = FWDSP_FRAME_HEADER_SIZE + (size_t)frame_len;
      if (reader->len < total_len) {
         return true;
      }

      GstBuffer *buffer = gst_buffer_new_allocate(NULL, frame_len, NULL);
      if (!buffer) {
         return false;
      }

      gst_buffer_fill(buffer, 0, reader->buf + FWDSP_FRAME_HEADER_SIZE,
         frame_len);

      if (gst_app_src_push_buffer(appsrc, buffer) != GST_FLOW_OK) {
         return false;
      }

      reader->len -= total_len;
      if (reader->len > 0) {
         memmove(reader->buf, reader->buf + total_len, reader->len);
      }
   }

   return true;
}

#define	STDIN_FD 0
#define	STDOUT_FD 1

static void run_loop(struct audio_config *cfg) {
   while (1) {
      dying = false;
      Log(LOG_DEBUG, "fwdsp", "Starting %s.%s pipeline", config_codec, cfg->tx_mode ? "tx" : "rx");

      pipeline = build_pipeline(cfg->pipeline);

      if (!pipeline) {
         fprintf(stderr, "fwdsp: Failed to build pipeline\n");
         cleanup_pipeline(&pipeline);
         sleep(1);
         continue;
      }

      GstElement *appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "rx-src");
      if (!appsrc) {
         appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "tx-src");
      }
      if (appsrc && !GST_IS_APP_SRC(appsrc)) {
         gst_object_unref(appsrc);
         appsrc = NULL;
      }

      GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), "rx-sink");
      if (!appsink) {
         appsink = gst_bin_get_by_name(GST_BIN(pipeline), "tx-sink");
      }
      if (appsink && !GST_IS_APP_SINK(appsink)) {
         gst_object_unref(appsink);
         appsink = NULL;
      }

      GstElement *volume = gst_bin_get_by_name(GST_BIN(pipeline), "rx-vol");
      GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
      if (ret == GST_STATE_CHANGE_FAILURE) {
         g_printerr("Failed to set pipeline to PLAYING state.\n");
         cleanup_pipeline(&pipeline);

         if (appsrc) {
            gst_object_unref(appsrc);
         }

         if (appsink) {
            gst_object_unref(appsink);
         }

         if (volume) {
            gst_object_unref(volume);
         }
         return;
      }

      GstBus *bus = gst_element_get_bus(pipeline);
      struct fwdsp_frame_reader input_reader = { 0 };

      while (!dying) {
         GstMessage *msg = gst_bus_timed_pop_filtered(bus, 10 * GST_MSECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

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
            break;
         }

         if (appsink) {
            GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 10 * GST_MSECOND);

            if (sample) {
               GstBuffer *buffer = gst_sample_get_buffer(sample);
               GstMapInfo map;

               if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                  if (!write_frame(STDOUT_FD, map.data, map.size)) {
                     dying = true;
                  }
                  gst_buffer_unmap(buffer, &map);
               }
               gst_sample_unref(sample);
            }
         }

         if (appsrc) {
            struct pollfd input_poll = { .fd = STDIN_FD, .events = POLLIN };
            if (poll(&input_poll, 1, 0) > 0 && (input_poll.revents & (POLLIN | POLLHUP))) {
               uint8_t input[4096];
               ssize_t bytes = read(STDIN_FD, input, sizeof(input));

               if (bytes > 0) {
                  if (!frame_reader_append(&input_reader, input, (size_t)bytes) ||
                      !frame_reader_push_appsrc(&input_reader, GST_APP_SRC(appsrc))) {
                     dying = true;
                  }
               } else if (bytes == 0) {
                  gst_app_src_end_of_stream(GST_APP_SRC(appsrc));
                  dying = true;
               } else if (errno != EINTR && errno != EAGAIN) {
                  dying = true;
               }
            }
         }

         if (control_fd >= 0) {
            struct pollfd control_poll = { .fd = control_fd, .events = POLLIN };
            struct fwdsp_control_msg control;

            if (poll(&control_poll, 1, 0) > 0 &&
                read(control_fd, &control, sizeof(control)) == (ssize_t)sizeof(control) &&
                control.magic == FWDSP_CTRL_MAGIC &&
                control.type == FWDSP_CTRL_SET_VOLUME && volume) {
               g_object_set(G_OBJECT(volume), "volume", control.value / 100.0, NULL);
            }
         }
      }
      gst_object_unref(bus);
      if (appsrc) {
         gst_object_unref(appsrc);
      }

      if (appsink) {
         gst_object_unref(appsink);
      }

      if (volume) {
         gst_object_unref(volume);
      }

      frame_reader_clear(&input_reader);
      cleanup_pipeline(&pipeline);

      if (!cfg->persistent) {
         return;
      }
   }
}

static void gst_log_handler(GstDebugCategory *category, GstDebugLevel level, const gchar *file, const gchar *function,
                            gint line, GObject *object, GstDebugMessage *message, gpointer user_data) {
   g_printerr( "GST %s: %s\n", gst_debug_level_get_name(level), gst_debug_message_get(message) );
}

int main(int argc, char *argv[]) {
   int saved_stdout = dup(STDOUT_FD);
   int null_stdout = open("/dev/null", O_WRONLY);

   if (saved_stdout >= 0 && null_stdout >= 0) {
      dup2(null_stdout, STDOUT_FD);
   }

   if (null_stdout >= 0) {
      close(null_stdout);
   }

   host_init();
   log_stdout = false;

#ifdef USE_COREDUMPS_FWDSP
   struct rlimit rl = {
      .rlim_cur = RLIM_INFINITY,
      .rlim_max = RLIM_INFINITY
   };
   setrlimit(RLIMIT_CORE, &rl);
#else
   struct rlimit rl = {
      0, 0
   };
   setrlimit(RLIMIT_CORE, &rl);
#endif // USE_COREDUMPS_FWDSP

   // Logging MUST go to stderr!
   logfp = stderr;
   now = time(NULL);

   int opt;
   while ( (opt = getopt(argc, argv, "C:c:f:htv") ) != -1) {
      switch (opt) {
         case 'C': {
            control_fd = atoi(optarg);
            break;
         }
         case 'c': {
            size_t clen = strlen(optarg);

            if (clen != 4) {
               fprintf(stderr, "Codec magic (-c) '%s' *must* be exactly 4 characters\n", optarg);
               exit(1);
            } else {
               fprintf(stderr, "Setting codec magic to %s\n", optarg);
               config_codec = strdup(optarg);
            }
            break;
         }
         case 'f': {
            config_file = strdup(optarg);
            break;
         }
         case 't': {
            codec_tx_mode = true;
            break;
         }
         case 'v': {
            config_video = true;
            break;
         }
         case 'h':
         default: {
            fprintf(stderr, "Usage: %s [-f config file] [-c codec-string] [-t]\n", argv[0]);
            fprintf(stderr, "  -c\t\t\tIs the codec id such as PCM16 or MU44\n");
            fprintf(stderr, "  -f\t\t\tFile name of config\n");
            fprintf(stderr, "  -t\t\t\tTransmit mode\n");
            fprintf(stderr, "  -v\t\t\tVideo mode\n");
            exit(1);
         }
      }
   }
   Log(LOG_INFO, "fwdsp", "Starting fwdsp v.%s", VERSION);
   // Find and load the configuration file
   int cfg_entries = (sizeof(configs) / sizeof(char *) );
   default_cfg = dict_new();
   cfg_set_defaults(default_cfg, defcfg);
   cfg_add_callback(NULL, "pipeline", config_pipeline_section_cb);

   // If the user specified a config, apply it, else try to find one in a sane
   // place
   if (config_file) {
      if (!(cfg = cfg_load(config_file) ) ) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", config_file);
      } else {
         Log(LOG_DEBUG, "config", "Loaded config from '%s'", config_file);
      }
   } else {
      const char *fullpath = "config/fwdsp.cfg";

      if (fullpath) {
         config_file = strdup(fullpath);
         if (!(cfg = cfg_load(config_file) ) ) {
            Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", fullpath);
         } else {
            Log(LOG_DEBUG, "config", "Loaded config from '%s'", fullpath);
         }
         empty_config = false;
      } else {
         // Use default settings and save it to ~/.config/rrclient.cfg
         cfg = default_cfg;
         empty_config = true;
         fprintf(stderr, "No config found :(\n");
         exit(1);
      }
   }
   const char *logfile = cfg_get_exp("log.file");
   logger_init( (logfile ? logfile : "-"), false);
   log_stdout = false;

   if (saved_stdout >= 0) {
      dup2(saved_stdout, STDOUT_FD);
      close(saved_stdout);
   }

   if (logfile) {
      free( (char *)logfile );     // _exp versions MUST be freed
      logfile = NULL;
   }

   // Set up some debugging
   setenv("GST_DEBUG_DUMP_DOT_DIR", ".", 0);
   const char *cfg_audio_debug = cfg_get("fwdsp:audio.debug");

   if (cfg_audio_debug) {
      setenv("GST_DEBUG", cfg_audio_debug, 0);
   }
   // codec_mapping_t *au_codec_find_by_magic(magic);

   struct audio_config au_cfg = {
      .pipeline = NULL,
      .sample_rate = 16000,
      .format = 0,
      .tx_mode = codec_tx_mode,
      .channel_id = -1
   };

   // set sane defaults
   if (au_cfg.tx_mode) {
      au_cfg.sock_path = DEFAULT_SOCKET_PATH_TX;
      au_cfg.media_direction = FW_DIR_TX;
   } else {
      au_cfg.sock_path = DEFAULT_SOCKET_PATH_RX;
      au_cfg.media_direction = FW_DIR_RX;
   }

   if (au_cfg.channel_id < 0) {
      au_cfg.channel_id = 0;
   }

   char keybuf[256];
   memset( keybuf, 0, sizeof(keybuf) );
   snprintf( keybuf, sizeof(keybuf), "pipeline:%s.%s", config_codec, (codec_tx_mode ? "tx" : "rx") );
   Log(LOG_DEBUG, "codec", "Selecting pipeline '%s' from config --", keybuf);

   const char *cfg_pipeline = cfg_get(keybuf);
   if (cfg_pipeline) {
      Log(LOG_DEBUG, "codec", "-> full pipeline:\t%s", cfg_pipeline);
      au_cfg.pipeline = cfg_pipeline;
   } else {
      Log(LOG_CRIT, "fwdsp", "No pipeline configured for codec id %s", config_codec);
      exit(1);
   }

   // unless set to video, treat it as audio frames
   if (config_video) {
      au_cfg.media_type = FW_MEDIA_VIDEO;
   } else {
      au_cfg.media_type = FW_MEDIA_AUDIO;
   }
   // set up gstreamer
   gst_init(&argc, &argv);
   gst_debug_add_log_function(gst_log_handler, NULL, NULL);

   time_t last_run = 0;
   do {
      now = time(NULL);
      run_loop(&au_cfg);
      fprintf( stderr, "Run took %li sec", (now - last_run) );
      last_run = now;
   } while (au_cfg.persistent);

   null_stdout = open("/dev/null", O_WRONLY);
   if (null_stdout >= 0) {
      dup2(null_stdout, STDOUT_FD);
      close(null_stdout);
   }
   host_cleanup();

   return 0;
}

void shutdown_app(int signum) {
   exit(signum);
}
