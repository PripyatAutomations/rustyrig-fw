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
#include <pthread.h>
#include <FLAC/stream_encoder.h>
#if     !defined(__FWDSP)
#define	__FWDSP
#endif
#include <librustyaxe/core.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <fwdsp/fwdsp-shared.h>
#include <librrprotocol/cfg.fwdsp.h>

extern const char **configs;
extern const int num_configs;
extern defconfig_t defcfg[];
extern bool log_stdout;

const char *config_file = NULL;
const char *config_codec = "pc16";
static int control_fd = -1;
const char *logfile = "./fwdsp.log";
bool codec_tx_mode = false;
bool config_video = false;               // is this audio or video stream?
bool dying = false;
bool restarting = false;
bool empty_config = true;
static GstElement *pipeline = NULL;

time_t now = -1;                 // time() called once a second in main loop to
                                 // update

// Use the same section callbacks as rrclient/rrserver.  The child receives
// the parent application's config file with -f, so there is one source of
// truth for [fwdsp] settings and [pipelines].
extern bool config_fwdsp_section_cb(const char *path, int line,
   const char *section, const char *buf);
extern bool config_pipeline_section_cb(const char *path, int line,
   const char *section, const char *buf);

#define FWDSP_RECORD_RING_SIZE_DEFAULT (512U * 1024U)
#define FWDSP_RECORD_RING_SIZE_MIN     4096U

struct fwdsp_recorder {
   pthread_t thread;
   pthread_mutex_t lock;
   pthread_cond_t cond;
   uint8_t *ring;
   size_t ring_size;
   size_t read_pos;
   size_t write_pos;
   size_t used;
   bool running;
   bool stopping;
   bool thread_started;
   bool overflow_logged;
   unsigned sample_rate;
   unsigned channels;
   unsigned bits_per_sample;
   char filename[PATH_MAX];
};

static struct fwdsp_recorder recorder = { 0 };
static bool record_requested = false;
static char record_user[FWDSP_RECORD_USER_LEN] = "unknown";
static bool record_tx = false;

static void recorder_reset_ring(struct fwdsp_recorder *rec) {
   rec->read_pos = 0;
   rec->write_pos = 0;
   rec->used = 0;
   rec->overflow_logged = false;
}

// Reserve the name atomically: concurrent channels and quick restarts must
// never truncate an existing recording. The suffix is only a collision ID.
static FILE *recorder_open_file(struct fwdsp_recorder *rec) {
   char base[PATH_MAX];
   snprintf(base, sizeof(base), "%s", rec->filename);
   for (unsigned suffix = 0 ; suffix < 1000000 ; suffix++) {
      int len = suffix ?
         snprintf(rec->filename, sizeof(rec->filename), "%s.%u.flac", base, suffix) :
         snprintf(rec->filename, sizeof(rec->filename), "%s.flac", base);
      if (len < 0 || (size_t)len >= sizeof(rec->filename)) {
         errno = ENAMETOOLONG;
         return NULL;
      }
      int fd = open(rec->filename, O_CREAT | O_EXCL | O_RDWR, 0666);
      if (fd >= 0) {
         FILE *file = fdopen(fd, "w+b");
         if (!file) {
            int error = errno;
            close(fd);
            unlink(rec->filename);
            errno = error;
         }
         return file;
      }
      if (errno != EEXIST) {
         return NULL;
      }
   }
   errno = EEXIST;
   return NULL;
}

