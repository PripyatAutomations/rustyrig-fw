//
// inc/rrserver/fwdsp-mgr.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_fwdsp_mgr_h)
#define	__rr_fwdsp_mgr_h

enum fwdsp_io_type {
    FW_IO_NONE = 0,		// invalid
    FW_IO_STDIO,		// stdin/stdout
    FW_IO_SOCKET		// socket io
};

struct fwdsp_io_conn {
   struct fwdsp_subproc *sp;
   bool is_stderr;
};

struct fwdsp_subproc {
   pid_t        pid;
   char         pl_id[5];
   char         pipeline[1024];
   bool         is_tx;
   int          refcount;
   time_t       cleanup_deadline;
   int          chan_id;
   enum fwdsp_io_type io_type;

   // stdio pipe FDs
   int          fw_stdin;
   int          fw_stdout;
   int          fw_stderr;

   // --- Mongoose tracking for polling ---
   struct mg_connection *mg_stdout_conn;
   struct mg_connection *mg_stderr_conn;
};

extern bool fwdsp_init(void);
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

#endif	// !defined(__rr_fwdsp_mgr_h)
