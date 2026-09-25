//
// libfwdspmgr/fwdsp-mgr.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rr_fwdsp_mgr_h)
#define	__rr_fwdsp_mgr_h

enum fwdsp_io_type {
   FW_IO_NONE = 0,              // invalid
   FW_IO_STDIO,                 // stdin/stdout
   FW_IO_SOCKET                 // socket io
};

enum fwdsp_pipeline_role {
   FWDSP_ROLE_NONE = 0,
   FWDSP_ROLE_CODEC,
   FWDSP_ROLE_PROCESSOR
};

enum fwdsp_processor_io {
   FWDSP_PROCESSOR_IO_BIDIRECTIONAL = 0,
   FWDSP_PROCESSOR_IO_CAPTURE,
   FWDSP_PROCESSOR_IO_PLAYBACK
};

typedef void (*fwdsp_processor_output_cb)(const char *name,
   const void *samples, size_t len, void *user_data);

struct fwdsp_io_conn {
   struct fwdsp_subproc *sp;
   bool is_stderr;
};

// Local child IPC: high length bit marks codec/container initialization data.
#define FWDSP_FRAME_STREAM_HEADER 0x80000000U

#define FWDSP_CTRL_MAGIC          0x46574453U
#define FWDSP_CTRL_SET_VOLUME     1	// set volume
#define	FWDSP_CTRL_SHUTDOWN       2	// shut down process
#define	FWDSP_CTRL_CONFIGURE	  3	// Configure the pipeline
#define	FWDSP_CTRL_PAUSE	  4     // Pause the stream
#define	FWDSP_CTRL_RESUME	  5     // Resume the stream
#define	FWDSP_CTRL_FLUSH	  6     // Flush the buffer
#define	FWDSP_CTRL_START_RECORD	  7	// Start recording the raw audio
#define FWDSP_CTRL_STOP_RECORD    8     // Stop recording the raw audio
#define FWDSP_RECORD_USER_LEN     64
#define FWDSP_RECORD_ID_LEN       64
#define FWDSP_RECORD_FILE_LEN     512

struct fwdsp_control_msg {
   uint32_t magic;
   uint8_t type;
   uint8_t value;
   uint16_t reserved;
   // START_RECORD only: direction + 1 (0 means unspecified), and identity.
   // Local manager/child IPC; rebuild fwdsp and libfwdspmgr together.
   uint8_t record_direction;
   char record_user[FWDSP_RECORD_USER_LEN];
   char record_id[FWDSP_RECORD_ID_LEN];
   char record_file[FWDSP_RECORD_FILE_LEN];
};

struct fwdsp_subproc {
   pid_t pid;
   enum fwdsp_pipeline_role role;
   char pl_id[5];
   char processor_namespace[8];
   char processor_name[64];
   bool destroying;                    // teardown is in progress; ignore re-entry
   char channel_uuid[64];
   char pipeline[1024];
   bool is_tx;
   bool is_video;                        // video (webcam etc) pipeline? passed
                                         // as -v so fwdsp sets FW_MEDIA_VIDEO
   uint8_t *stream_headers;          // length-framed initialization packets
   size_t stream_headers_len;
   bool replay_headers;
   int refcount;
   time_t cleanup_deadline;
   time_t idle_since;
   time_t last_no_channel_warn;
   bool recording_active;
   int chan_id;
   enum fwdsp_io_type io_type;

   // stdio pipe FDs
   int fw_stdin;
   int fw_stdout;
   int fw_stderr;
   int fw_control;

#ifdef	USE_MONGOOSE
   // --- Mongoose tracking for polling ---
   struct mg_connection *mg_stdin_conn;
   struct mg_connection *mg_stdout_conn;
   struct mg_connection *mg_stderr_conn;
// Otherwise, we should probably use glib
#endif	// USE_MONGOOSE
   enum fwdsp_processor_io processor_io;
   fwdsp_processor_output_cb processor_output;
   void *processor_output_data;
};

