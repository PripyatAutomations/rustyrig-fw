// Client-side PCM recording and playback. GPL-2.0-only.
#include "recordings.h"
#include "recordings.codec.h"
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#define read _read
#define write _write
#define close _close
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#endif
#include <errno.h>
#include <stdio.h>

#define RATE 16000
#define QUEUE_FRAMES (RATE * 4)
typedef struct {
   GMutex lock;
   GCond ready;
   int16_t ring[QUEUE_FRAMES * 2];
   size_t head, used;
   gboolean recording, stopping;
   GThread *worker;
   rr_recording_tags tags;
   char *ogg, *flac, *id;
   guint64 dropped;
} Recorder;
/* Snapshot metadata before handing the request to the encoder thread.
 * Never retain GTK-owned strings beyond the initiating callback. */
static void recording_tags_copy(rr_recording_tags *dst, const rr_recording_tags *src) {
   dst->title = g_strdup(src->title);
   dst->user = g_strdup(src->user);
   dst->start_time = g_strdup(src->start_time);
   dst->site = g_strdup(src->site);
   dst->server = g_strdup(src->server);
   dst->rig = g_strdup(src->rig);
   dst->vfo = g_strdup(src->vfo);
   dst->frequency = g_strdup(src->frequency);
   dst->mode = g_strdup(src->mode);
   dst->grid = g_strdup(src->grid);
   dst->latitude = g_strdup(src->latitude);
   dst->longitude = g_strdup(src->longitude);
   dst->location_source = g_strdup(src->location_source);
   dst->sources = g_strdup(src->sources);
}
static void recording_tags_clear(rr_recording_tags *tags) {
   g_free((char *)tags->title);
   g_free((char *)tags->user);
   g_free((char *)tags->start_time);
   g_free((char *)tags->site);
   g_free((char *)tags->server);
   g_free((char *)tags->rig);
   g_free((char *)tags->vfo);
   g_free((char *)tags->frequency);
   g_free((char *)tags->mode);
   g_free((char *)tags->grid);
   g_free((char *)tags->latitude);
   g_free((char *)tags->longitude);
   g_free((char *)tags->location_source);
   g_free((char *)tags->sources);
   memset(tags, 0, sizeof(*tags));
}
static Recorder rec;
static gsize initialized;
static GMutex playback_lock;
static GCond playback_ready;
static gboolean playback_stop;
static GThread *playback_thread;
static char *playback_path;
static rr_recording_playback_fn playback_output;
static GMutex wave_lock;
static GCond wave_finished;
static guint64 wave_generation;
static unsigned wave_workers;
static gboolean wave_shutting_down;
static gboolean playback_seek, playback_reverse, playback_paused;
static double playback_target;

static void ensure_init(void) {
   if (g_once_init_enter(&initialized)) {
      g_mutex_init(&rec.lock);
      g_cond_init(&rec.ready);
      g_mutex_init(&playback_lock);
      g_cond_init(&playback_ready);
      g_mutex_init(&wave_lock);
      g_cond_init(&wave_finished);
      g_once_init_leave(&initialized, 1);
   }
}
char *rr_recordings_directory(void) {
   const char *home = g_get_home_dir();
   char *dir = g_build_filename(home, ".local", "share", "rustyrig", "recordings", NULL);
   g_mkdir_with_parents(dir, 0700);
   return dir;
}
static char *safe(const char *text) {
   char *s = g_strdup(text && *text ? text : "unknown");
   for (char *p = s; *p; p++)
      if (!g_ascii_isalnum(*p) && !strchr("._-", *p)) *p = '_';
   return s;
}
/* Human-readable filenames, while keeping exact Hz/mode/site in Ogg/FLAC tags.
 * Never concatenate an unknown CAT mode onto a naked frequency integer. */
