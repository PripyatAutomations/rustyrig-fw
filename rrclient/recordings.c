// Client-side PCM recording and playback. GPL-2.0-only.
#include "recordings.h"
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
   GPid child;
   int child_fd;
   char *ogg, *flac, *id;
   guint64 dropped;
} Recorder;
static Recorder rec;
static gsize initialized;
static GMutex playback_lock;
static GCond playback_ready;
static gboolean playback_stop;
static GThread *playback_thread;
static char *playback_path;
static rr_recording_playback_fn playback_output;
static gboolean playback_seek, playback_reverse, playback_paused;
static double playback_target;

static void ensure_init(void) {
   if (g_once_init_enter(&initialized)) {
      g_mutex_init(&rec.lock);
      g_cond_init(&rec.ready);
      rec.child_fd = -1;
      g_mutex_init(&playback_lock);
      g_cond_init(&playback_ready);
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
static char *make_path(const char *base, const char *suffix) {
   char *dir = rr_recordings_directory();
   char *path = g_strdup_printf("%s/%s.%s", dir, base, suffix);
   g_free(dir);
   return path;
}
static gboolean write_all(int fd, const void *data, size_t length) {
   const char *p = data;
   while (length) {
      gssize n = write(fd, p, (unsigned int)MIN(length, (size_t)0x7fffffff));
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return FALSE;
      p += n;
      length -= (size_t)n;
   }
   return TRUE;
}
/* Append end time after ffmpeg finalizes; keep UUID so both formats remain linked. */
static char *rename_with_end(const char *oldpath, const char *stamp) {
   if (!oldpath || !*oldpath) return NULL;
   const char *ext = strrchr(oldpath, '.');
   if (!ext) return g_strdup(oldpath);
   char *prefix = g_strndup(oldpath, ext - oldpath);
   char *newpath = g_strdup_printf("%s - %s%s", prefix, stamp, ext);
   if (g_rename(oldpath, newpath)) {
      g_free(newpath);
      newpath = g_strdup(oldpath);
   }
   g_free(prefix);
   return newpath;
}
static gboolean child_wait_ok(GPid pid) {
#ifdef _WIN32
   DWORD code = (DWORD)-1;
   return WaitForSingleObject((HANDLE)pid, INFINITE) == WAIT_OBJECT_0 &&
      GetExitCodeProcess((HANDLE)pid, &code) && code == 0;
#else
   int status = 0;
   return waitpid(pid, &status, 0) >= 0 && WIFEXITED(status) && !WEXITSTATUS(status);
#endif
}
static void child_stop(GPid pid) {
#ifdef _WIN32
   TerminateProcess((HANDLE)pid, 1);
#else
   kill(pid, SIGTERM);
#endif
}
static void block_worker_sigpipe(void) {
#ifndef _WIN32
   sigset_t blocked;
   sigemptyset(&blocked);
   sigaddset(&blocked, SIGPIPE);
   pthread_sigmask(SIG_BLOCK, &blocked, NULL);
#endif
}
static gpointer recorder_worker(gpointer unused) {
   (void)unused;
   // An encoder exiting early must not deliver SIGPIPE to the entire client.
   block_worker_sigpipe();
   int16_t block[1024 * 2];
   gboolean ok = TRUE;
   for (;;) {
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
      if (n && !write_all(rec.child_fd, block, n * 2 * sizeof(int16_t))) {
         ok = FALSE;
         break;
      }
   }
   close(rec.child_fd);
   rec.child_fd = -1;
   if (!child_wait_ok(rec.child)) ok = FALSE;
   g_spawn_close_pid(rec.child);
   rec.child = 0;
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
static void append_tag(GPtrArray *argv, const char *key, const char *value) {
   if (!value || !*value) return;
   g_ptr_array_add(argv, g_strdup("-metadata"));
   g_ptr_array_add(argv, g_strdup_printf("%s=%s", key, value));
}
static void append_output(GPtrArray *argv, const char *codec, const char *path,
                          const rr_recording_tags *tags, const char *id) {
   g_ptr_array_add(argv, g_strdup("-map"));
   g_ptr_array_add(argv, g_strdup("0:a:0"));
   g_ptr_array_add(argv, g_strdup("-c:a"));
   g_ptr_array_add(argv, g_strdup(codec));
   if (!strcmp(codec, "libvorbis")) {
      g_ptr_array_add(argv, g_strdup("-q:a"));
      g_ptr_array_add(argv, g_strdup("4"));
   }
   append_tag(argv, "TITLE", tags->title);
   append_tag(argv, "ARTIST", tags->user);
   append_tag(argv, "COMMENT", "Recorded by RustyRig client from stereo monitor PCM");
   append_tag(argv, "RECORDING_ID", id);
   append_tag(argv, "START_TIME", tags->start_time);
   append_tag(argv, "SITE", tags->site);
   append_tag(argv, "SERVER", tags->server);
   append_tag(argv, "RIG", tags->rig);
   append_tag(argv, "VFO", tags->vfo);
   append_tag(argv, "FREQUENCY_HZ", tags->frequency);
   append_tag(argv, "MODE", tags->mode);
   append_tag(argv, "GRID_SQUARE", tags->grid);
   append_tag(argv, "GPS_LATITUDE", tags->latitude);
   append_tag(argv, "GPS_LONGITUDE", tags->longitude);
   append_tag(argv, "LOCATION_SOURCE", tags->location_source);
   append_tag(argv, "SOURCES", tags->sources);
   append_tag(argv, "USER", tags->user);
   g_ptr_array_add(argv, g_strdup("-f"));
   g_ptr_array_add(argv, g_strdup(!strcmp(codec, "flac") ? "flac" : "ogg"));
   g_ptr_array_add(argv, g_strdup(path));
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
   char *station = safe(tags->site);
   char *rig = safe(tags->rig);
   char *vfo = safe(tags->vfo);
   char *freq = safe(tags->frequency);
   char *mode = safe(tags->mode);
   char *id = g_uuid_string_random();
   char *base = g_strdup_printf("RX - %s%s - %s.%s.%s - %s - %s",
      freq, mode, station, rig, vfo, stamp, id);
   char *ogg = format != RR_RECORD_FLAC ? make_path(base, "ogg") : NULL;
   char *flac = format != RR_RECORD_OGG ? make_path(base, "flac") : NULL;
   g_free(base); g_free(station); g_free(rig); g_free(vfo); g_free(freq); g_free(mode); g_free(stamp);
   g_date_time_unref(now);
   GPtrArray *args = g_ptr_array_new_with_free_func(g_free);
   const char *prefix[] = {"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-n",
      "-f", "s16le", "-ar", "16000", "-ac", "2", "-i", "pipe:0", NULL};
   for (unsigned i = 0; prefix[i]; i++) g_ptr_array_add(args, g_strdup(prefix[i]));
   if (ogg) append_output(args, "libvorbis", ogg, tags, id);
   if (flac) append_output(args, "flac", flac, tags, id);
   g_ptr_array_add(args, NULL);
   GError *error = NULL;
   gint child_stdin = -1;
   GPid pid = 0;
   gboolean started = g_spawn_async_with_pipes(NULL, (gchar **)args->pdata, NULL,
      G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid,
      &child_stdin, NULL, NULL, &error);
   g_ptr_array_free(args, TRUE);
   if (!started) {
      g_warning("Unable to start recording encoder: %s", error ? error->message : "unknown error");
      g_clear_error(&error);
      g_free(ogg); g_free(flac); g_free(id);
      return FALSE;
   }
   g_mutex_lock(&rec.lock);
   rec.ogg = ogg; rec.flac = flac; rec.id = id;
#ifdef _WIN32
   _setmode(child_stdin, _O_BINARY);
#endif
   rec.child = pid; rec.child_fd = child_stdin;
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
/* The playback worker owns decoder processes. Seek/reverse requests only change
 * shared control fields; neither GTK nor the audio thread waits for ffmpeg. */
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
      double length = reverse ? position - start : 0.0;
      char seek_value[G_ASCII_DTOSTR_BUF_SIZE], length_value[G_ASCII_DTOSTR_BUF_SIZE];
      g_ascii_formatd(seek_value, sizeof(seek_value), "%.6f", start);
      g_ascii_formatd(length_value, sizeof(length_value), "%.6f", length);
      gchar *args_forward[] = {
         "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
         "-ss", seek_value, "-i", path, "-vn", "-f", "s16le",
         "-ac", "2", "-ar", "16000", "pipe:1", NULL
      };
      gchar *args_reverse[] = {
         "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
         "-ss", seek_value, "-i", path, "-t", length_value,
         "-vn", "-af", "areverse", "-f", "s16le",
         "-ac", "2", "-ar", "16000", "pipe:1", NULL
      };
      GPid pid = 0;
      gint fd = -1;
      GError *error = NULL;
      if (!g_spawn_async_with_pipes(NULL, reverse ? args_reverse : args_forward, NULL,
         G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
         &pid, NULL, &fd, NULL, &error)) {
         g_warning("Unable to start recording decoder: %s", error ? error->message : "unknown");
         g_clear_error(&error);
         break;
      }
#ifdef _WIN32
      _setmode(fd, _O_BINARY);
#endif
      gboolean restart = FALSE, ended = FALSE;
      int16_t block[320 * 2];
      for (;;) {
         if (playback_control(&requested, &next_direction, &stop)) {
            position = MAX(0.0, requested);
            reverse = next_direction;
            restart = TRUE;
            break;
         }
         if (stop) break;
         size_t n = 0;
         while (n < sizeof(block)) {
            gssize got = read(fd, ((char *)block) + n,
               (unsigned int)(sizeof(block) - n));
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) break;
            n += (size_t)got;
         }
         if (!n) { ended = TRUE; break; }
         size_t frames = n / (sizeof(int16_t) * 2);
         if (!frames) continue;
         if (output) output(block, frames);
         double elapsed = (double)frames / RATE;
         position = reverse ? MAX(0.0, position - elapsed) : position + elapsed;
         gint64 now = g_get_monotonic_time();
         if (now - last_update >= 100000) {
            char value[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_formatd(value, sizeof(value), "%.6f", position);
            event_emit(RR_RECORDING_PLAYBACK_POSITION, NULL, value);
            last_update = now;
         }
         g_usleep((gulong)(elapsed * 1000000.0));
      }
      close(fd);
      if (stop || restart) child_stop(pid);
      child_wait_ok(pid);
      g_spawn_close_pid(pid);
      if (stop) break;
      if (restart) continue;
      if (!reverse || position <= 0.0001 || !ended) break;
      // Reverse uses four-second segments, keeping areverse's memory bounded.
   }
   g_free(path);
   event_emit("client.recordings.playback-finished", NULL, NULL);
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

/* Flush the encoder and reap all worker processes on client shutdown. */
void rr_recordings_shutdown(void) {
   ensure_init();
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