extern void fwdsp_reap_children(void);
extern void fwdsp_send_stream_headers(const char *uuid, rrconn_t *cptr);
extern bool fwdsp_init(void);
extern bool fwdsp_fini(void);
//extern int fwdsp_find_offset(const char *id);
//extern struct fwdsp_subproc *fwdsp_find_instance(const char *id);
//extern struct fwdsp_subproc *fwdsp_create(const char *id, enum fwdsp_io_type io_type, bool is_tx);
extern struct fwdsp_subproc *fwdsp_find_or_create(const char *id, enum fwdsp_io_type io_type, bool is_tx);
//extern bool fwdsp_destroy(struct fwdsp_subproc *instance);
//extern bool fwdsp_spawn(struct fwdsp_subproc *sp, const char *path);
typedef void (*fwdsp_exit_cb_t)(struct fwdsp_subproc *sp, int status);
//extern void fwdsp_set_exit_cb(fwdsp_exit_cb_t cb);
extern bool ws_send_capab(struct mg_connection *c);
extern bool fwdsp_spawn(struct fwdsp_subproc *sp);
extern int fwdsp_get_chan_id(const char *magic, bool is_tx);
extern void fwdsp_sweep_expired(void);
extern struct fwdsp_subproc *fwdsp_start_stdio_from_list(const char *codec_list, bool tx_mode);
extern int fwdsp_codec_start(const char codec_id[5], bool is_tx, const char *channel_uuid);
extern bool fwdsp_write_samples(const char codec_id[5], bool is_tx, const void *data, size_t len);
// Write framed payload to the instance bound to a specific media channel.
// Returns true when all bytes are accepted by the child pipe.
extern bool fwdsp_write_channel_samples(const char codec_id[5], bool is_tx,
   const char *channel_uuid, const void *data, size_t len);
// Receive the decoded PCM tap from one active RX decoder.
extern bool fwdsp_codec_set_pcm_callback(const char codec_id[5], const char *channel_uuid,
   fwdsp_processor_output_cb output_cb, void *user_data);
extern bool fwdsp_cmd_setvol(const char codec_id[5], bool is_tx, int percent);
extern bool fwdsp_cmd_shutdown(const char codec_id[5], bool is_tx, int unused1);
extern int fwdsp_video_start(const char codec_id[5], bool is_tx);
extern int fwdsp_codec_stop(const char *codec, bool is_tx);
extern int fwdsp_codec_stop_channel(const char *codec, bool is_tx, const char *channel_uuid);
extern int fwdsp_codec_switch(const char *old_codec, const char *new_codec, bool is_tx, const char *channel_uuid);
extern struct fwdsp_subproc *fwdsp_find_channel_instance(const char *id, bool is_tx, const char *channel_uuid);
extern bool fwdsp_cmd_start_record_channel(const char codec_id[5], bool is_tx, const char *channel_uuid);
extern bool fwdsp_cmd_stop_record_channel(const char codec_id[5], bool is_tx, const char *channel_uuid);
extern struct fwdsp_subproc *fwdsp_find_instance(const char *id, bool is_tx);
extern int fwdsp_codec_stop_immediate(const char *codec, bool is_tx);
extern int fwdsp_codec_stop_channel_immediate(const char *codec, bool is_tx, const char *channel_uuid);
// -T audio pipelines exchange mono S16LE PCM at 16 kHz using the fwdsp framed
// stdin/stdout protocol. They may expose appsrc name=processor-src, appsink
// name=processor-sink, or both. If pipeline is NULL, use pipeline:proc.<name>.
// Capture and playback endpoints resolve to pipeline:src.<name> and
// pipeline:sink.<name>. fwdsp also accepts pipeline:recode.<name> for recoders.
// Output buffers are borrowed during the callback;
// callbacks run on the manager/event-loop thread and should return promptly.
extern bool fwdsp_processor_start(const char *name, const char *pipeline,
   fwdsp_processor_output_cb output_cb, void *user_data);
extern bool fwdsp_audio_capture_start(const char *name, const char *pipeline,
   fwdsp_processor_output_cb output_cb, void *user_data);
extern bool fwdsp_audio_playback_start(const char *name, const char *pipeline);
// Write one even-length PCM frame to a processor or playback endpoint. true
// means the complete framed sample data was accepted by the child pipe.
extern bool fwdsp_processor_write(const char *name, const void *samples, size_t len);
// Set the named processor's processor-vol element to 0..100 percent.
extern bool fwdsp_processor_setvol(const char *name, int percent);
// Stop a named endpoint. Pass a qualified name (src.rig0, sink.rig0,
// proc.effect-name, or recode.codec-name) when names may overlap.
extern bool fwdsp_processor_stop(const char *name);

#endif // !defined(__rr_fwdsp_mgr_h)