static void *recorder_thread_main(void *arg) {
   struct fwdsp_recorder *rec = arg;
   FLAC__StreamEncoder *enc = FLAC__stream_encoder_new();
   FLAC__int32 *samples = NULL;
   size_t samples_cap = 0;
   uint8_t chunk[8192];

   if (!enc) {
      Log(LOG_CRIT, "record", "Unable to allocate FLAC encoder");
      return NULL;
   }

   FLAC__stream_encoder_set_channels(enc, rec->channels);
   FLAC__stream_encoder_set_bits_per_sample(enc, rec->bits_per_sample);
   FLAC__stream_encoder_set_sample_rate(enc, rec->sample_rate);
   FLAC__stream_encoder_set_compression_level(enc, 3);

   FILE *file = recorder_open_file(rec);
   if (!file) {
      Log(LOG_CRIT, "record", "Unable to create recording %s: %s", rec->filename, strerror(errno));
      FLAC__stream_encoder_delete(enc);
      return NULL;
   }
   if (FLAC__stream_encoder_init_FILE(enc, file, NULL, NULL) != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
      Log(LOG_CRIT, "record", "Unable to initialize recording %s", rec->filename);
      // init_FILE transfers ownership even when initialization fails.
      FLAC__stream_encoder_delete(enc);
      return NULL;
   }

   Log(LOG_INFO, "record", "Recording to %s", rec->filename);

   for (;;) {
      size_t got = 0;

      pthread_mutex_lock(&rec->lock);
      while (rec->used == 0 && !rec->stopping) {
         pthread_cond_wait(&rec->cond, &rec->lock);
      }

      if (rec->used == 0 && rec->stopping) {
         pthread_mutex_unlock(&rec->lock);
         break;
      }

      size_t frame_bytes = (rec->bits_per_sample / 8U) * rec->channels;
      got = rec->used < sizeof(chunk) ? rec->used : sizeof(chunk);
      got -= got % frame_bytes;

      // If stopping with a partial sample left, discard only that impossible
      // tail rather than consuming aligned audio and losing bytes silently.
      if (got == 0) {
         if (rec->stopping) {
            rec->used = 0;
            pthread_mutex_unlock(&rec->lock);
            break;
         }
         pthread_mutex_unlock(&rec->lock);
         continue;
      }

      size_t first = rec->ring_size - rec->read_pos;
      if (first > got) {
         first = got;
      }
      memcpy(chunk, rec->ring + rec->read_pos, first);
      if (got > first) {
         memcpy(chunk + first, rec->ring, got - first);
      }
      rec->read_pos = (rec->read_pos + got) % rec->ring_size;
      rec->used -= got;
      pthread_mutex_unlock(&rec->lock);

      size_t frames = got / frame_bytes;
      size_t values = frames * rec->channels;

      if (values > samples_cap) {
         FLAC__int32 *tmp = realloc(samples, values * sizeof(*samples));
         if (!tmp) {
            Log(LOG_CRIT, "record", "OOM converting recording samples");
            break;
         }
         samples = tmp;
         samples_cap = values;
      }

      for (size_t i = 0 ; i < values ; i++) {
         uint16_t v = (uint16_t)chunk[i * 2] | ((uint16_t)chunk[i * 2 + 1] << 8);
         samples[i] = (int16_t)v;
      }

      if (frames > 0 && !FLAC__stream_encoder_process_interleaved(enc, samples, (unsigned)frames)) {
         Log(LOG_CRIT, "record", "FLAC encoder failed while writing %s", rec->filename);
         break;
      }
   }

   FLAC__stream_encoder_finish(enc);
   FLAC__stream_encoder_delete(enc);
   free(samples);
   Log(LOG_INFO, "record", "Closed recording %s", rec->filename);
   return NULL;
}