static char *recording_frequency(const char *hz_text) {
   if (!hz_text || !*hz_text) return NULL;
   char *end = NULL;
   guint64 hz = g_ascii_strtoull(hz_text, &end, 10);
   if (end == hz_text || *end || !hz) return NULL;
   double value = hz >= 1000000 ? (double)hz / 1000000.0 :
                  hz >= 1000 ? (double)hz / 1000.0 : (double)hz;
   const char *unit = hz >= 1000000 ? "MHz" : hz >= 1000 ? "kHz" : "Hz";
   char number[G_ASCII_DTOSTR_BUF_SIZE];
   g_ascii_formatd(number, sizeof(number), "%.6f", value);
   char *dot = strchr(number, '.');
   if (dot) {
      char *p = number + strlen(number) - 1;
      while (p > dot && *p == '0') *p-- = 0;
      if (p == dot) *p = 0;
   }
   return g_strdup_printf("%s %s", number, unit);
}
static char *recording_stem(const rr_recording_tags *tags, const char *stamp) {
   char *frequency = recording_frequency(tags->frequency);
   const char *mode = tags->mode && *tags->mode &&
      g_ascii_strcasecmp(tags->mode, "unknown") ? tags->mode : NULL;
   const char *site = tags->site && *tags->site ? tags->site : NULL;
   const char *rig = tags->rig && *tags->rig ? tags->rig : NULL;
   const char *vfo = tags->vfo && *tags->vfo ? tags->vfo : NULL;
   gboolean generic = (!site || !strcmp(site, "local")) &&
      (!rig || !strcmp(rig, "receiver"));
   char *location = generic ? g_strdup("Local monitor") :
      g_strdup_printf("%s%s%s%s%s", site ? site : "Remote",
         rig ? "." : "", rig ? rig : "",
         vfo && *vfo ? "." : "", vfo ? vfo : "");
   char *safe_location = safe(location);
   char *safe_mode = mode ? safe(mode) : NULL;
   char *stem = g_strdup_printf("%s - %s%s%s - %s - RX", stamp,
      frequency ? frequency : "Audio",
      safe_mode ? " " : "", safe_mode ? safe_mode : "", safe_location);
   g_free(frequency); g_free(location); g_free(safe_location); g_free(safe_mode);
   return stem;
}
static char *make_path(const char *base, const char *suffix) {
   char *dir = rr_recordings_directory();
   char *path = g_strdup_printf("%s/%s.%s", dir, base, suffix);
   g_free(dir);
   return path;
}
/* Append the finish timestamp after encoding; both formats share the metadata ID. */
static char *rename_with_end(const char *oldpath, const char *stamp) {
   if (!oldpath || !*oldpath) return NULL;
   const char *ext = strrchr(oldpath, '.');
   if (!ext) return g_strdup(oldpath);
   char *prefix = g_strndup(oldpath, ext - oldpath);
   /* Start carries the date; show end time without repeating that date. */
   const char *time = strchr(stamp, '_');
   char *newpath = g_strdup_printf("%s - end-%s%s", prefix,
      time ? time + 1 : stamp, ext);
   if (g_rename(oldpath, newpath)) {
      g_free(newpath);
      newpath = g_strdup(oldpath);
   }
   g_free(prefix);
   return newpath;
}
static gpointer recorder_worker(gpointer unused) {
   (void)unused;
   int16_t block[1024 * 2];
   rr_recording_encoder *encoder = rr_recording_encoder_open(rec.ogg, rec.flac, &rec.tags, rec.id);
   recording_tags_clear(&rec.tags);
   gboolean ok = encoder != NULL;
   if (!ok) g_warning("Unable to initialize native Ogg/FLAC recording encoder");
   while (ok) {
      g_mutex_lock(&rec.lock);
      while (!rec.used && !rec.stopping) g_cond_wait(&rec.ready, &rec.lock);
      size_t n = MIN(rec.used, (size_t)1024);
      size_t first = MIN(n, (size_t)(QUEUE_FRAMES - rec.head));
      memcpy(block, rec.ring + rec.head * 2, first * 2 * sizeof(int16_t));
      if (n > first) memcpy(block + first * 2, rec.ring, (n - first) * 2 * sizeof(int16_t));
      rec.head = (rec.head + n) % QUEUE_FRAMES;
      rec.used -= n;
      gboolean done = rec.stopping && n == 0;
      g_mutex_unlock(&rec.lock);
      if (done) break;
      if (n && !rr_recording_encoder_write(encoder, block, n)) {
         ok = FALSE;
         break;
      }
   }
   if (encoder && !rr_recording_encoder_close(encoder)) ok = FALSE;
   if (ok) {
      GDateTime *end_time = g_date_time_new_now_local();
      char *end_stamp = g_date_time_format(end_time, "%Y-%m-%d_%H-%M-%S");
      if (rec.ogg) {
         char *renamed = rename_with_end(rec.ogg, end_stamp);
         g_free(rec.ogg);
         rec.ogg = renamed;
      }
      if (rec.flac) {
         char *renamed = rename_with_end(rec.flac, end_stamp);
         g_free(rec.flac);
         rec.flac = renamed;
      }
      g_free(end_stamp);
      g_date_time_unref(end_time);
   }
   g_mutex_lock(&rec.lock);
   rec.recording = FALSE;
   rec.stopping = FALSE;
   g_mutex_unlock(&rec.lock);
   // The event includes paths, status and drop count; the GTK subscriber owns its UI changes.
   char *info = g_strdup_printf("%s\n%s\n%s\n%" G_GUINT64_FORMAT,
      ok ? "ok" : "error", rec.ogg ? rec.ogg : "", rec.flac ? rec.flac : "", rec.dropped);
   event_emit("client.recordings.finished", NULL, info);
   g_free(info);
   return NULL;
}
gboolean rr_recordings_start(rr_recording_format format, const rr_recording_tags *tags) {
   ensure_init();
   if (!tags || format < RR_RECORD_OGG || format > RR_RECORD_BOTH) return FALSE;
   g_mutex_lock(&rec.lock);
   gboolean busy = rec.recording || rec.worker != NULL;
   g_mutex_unlock(&rec.lock);
   if (busy) return FALSE;
   GDateTime *now = g_date_time_new_now_local();
   char *stamp = g_date_time_format(now, "%Y-%m-%d_%H-%M-%S");
   /* UUID stays in metadata, never in the visible filename. */
   char *id = g_uuid_string_random();
   char *stem = recording_stem(tags, stamp);
   char *base = NULL;
   for (unsigned serial = 1; serial < 1000000; serial++) {
      g_free(base);
      base = serial == 1 ? g_strdup(stem) :
         g_strdup_printf("%s (%u)", stem, serial);
      char *candidate_ogg = make_path(base, "ogg");
      char *candidate_flac = make_path(base, "flac");
      gboolean occupied = g_file_test(candidate_ogg, G_FILE_TEST_EXISTS) ||
         g_file_test(candidate_flac, G_FILE_TEST_EXISTS);
      g_free(candidate_ogg);
      g_free(candidate_flac);
      if (!occupied) break;
      if (serial == 999999) {
         g_warning("Unable to allocate an unused recording filename");
         g_free(stem); g_free(base); g_free(id);
         g_free(stamp);
         g_date_time_unref(now);
         return FALSE;
      }
   }
   g_free(stem);
   char *ogg = format != RR_RECORD_FLAC ? make_path(base, "ogg") : NULL;
   char *flac = format != RR_RECORD_OGG ? make_path(base, "flac") : NULL;
   g_free(base); g_free(stamp);
   g_date_time_unref(now);
   g_mutex_lock(&rec.lock);
   rec.ogg = ogg; rec.flac = flac; rec.id = id;
   recording_tags_copy(&rec.tags, tags);
   rec.head = rec.used = 0; rec.dropped = 0;
   rec.stopping = FALSE; rec.recording = TRUE;
   rec.worker = g_thread_new("rr-record-encoder", recorder_worker, NULL);
   g_mutex_unlock(&rec.lock);
   return TRUE;
}
void rr_recordings_push(const int16_t *pcm, size_t frames) {
   if (!initialized || !pcm || !frames || frames > QUEUE_FRAMES) return;
   if (!g_mutex_trylock(&rec.lock)) return;
   if (rec.recording && !rec.stopping) {
      if (frames <= QUEUE_FRAMES - rec.used) {
         size_t tail = (rec.head + rec.used) % QUEUE_FRAMES;
         size_t first = MIN(frames, (size_t)(QUEUE_FRAMES - tail));
         memcpy(rec.ring + tail * 2, pcm, first * 2 * sizeof(int16_t));
         if (frames > first) memcpy(rec.ring, pcm + first * 2,
            (frames - first) * 2 * sizeof(int16_t));
         rec.used += frames;
         g_cond_signal(&rec.ready);
      } else rec.dropped += frames;
   }
   g_mutex_unlock(&rec.lock);
}
void rr_recordings_stop(void) {
   ensure_init();
   g_mutex_lock(&rec.lock);
   rec.stopping = TRUE;
   g_cond_signal(&rec.ready);
   g_mutex_unlock(&rec.lock);
}
gboolean rr_recordings_active(void) {
   ensure_init();
   g_mutex_lock(&rec.lock);
   gboolean active = rec.recording && !rec.stopping;
   g_mutex_unlock(&rec.lock);
   return active;
}
void rr_recordings_collect(void) {
   ensure_init();
   g_mutex_lock(&rec.lock);
   GThread *thread = rec.worker;
   gboolean finished = thread && !rec.recording;
   if (finished) rec.worker = NULL;
   g_mutex_unlock(&rec.lock);
   if (finished) {
      g_thread_join(thread);
      g_clear_pointer(&rec.ogg, g_free);
      g_clear_pointer(&rec.flac, g_free);
      g_clear_pointer(&rec.id, g_free);
   }
}
/* Native decoder workers own codec state; GTK and the audio callback never decode. */
static gboolean playback_control(double *seek_to, gboolean *reverse, gboolean *stop) {
   g_mutex_lock(&playback_lock);
   while (playback_paused && !playback_stop && !playback_seek)
      g_cond_wait(&playback_ready, &playback_lock);
   *stop = playback_stop;
   gboolean seek = playback_seek;
   if (seek) {
      *seek_to = playback_target;
      *reverse = playback_reverse;
      playback_seek = FALSE;
   }
   g_mutex_unlock(&playback_lock);
   return seek;
}
static gpointer playback_worker(gpointer unused) {
   (void)unused;
   char *path;
   rr_recording_playback_fn output;
   double position;
   gboolean reverse;
   g_mutex_lock(&playback_lock);
   path = g_strdup(playback_path);
   output = playback_output;
   position = playback_target;
   reverse = playback_reverse;
   playback_seek = FALSE;
   g_mutex_unlock(&playback_lock);
   gint64 last_update = 0;
   gboolean stop = FALSE;
   for (;;) {
      double requested = position;
      gboolean next_direction = reverse;
      if (playback_control(&requested, &next_direction, &stop)) {
         position = MAX(0.0, requested);
         reverse = next_direction;
      }
      if (stop || (reverse && position <= 0.0)) break;
      double start = reverse ? MAX(0.0, position - 4.0) : position;
      rr_recording_decoder *decoder = rr_recording_decoder_open(path);
      if (!decoder || !rr_recording_decoder_seek(decoder, start)) {
         g_warning("Unable to open or seek recording decoder: %s", path);
         rr_recording_decoder_close(decoder);
         break;
      }
      gboolean restart = FALSE, ended = FALSE;
      int16_t block[320 * 2];
      if (reverse) {
         /* Reverse bounded four-second windows, not entire recordings. */
         size_t wanted = (size_t)((position - start) * RATE);
         int16_t *segment = g_new(int16_t, (wanted + 1) * 2);
         size_t count = 0;
         while (count < wanted && !stop && !restart) {
            if (playback_control(&requested, &next_direction, &stop)) {
               position = MAX(0.0, requested);
               reverse = next_direction;
               restart = TRUE;
               break;
            }
            if (stop) break;
            gssize got = rr_recording_decoder_read(decoder,
               segment + count * 2, MIN(wanted - count, (size_t)320));
            if (got <= 0) break;
            count += (size_t)got;
         }
         if (!stop && !restart) {
            for (size_t off = count; off; ) {
               if (playback_control(&requested, &next_direction, &stop)) {
                  position = MAX(0.0, requested);
                  reverse = next_direction;
                  restart = TRUE;
                  break;
               }
               if (stop) break;
               size_t frames = MIN(off, (size_t)320);
               for (size_t i = 0; i < frames; ++i) {
                  block[i * 2] = segment[(off - 1 - i) * 2];
                  block[i * 2 + 1] = segment[(off - 1 - i) * 2 + 1];
               }
               if (output) output(block, frames);
               double elapsed = (double)frames / RATE;
               position = MAX(0.0, position - elapsed);
               off -= frames;
               gint64 tick = g_get_monotonic_time();
               if (tick - last_update >= 100000) {
                  char value[G_ASCII_DTOSTR_BUF_SIZE];
                  g_ascii_formatd(value, sizeof(value), "%.6f", position);
                  event_emit(RR_RECORDING_PLAYBACK_POSITION, NULL, value);
                  last_update = tick;
               }
               g_usleep((gulong)(elapsed * 1000000.0));
            }
         }
         ended = count > 0;
         g_free(segment);
      } else {
         for (;;) {
            if (playback_control(&requested, &next_direction, &stop)) {
               position = MAX(0.0, requested);
               reverse = next_direction;
               restart = TRUE;
               break;
            }
            if (stop) break;
            gssize got = rr_recording_decoder_read(decoder, block, 320);
            if (got <= 0) {
               if (got < 0) g_warning("Recording decoder error: %s", path);
               ended = TRUE;
               break;
            }
            if (output) output(block, (size_t)got);
            double elapsed = (double)got / RATE;
            position += elapsed;
            gint64 tick = g_get_monotonic_time();
            if (tick - last_update >= 100000) {
               char value[G_ASCII_DTOSTR_BUF_SIZE];
               g_ascii_formatd(value, sizeof(value), "%.6f", position);
               event_emit(RR_RECORDING_PLAYBACK_POSITION, NULL, value);
               last_update = tick;
            }
            g_usleep((gulong)(elapsed * 1000000.0));
         }
      }
      rr_recording_decoder_close(decoder);
      if (stop) break;
      if (restart) continue;
      if (!reverse || position <= 0.0001 || !ended) break;
      // Reverse uses four-second segments, keeping PCM memory bounded.
   }
   g_free(path);
   event_emit("client.recordings.playback-decoded", NULL, NULL);
   return NULL;
}
gboolean rr_recordings_play_at(const char *path, double seconds, gboolean reverse,
                              rr_recording_playback_fn output) {
   ensure_init();
   if (!path || !output) return FALSE;
   g_mutex_lock(&playback_lock);
   if (playback_thread) { g_mutex_unlock(&playback_lock); return FALSE; }
   playback_stop = playback_seek = playback_paused = FALSE;
   playback_reverse = reverse;
   playback_target = MAX(seconds, 0.0);
   playback_path = g_strdup(path);
   playback_output = output;
   playback_thread = g_thread_new("rr-record-playback", playback_worker, NULL);
   g_mutex_unlock(&playback_lock);
   return TRUE;
}
gboolean rr_recordings_play(const char *path, rr_recording_playback_fn output) {
   return rr_recordings_play_at(path, 0.0, FALSE, output);
}
gboolean rr_recordings_play_busy(void) {
   ensure_init();
   g_mutex_lock(&playback_lock);
   gboolean busy = playback_thread != NULL;
   g_mutex_unlock(&playback_lock);
   return busy;
}
gboolean rr_recordings_play_reposition(const char *path, double seconds, gboolean reverse) {
   ensure_init();
   g_mutex_lock(&playback_lock);
   gboolean found = playback_thread && !playback_stop && playback_path &&
      !g_strcmp0(path, playback_path);
   if (found) {
      playback_target = MAX(0.0, seconds);
      playback_reverse = reverse;
      playback_seek = TRUE;
      playback_paused = FALSE;
      g_cond_broadcast(&playback_ready);
   }
   g_mutex_unlock(&playback_lock);
   return found;
}
void rr_recordings_play_pause(gboolean pause) {
   ensure_init();
   g_mutex_lock(&playback_lock);
   playback_paused = pause;
   g_cond_broadcast(&playback_ready);
   g_mutex_unlock(&playback_lock);
}
void rr_recordings_play_stop(void) {
   ensure_init();
   g_mutex_lock(&playback_lock);
   playback_stop = TRUE;
   playback_paused = FALSE;
   g_cond_broadcast(&playback_ready);
   g_mutex_unlock(&playback_lock);
}
void rr_recordings_play_collect(void) {
   ensure_init();
   g_mutex_lock(&playback_lock);
   GThread *thread = playback_thread;
   playback_thread = NULL;
   g_mutex_unlock(&playback_lock);
   if (thread) g_thread_join(thread);
   g_clear_pointer(&playback_path, g_free);
}

