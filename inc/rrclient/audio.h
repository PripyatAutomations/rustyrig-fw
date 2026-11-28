//
// rrclient/audio.h
//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrclient_audio_h)
#define __rrclient_audio_h
#include <librustyaxe/core.h>
#include <stddef.h>
#include <rrclient/audio.engine.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/spectrum.af.h>

/* Control pins carry source-qualified logical signals, not rig status. The initial emitter is the local client's PTT command output. */
#define RRCLIENT_ROUTING_PTT_SIGNAL "client.routing.ptt.signal"
#define RRCLIENT_ROUTING_PTT_LOCAL "ptt.local.active-vfo"
typedef struct {
   char source[64];
   bool raised;
} rrclient_routing_ptt_signal_t;
/* Core-only: send source-qualified modem PTT through the rig's server. */
bool rrclient_modem_ptt_send(const char *source, const char *rx_id, bool on);

/* Native oscillator controls cross GTK/core domains; no implicit TX/PTT route. */
#define RRCLIENT_AUDIO_OSC_REQUEST "client.audio.osc.request"
typedef struct {
   char action; /* A add/update, D delete, P set parameter, C connect, U unplug */
   unsigned slot;
   rr_audio_osc_config_t config;
   char property[24]; /* P: frequency, level, waveform, route */
   double number;     /* P numeric parameter */
   char value[24];    /* P string enum value */
   unsigned port;      /* C/U source pin: 0 = I, 1 = Q */
   char target[64];    /* C/U sink: rx.mix, hq.mix, tx.mix, proc.lv2.N.in.L/R, modemN.rx */
} rr_audio_osc_request_t;
void audio_request_oscillator(const rr_audio_osc_request_t *request);
/* Numeric control sources are core-owned and may override an oscillator
 * property while connected; saved defaults remain unchanged. */
#define RRCLIENT_AUDIO_OSC_CONTROL_REQUEST "client.audio.osc.control.request"
typedef struct {
   char action; /* B bind, U unbind, V live value, S save value, D delete source */
   unsigned slot;
   char property[24]; /* frequency, level, waveform */
   char source[64];   /* control source ID (e.g. control.ctcss) */
   double value;      /* valid only for V */
} rr_audio_osc_control_request_t;
void audio_request_oscillator_control(const rr_audio_osc_control_request_t *request);

/* Core-domain source identity shared by modem and LV2 consumers. */
bool audio_oscillator_source_present(const char *source);
extern bool audio_enabled;
/* Playback requests must cross the GTK -> core event boundary. */
#define RR_AUDIO_RECORDING_CONTROL_REQUEST "client.audio.recording.control"
typedef struct {
   char operation;              /* P play/seek, S stop, H pause, R resume */
   gboolean reverse;
   double seconds;
   char path[2048];
} rr_audio_recording_control_t;
void audio_recording_events_init(void);
void audio_recording_control_core(const char *event, const void *data, size_t size, rrconn_t *conn, void *user);
bool audio_recording_play_at(const char *path, double seconds, bool reverse);
/* File playback is routed into the client PCM hub. */
bool audio_recording_play(const char *path);
void audio_recording_play_stop(void);
void audio_recording_play_collect(void);
extern bool gst_active;

extern bool audio_init(void);
/* Low-rate local SDR PCM joins the 16 kHz radio bus, shared with remote codecs/modems; higher-rate sources enter the 44.1/48 kHz HQ mixer directly. PCM formats
 * are internal and never advertised as codecs. */
bool audio_local_rx_start(const char *id);
bool audio_local_rx_start_format(const char *id, const rr_pcm_stream_format_t *format);
bool audio_local_rx_push_pcm(const char *id, const void *interleaved, size_t frames);
bool audio_local_rx_push(const char *id, const int16_t *pcm, size_t frames);
void audio_local_rx_stop(const char *id);
/* Core-thread modem PCM TX ingress. Never keys PTT; requires active, authorized TX media and a confirmed PTT state before forwarding. */
bool audio_modem_tx_pcm(unsigned slot, const char *ptt_target, const int16_t *pcm, size_t frames);
bool audio_modem_tx_allowed(const char *target);
/* Core-only slot mixer handoff; NULL PCM performs readiness/teardown checks. */
void audio_vrig_tx_tick(unsigned slot,const int16_t *mono,size_t frames);
void audio_vrig_tx_reset(unsigned slot);
/* Internal source patch interface. Producers register their PCM format, route into tx.mix, then push samples; no source touches the encoder. */
bool audio_tx_source_register(const char *id, const rr_pcm_stream_format_t *format);
bool audio_tx_source_route(const char *id, bool connected);
bool audio_tx_source_push(const char *id, const void *pcm, size_t frames);

