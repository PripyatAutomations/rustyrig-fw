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

struct fwdsp_control_msg {
   uint32_t magic;
   uint8_t type;
   uint8_t value;
   uint16_t reserved;
   // START_RECORD only: direction + 1 (0 means unspecified), and identity.
   // Local manager/child IPC; rebuild fwdsp and libfwdspmgr together.
   uint8_t record_direction;
   char record_user[FWDSP_RECORD_USER_LEN];
};

struct fwdsp_subproc {
   pid_t pid;
   char pl_id[5];
   char channel_uuid[64];
   char pipeline[1024];
   bool is_tx;
   bool is_video;                        // video (webcam etc) pipeline? passed
                                         // as -v so fwdsp sets FW_MEDIA_VIDEO
   bool is_transcoder;                    // is this a transcoder? If so it'll
                                         // have tc_* below set
   uint8_t *stream_headers;          // length-framed initialization packets
   size_t stream_headers_len;
   bool replay_headers;
   int refcount;
   time_t cleanup_deadline;
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
   // transcoder stuff
   char tc_in_codec[5];                          // Input codec
   int tc_in_channel;                            // Input channel
   char tc_out_codec[5];                         // Output codec
   int tc_out_channel;                           // Output channel
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

#endif // !defined(__rr_fwdsp_mgr_h)