/* A compact, bounded-memory overview of even very long recordings.
 * Mono 800 Hz analysis yields min/max envelopes in up to 4096 time bins. */
typedef struct {
   char *path;
   guint64 generation;
} waveform_request;
static gpointer waveform_worker(gpointer data) {
   waveform_request *request = data;
   char *path = request->path;
   guint64 generation = request->generation;
   gboolean canceled = FALSE;
   rr_recording_waveform *wave = g_new0(rr_recording_waveform, 1);
   g_strlcpy(wave->path, path, sizeof(wave->path));
   rr_recording_decoder *decoder = rr_recording_decoder_open(path);
   if (decoder) {
      gint16 raw[1024 * 2];
      gint16 lows[2] = {G_MAXINT16, G_MAXINT16};
      gint16 highs[2] = {G_MININT16, G_MININT16};
      guint width = 20, phase = 0;
      guint64 frames = 0;
      gboolean error = FALSE;
      for (;;) {
         g_mutex_lock(&wave_lock);
         canceled = wave_shutting_down || generation != wave_generation;
         g_mutex_unlock(&wave_lock);
         if (canceled) break;
         gssize n = rr_recording_decoder_read(decoder, raw, 1024);
         if (n <= 0) { error = n < 0; break; }
         for (gssize j = 0; j < n; ++j) {
            frames++;
            for (unsigned c = 0; c < 2; ++c) {
               gint16 sample = raw[j * 2 + c];
               lows[c] = MIN(lows[c], sample);
               highs[c] = MAX(highs[c], sample);
            }
            if (++phase == width) {
               wave->bins[wave->count++] = (rr_recording_wave_bin){
                  lows[0], highs[0], lows[1], highs[1]
               };
               phase = 0;
               lows[0] = lows[1] = G_MAXINT16;
               highs[0] = highs[1] = G_MININT16;
               if (wave->count == RR_RECORDING_WAVE_BINS) {
                  for (guint k = 0; k < RR_RECORDING_WAVE_BINS / 2; k++) {
                     rr_recording_wave_bin x = wave->bins[k * 2], y = wave->bins[k * 2 + 1];
                     wave->bins[k] = (rr_recording_wave_bin){
                        MIN(x.left_low, y.left_low), MAX(x.left_high, y.left_high),
                        MIN(x.right_low, y.right_low), MAX(x.right_high, y.right_high)
                     };
                  }
                  wave->count /= 2;
                  width *= 2;
               }
            }
         }
      }
      if (phase && wave->count < RR_RECORDING_WAVE_BINS)
         wave->bins[wave->count++] = (rr_recording_wave_bin){
            lows[0], highs[0], lows[1], highs[1]
         };
      wave->duration = rr_recording_decoder_duration(decoder);
      if (wave->duration <= 0) wave->duration = (double)frames / RATE;
      wave->success = !canceled && !error && frames > 0;
      rr_recording_decoder_close(decoder);
   } else g_warning("Unable to analyze recording waveform: %s", path);
   g_mutex_lock(&wave_lock);
   gboolean current = !wave_shutting_down && generation == wave_generation;
   g_mutex_unlock(&wave_lock);
   if (current) event_emit_binary(RR_RECORDING_WAVEFORM_READY, NULL, wave, sizeof(*wave));
   g_mutex_lock(&wave_lock);
   wave_workers--;
   g_cond_broadcast(&wave_finished);
   g_mutex_unlock(&wave_lock);
   g_free(wave);
   g_free(path);
   g_free(request);
   return NULL;
}
void rr_recordings_waveform_request(const char *path) {
   if (!path || !*path) return;
   ensure_init();
   g_mutex_lock(&wave_lock);
   if (wave_shutting_down) {
      g_mutex_unlock(&wave_lock);
      return;
   }
   waveform_request *request = g_new0(waveform_request, 1);
   request->path = g_strdup(path);
   request->generation = ++wave_generation;
   wave_workers++;
   g_mutex_unlock(&wave_lock);
   GThread *thread = g_thread_new("rr-record-waveform", waveform_worker, request);
   g_thread_unref(thread);
}

/* Flush native codecs and join worker threads on client shutdown. */
void rr_recordings_shutdown(void) {
   ensure_init();
   g_mutex_lock(&wave_lock);
   wave_shutting_down = TRUE;
   wave_generation++;
   while (wave_workers) g_cond_wait(&wave_finished, &wave_lock);
   g_mutex_unlock(&wave_lock);
   rr_recordings_stop();
   g_mutex_lock(&rec.lock);
   GThread *thread = rec.worker;
   rec.worker = NULL;
   g_mutex_unlock(&rec.lock);
   if (thread) g_thread_join(thread);
   g_clear_pointer(&rec.ogg, g_free);
   g_clear_pointer(&rec.flac, g_free);
   g_clear_pointer(&rec.id, g_free);
   rr_recordings_play_stop();
   rr_recordings_play_collect();
}