/* Send a complete encoded audio binframe via the existing WS/direct-RTP media transport. Core context only. */
bool rrclient_send_tx_audio_frame(rrconn_t *connection, const void *frame, size_t length, bool setup);
extern void audio_tx_free_frame(void);
extern bool ws_audio_init(void);
extern void ws_audio_shutdown(void);
extern bool audio_process_frame(const char *data, size_t len);
/* Every local LV2 block is its own proc.lv2.N fwdsp subprocess. GTK only submits messages; the core owns worker lifetime and PCM routes. */
#define RRCLIENT_AUDIO_EFFECT_REQUEST "client.audio.effect.request"
#define RRCLIENT_AUDIO_EFFECT_CHANGED "client.audio.effect.changed"
#define RR_AUDIO_EFFECT_MAX 8
typedef struct {
   char action;                 /* A add/start, D delete, R input, O output */
   unsigned slot;               /* 0 = next free for A */
   char factory[96];
   char source[64];             /* decoded 16 kHz mono stream ID */
   char output;                 /* O: M disconnect, L/R mixer channel, S both */
   unsigned channel;            /* 0 mono/left, 1 right (stereo plugins only) */
   unsigned channels;           /* A: plugin audio layout 1 mono / 2 stereo (persistent) */
} rr_audio_effect_request_t;
void audio_request_effect_block(const rr_audio_effect_request_t *request);
#define RRCLIENT_AUDIO_CONTROL_REQUEST "client.audio.control.request"
typedef enum {
   RR_AUDIO_CONTROL_MASTER = 1,
   RR_AUDIO_CONTROL_EAR,
   RR_AUDIO_CONTROL_STREAM,
   RR_AUDIO_CONTROL_INPUT_GAIN,
   RR_AUDIO_CONTROL_PORT_GAIN,
   RR_AUDIO_CONTROL_EFFECT_PARAMETER,
   RR_AUDIO_CONTROL_EFFECT_STRING,
   RR_AUDIO_CONTROL_MIC_TX_ROUTE,
   RR_AUDIO_CONTROL_MIC_TX_CHANNEL /* channel=0/1; percent=connect (0/1) */
} rr_audio_control_kind_t;
typedef struct {
   rr_audio_control_kind_t kind;
   unsigned channel;
   int percent;
   double gain_db;
   char stream[64];
   char element[96], property[96];
   char string_value[256];
   char output;
} rr_audio_control_request_t;
/* Asynchronous GTK -> core dispatch; do not mutate fwdsp or routing state on GTK. */
void audio_request_rx_volume(int percent);
void audio_request_rx_channel_volume(unsigned channel, int percent);
void audio_request_rx_stream_route(const char *id, char output);
/* Route local microphone into a real rig TX media channel (RX UUID),
 * "auto" follows the selected TX channel, "none" disconnects. */
void audio_request_mic_tx_route(const char *rx_uuid);
/* Patch one mono microphone output to either TX mixer ear independently.
 * Two connections are allowed; TX encoder ultimately downmixes both. */
void audio_request_mic_tx_channel(unsigned channel, bool connect);
void audio_request_rx_input_gain(const char *id, unsigned channel, double gain_db);
void audio_request_port_gain(const char *id, unsigned channel, double gain_db);
void audio_request_effect_parameter(const char *processor, const char *element, const char *property, double value);
void audio_request_effect_string(const char *processor, const char *element, const char *property, const char *value);
extern bool audio_set_rx_volume(int percent);
extern bool audio_set_rx_channel_volume(unsigned channel, int percent);
extern bool audio_set_tx_volume(int percent);
struct rrclient_rx_audio {
   const char *uuid, *codec;
   const char *room;
   uint32_t source_rate_hz;
};
/* Per-server room routing: L/R/S (both ears)/M. Independent of UI selection. */
char audio_room_route(const char *server, const char *room);
bool audio_set_room_route(const char *server, const char *room, char output);
/* Snapshot of all subscribed native RX streams, across connections. Opaque IDs survive codec changes; use only on the client main loop. Returns total count. */
struct rrclient_rx_stream {
   char id[64], server[512], room[128], uuid[64], codec[5];
   char output;
   uint32_t source_rate_hz;
   bool active_rx;              /* selected radio's RX alias: rig.src */
   bool active_tx;              /* real TX capability: selected rig exposes rig.sink */
};
size_t audio_rx_streams(struct rrclient_rx_stream *streams, size_t capacity);
/* AF visualization request/reply. Only core reads RX route inventory;
 * GTK receives an owned spectrum sample, never a route pointer. */
#define RRCLIENT_AF_SPECTRUM_REQUEST "client.audio.af-spectrum.request"
#define RRCLIENT_AF_SPECTRUM_RESULT "client.audio.af-spectrum.result"
typedef struct {
   guint64 request_id;
   char server[512], room[128];
   /* M = heard mix, L/R = heard ear, A = combined stereo, S = explicit decoder stream. */
   char source;
   char stream[64];
} rrclient_af_spectrum_request_t;
typedef struct {
   guint64 request_id;
   bool ready;
   bool stereo;
   /* Highest meaningful AF frequency for the selected source, bounded by the 16 kHz analyzer Nyquist. Zero means metadata unavailable. */
   uint32_t max_hz;
   float bins[RR_AF_BINS];
   float left[RR_AF_BINS], right[RR_AF_BINS];   /* final mixer channels, for stereo overlay */
} rrclient_af_spectrum_result_t;
#define RRCLIENT_AUDIO_SNAPSHOT_REQUEST "client.audio.snapshot.request"
#define RRCLIENT_AUDIO_SNAPSHOT_RESULT "client.audio.snapshot.result"
typedef struct {
   uint32_t count;
   struct rrclient_rx_stream streams[];
} rr_audio_stream_snapshot_t;

bool audio_set_rx_stream_route(const char *id, char output);
/* Synchronize this connection's subscribed decoders without stopping muted ones. */
void audio_sync_rx_channels(const struct rrclient_rx_audio *channels, size_t count);

extern bool audio_switch_codec(const char *codec, bool is_tx);
extern void audio_stop_codec(bool is_tx);

extern void try_send_next_frame(rrconn_t *cptr);

#endif // !defined(__rrclient_audio_h)
