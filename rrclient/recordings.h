// Client monitor recording API. GPL-2.0-only.
#ifndef RRCLIENT_RECORDINGS_H
#define RRCLIENT_RECORDINGS_H
#include <glib.h>
#include <stdint.h>
#include <stddef.h>
typedef enum { RR_RECORD_OGG = 0, RR_RECORD_FLAC = 1, RR_RECORD_BOTH = 2 } rr_recording_format;
typedef struct {
   const char *title, *user, *start_time, *site, *server, *rig, *vfo;
   const char *frequency, *mode, *grid, *latitude, *longitude, *location_source, *sources;
} rr_recording_tags;
#define RR_RECORDING_WAVEFORM_READY "client.recordings.waveform-ready"
#define RR_RECORDING_PLAYBACK_POSITION "client.recordings.playback-position"
#define RR_RECORDING_WAVE_BINS 4096
typedef struct {
   gint16 low, high;
} rr_recording_wave_bin;
typedef struct {
   char path[2048];
   double duration;
   guint count;
   gboolean success;
   rr_recording_wave_bin bins[RR_RECORDING_WAVE_BINS];
} rr_recording_waveform;
void rr_recordings_waveform_request(const char *path);
typedef void (*rr_recording_playback_fn)(const int16_t *stereo, size_t frames);
char *rr_recordings_directory(void);
gboolean rr_recordings_start(rr_recording_format format, const rr_recording_tags *tags);
void rr_recordings_push(const int16_t *stereo, size_t frames);
void rr_recordings_stop(void);
gboolean rr_recordings_active(void);
/* Call on finished event, not on the audio callback. */
void rr_recordings_collect(void);
gboolean rr_recordings_play(const char *path, rr_recording_playback_fn output);
gboolean rr_recordings_play_at(const char *path, double seconds, gboolean reverse, rr_recording_playback_fn output);
gboolean rr_recordings_play_reposition(const char *path, double seconds, gboolean reverse);
void rr_recordings_play_pause(gboolean pause);
void rr_recordings_play_stop(void);
void rr_recordings_play_collect(void);
void rr_recordings_shutdown(void);
#endif
