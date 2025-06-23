//
// inc/rustyrig/fwdsp-mgr.h
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
    FW_IO_AF_UNIX
};

struct fwdsp_subproc {
   pid_t 	pid;		// Process ID
   char		pl_id[4];	// pipeline ID
   char         pipeline[512];  // pipeline string
   bool		is_tx;		// Is this a TX channel?

   enum fwdsp_io_type io_type;	// instance io type
   //// Only one of these will be used, either stdin/stdout/stderr or unix_sock depending on above io_type
   int		fw_stdin;	// redirected stdin
   int		fw_stdout;	// redirrcted stdout
   int		fw_stderr;	// redirected stderr
   //// -- or --
   int		unix_sock;	// unix socket
   /////
};

extern bool fwdsp_init(void);
extern int fwdsp_find_offset(const char *id);
extern struct fwdsp_subproc *fwdsp_find_instance(const char *id);
extern struct fwdsp_subproc *fwdsp_create(const char *id, enum fwdsp_io_type io_type, bool is_tx);
extern struct fwdsp_subproc *fwdsp_find_or_create(const char *id, enum fwdsp_io_type io_type, bool is_tx);
extern bool fwdsp_destroy(struct fwdsp_subproc *instance);
extern bool fwdsp_spawn(struct fwdsp_subproc *sp, const char *path);

typedef void (*fwdsp_exit_cb_t)(struct fwdsp_subproc *sp, int status);
extern void fwdsp_set_exit_cb(fwdsp_exit_cb_t cb);
extern bool ws_send_capab(struct mg_connection *c);

#endif	// !defined(__rr_fwdsp_mgr_h)