static bool recorder_start(unsigned sample_rate, unsigned channels, unsigned bits_per_sample) {
   const char *record_dir;
   struct tm tm_now;
   char stamp[32];

   if (recorder.running) {
      return true;
   }

   if (bits_per_sample != 16 || channels == 0 || sample_rate == 0) {
      Log(LOG_WARN, "record", "Unsupported raw recording format: %u Hz, %u ch, %u bit",
         sample_rate, channels, bits_per_sample);
      return false;
   }

   record_dir = cfg_get_exp("fwdsp.recording.path");
   if (!record_dir || !*record_dir) {
      free((char *)record_dir);
      record_dir = strdup("./recordings");
   }

   if (mkdir(record_dir, 0755) < 0 && errno != EEXIST) {
      Log(LOG_CRIT, "record", "Unable to create recording directory %s: %s",
         record_dir, strerror(errno));
      free((char *)record_dir);
      return false;
   }

   time_t record_now = time(NULL);
   localtime_r(&record_now, &tm_now);
   strftime(stamp, sizeof(stamp), "%Y%m%d.%H%M%S", &tm_now);
   char safe_user[FWDSP_RECORD_USER_LEN];
   snprintf(safe_user, sizeof(safe_user), "%s", record_user);
   for (char *p = safe_user ; *p ; p++) {
      if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) {
         *p = '_';
      }
   }
   int name_len = snprintf(recorder.filename, sizeof(recorder.filename), "%s/%s.%s.%s",
      record_dir, stamp, safe_user, record_tx ? "tx" : "rx");
   free((char *)record_dir);
   if (name_len < 0 || (size_t)name_len >= sizeof(recorder.filename)) {
      Log(LOG_CRIT, "record", "Recording path is too long");
      return false;
   }

   size_t ring_size = (size_t)cfg_get_int("fwdsp.recording.buffer-size", FWDSP_RECORD_RING_SIZE_DEFAULT);
   if (ring_size < FWDSP_RECORD_RING_SIZE_MIN) {
      ring_size = FWDSP_RECORD_RING_SIZE_MIN;
   }

   recorder.ring = malloc(ring_size);
   if (!recorder.ring) {
      Log(LOG_CRIT, "record", "Unable to allocate recording ring buffer");
      return false;
   }

   recorder.ring_size = ring_size;
   recorder.sample_rate = sample_rate;
   recorder.channels = channels;
   recorder.bits_per_sample = bits_per_sample;
   recorder.stopping = false;
   recorder.running = true;
   recorder_reset_ring(&recorder);
   pthread_mutex_init(&recorder.lock, NULL);
   pthread_cond_init(&recorder.cond, NULL);

   int thread_rc = pthread_create(&recorder.thread, NULL, recorder_thread_main, &recorder);
   if (thread_rc != 0) {
      Log(LOG_CRIT, "record", "Unable to start recording thread: %s", strerror(thread_rc));
      pthread_cond_destroy(&recorder.cond);
      pthread_mutex_destroy(&recorder.lock);
      free(recorder.ring);
      memset(&recorder, 0, sizeof(recorder));
      return false;
   }

   recorder.thread_started = true;
   return true;
}

static void recorder_stop(void) {
   if (!recorder.running) {
      return;
   }

   pthread_mutex_lock(&recorder.lock);
   recorder.stopping = true;
   pthread_cond_signal(&recorder.cond);
   pthread_mutex_unlock(&recorder.lock);

   if (recorder.thread_started) {
      pthread_join(recorder.thread, NULL);
   }

   pthread_cond_destroy(&recorder.cond);
   pthread_mutex_destroy(&recorder.lock);
   free(recorder.ring);
   memset(&recorder, 0, sizeof(recorder));
}

static void recorder_write(const uint8_t *data, size_t len) {
   if (!recorder.running || !data || len == 0) {
      return;
   }

   pthread_mutex_lock(&recorder.lock);
   size_t free_space = recorder.ring_size - recorder.used;
   if (len > free_space) {
      if (!recorder.overflow_logged) {
         Log(LOG_WARN, "record", "Recording ring overflow; dropping audio until writer catches up");
         recorder.overflow_logged = true;
      }
      pthread_mutex_unlock(&recorder.lock);
      return;
   }

   size_t first = recorder.ring_size - recorder.write_pos;
   if (first > len) {
      first = len;
   }
   memcpy(recorder.ring + recorder.write_pos, data, first);
   if (len > first) {
      memcpy(recorder.ring, data + first, len - first);
   }
   recorder.write_pos = (recorder.write_pos + len) % recorder.ring_size;
   recorder.used += len;
   recorder.overflow_logged = false;
   pthread_cond_signal(&recorder.cond);
   pthread_mutex_unlock(&recorder.lock);
}

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

