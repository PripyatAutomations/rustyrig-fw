#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct au_shm_ctx au_shm_ctx;

// open a connection to an existing shmsink (for capture)
//   path: socket-path, e.g. "/tmp/rrws-in"
// returns ctx or NULL
au_shm_ctx *au_shm_open_reader(const char *path);

// create a shmsink socket to accept one shmsrc (for playback)
//   path: socket-path
au_shm_ctx *au_shm_open_writer(const char *path);

// close and free
void au_shm_close(au_shm_ctx *ctx);

// blocking read into buf, returns number of bytes or <0 on error/EOF
int au_shm_read(au_shm_ctx *ctx, void *buf, size_t n);

// blocking write of n bytes, returns 0 on success
int au_shm_write(au_shm_ctx *ctx, const void *buf, size_t n);

////////////////////////
struct au_channel {
   // Channel metadata
   u_int32_t	ch_id;		// Channel ID
   u_int32_t	ch_4cc;		// 4byte CC
   // Channel state
   u_int32_t	ch_subscribers;	// # of active subscribers
   u_int32_t	ch_lastseq;	// Last sent seq number

   // gstreamer shm interface
   au_shm_ctx  *gst_shm;

   // XXX: Should we store pending buffers here?
};