static bool write_frame(int fd, const uint8_t *data, size_t len, bool is_header) {
   uint8_t header[FWDSP_FRAME_HEADER_SIZE];

   if (!data || len == 0 || len > UINT32_MAX || len > FWDSP_MAX_FRAME_SIZE) {
      return false;
   }

   frame_length_encode(header, (uint32_t)len | (is_header ? FWDSP_FRAME_STREAM_HEADER : 0));

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

      GstElement *record_sink = gst_bin_get_by_name(GST_BIN(pipeline), "record-sink");
      if (record_sink && !GST_IS_APP_SINK(record_sink)) {
         gst_object_unref(record_sink);
         record_sink = NULL;
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

         if (record_sink) {
            gst_object_unref(record_sink);
         }
         return;
      }

      GstBus *bus = gst_element_get_bus(pipeline);
      struct fwdsp_frame_reader input_reader = { 0 };
      struct fwdsp_control_msg control;
      size_t control_used = 0;
      record_tx = cfg->tx_mode;
      record_requested = false;   // application supplies recording policy and identity
      bool pipeline_paused = false;

      while (!dying) {

         if (appsink) {
            GstSample *sample;
            while ((sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 0))) {
               GstBuffer *buffer = gst_sample_get_buffer(sample);
               GstMapInfo map;

               if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                  if (!write_frame(STDOUT_FD, map.data, map.size,
                      GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_HEADER))) {
                     dying = true;
                  }
                  gst_buffer_unmap(buffer, &map);
               }
               gst_sample_unref(sample);
            }
         }

         // Support for recording TX (and optionally RX) audio to FLAC files
         if (record_sink) {
            GstSample *record_sample;
            while ((record_sample = gst_app_sink_try_pull_sample(GST_APP_SINK(record_sink), 0))) {
               GstBuffer *record_buffer = gst_sample_get_buffer(record_sample);
               GstCaps *caps = gst_sample_get_caps(record_sample);
               GstMapInfo record_map;
               unsigned rate = 16000;
               unsigned channels = 1;
               bool format_ok = false;

               if (caps) {
                  GstStructure *st = gst_caps_get_structure(caps, 0);
                  int tmp = 0;
                  const char *format = gst_structure_get_string(st, "format");
                  format_ok = format && strcmp(format, "S16LE") == 0;
                  if (gst_structure_get_int(st, "rate", &tmp) && tmp > 0) {
                     rate = (unsigned)tmp;
                  }
                  if (gst_structure_get_int(st, "channels", &tmp) && tmp > 0) {
                     channels = (unsigned)tmp;
                  }
               }

               if (record_requested && !format_ok) {
                  Log(LOG_WARN, "record", "record-sink must provide audio/x-raw,format=S16LE");
                  record_requested = false;
               }

               if (record_requested && !recorder.running) {
                  recorder_start(rate, channels, 16);
               }

               if (record_requested && recorder.running && record_buffer &&
                   gst_buffer_map(record_buffer, &record_map, GST_MAP_READ)) {
                  recorder_write(record_map.data, record_map.size);
                  gst_buffer_unmap(record_buffer, &record_map);
               }
               gst_sample_unref(record_sample);
            }
         }

         GstMessage *msg = gst_bus_timed_pop_filtered(bus, 0, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

         if (msg) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
               GError *err;
               gchar *dbg;
               gst_message_parse_error(msg, &err, &dbg);
               fprintf(stderr, "fwdsp: GStreamer error: %s\n", err->message);
               if (dbg) {
                  fprintf(stderr, "fwdsp: GStreamer details: %s\n", dbg);
               }
               g_error_free(err);
               g_free(dbg);
            } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
               fprintf(stderr, "fwdsp: GStreamer EOS received\n");
            }
            dying = true;
            gst_message_unref(msg);
            break;
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
            if (poll(&control_poll, 1, 0) > 0) {
               ssize_t got = read(control_fd, (uint8_t *)&control + control_used,
                  sizeof(control) - control_used);
               if (got > 0) {
                  control_used += (size_t)got;
               }
            }
            if (control_used == sizeof(control)) {
               control_used = 0;
               if (control.magic != FWDSP_CTRL_MAGIC) {
                  Log(LOG_WARN, "fwdsp", "Invalid control message magic");
                  dying = true;
                  continue;
               }
               switch (control.type) {
                  case FWDSP_CTRL_SET_VOLUME:
                     if (volume) {
                        g_object_set(G_OBJECT(volume), "volume", control.value / 100.0, NULL);
                     }
                     break;

                  case FWDSP_CTRL_START_RECORD:
                     if (!record_sink) {
                        Log(LOG_WARN, "record",
                           "Recording requested but pipeline %s.%s has no appsink name=record-sink",
                           config_codec, codec_tx_mode ? "tx" : "rx");
                     } else {
                        control.record_user[sizeof(control.record_user) - 1] = '\0';
                        const char *who = control.record_user[0] ? control.record_user : "unknown";
                        bool tx = control.record_direction ? control.record_direction == 2 : cfg->tx_mode;
                        if (recorder.running && (strcmp(record_user, who) != 0 || record_tx != tx)) {
                           recorder_stop();
                        }
                        snprintf(record_user, sizeof(record_user), "%s", who);
                        record_tx = tx;
                        record_requested = true;
                     }
                     break;

                  case FWDSP_CTRL_STOP_RECORD:
                     record_requested = false;
                     recorder_stop();
                     break;

                  case FWDSP_CTRL_PAUSE:
                     if (gst_element_set_state(pipeline, GST_STATE_PAUSED) != GST_STATE_CHANGE_FAILURE) {
                        pipeline_paused = true;
                     }
                     break;

                  case FWDSP_CTRL_RESUME:
                     if (gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) {
                        pipeline_paused = false;
                     }
                     break;

                  case FWDSP_CTRL_FLUSH:
                     gst_element_send_event(pipeline, gst_event_new_flush_start());
                     gst_element_send_event(pipeline, gst_event_new_flush_stop(TRUE));
                     break;

                  case FWDSP_CTRL_SHUTDOWN:
                     dying = true;
                     break;

                  default:
                     Log(LOG_WARN, "fwdsp", "Unknown control message type %u", control.type);
                     break;
               }
            }
         }
         // Service input/control promptly without sleeping once per audio packet.
         // Encoders may produce several packets from one capture buffer.
         struct pollfd pending[2] = {
            { .fd = appsrc ? STDIN_FD : -1, .events = POLLIN },
            { .fd = control_fd, .events = POLLIN }
         };
         int wait_ms = pipeline_paused ? 100 : 2;
         int poll_rc = poll(pending, 2, wait_ms);
         if (poll_rc > 0 && pending[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            dying = true;
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

      if (record_sink) {
         gst_object_unref(record_sink);
      }

      recorder_stop();
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

   const char *parent_pipeline = NULL;
   int opt;
   while ( (opt = getopt(argc, argv, "C:c:f:p:htv") ) != -1) {
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
         case 'p': {
            parent_pipeline = *optarg ? optarg : NULL;
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
            fprintf(stderr, "  -p\t\t\tPipeline selected by the parent (overrides config/defaults)\n");
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
   cfg_add_callback(NULL, "fwdsp", config_fwdsp_section_cb);
   cfg_add_callback(NULL, "pipeline", config_pipeline_section_cb);
   cfg_add_callback(NULL, "pipelines", config_pipeline_section_cb);

   // If the user specified a config, apply it, else try to find one in a sane
   // place
   if (config_file) {
      if (!(cfg = cfg_load(config_file) ) ) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", config_file);
      } else {
         Log(LOG_DEBUG, "config", "Loaded config from '%s'", config_file);
      }
   } else {
      Log(LOG_CRIT, "core",
         "fwdsp requires -f with the parent application's config file");
      exit(1);
   }
   const char *logfile = cfg_get_exp("fwdsp.log.file");
   logger_init( (logfile ? logfile : "-"), false);
   log_stdout = false;
   if (logfp == stdout) {
      // stdout is framed media, even when logging is configured as "-".
      logfp = stderr;
   }

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
   const char *cfg_audio_debug = cfg_get("fwdsp.audio.debug");

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

   const char *cfg_pipeline = parent_pipeline ? parent_pipeline : cfg_get(keybuf);
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
