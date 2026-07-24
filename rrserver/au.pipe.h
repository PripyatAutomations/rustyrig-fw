//
// au.pipe.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if	!defined(__rr_au_pipe_h)
#define __rr_au_pipe_h

#include <stdbool.h>
#include <stddef.h>

// Pipe-specific audio device structure
typedef struct {
    int pipe_fd;  // File descriptor for the pipe/socket
} rr_au_pipe_device_t;

// Initialize a pipe for audio communication
bool pipe_init(rr_au_pipe_device_t *device, const char *pipe_name);

// Write PCM audio samples to the pipe
bool pipe_write_samples(rr_au_pipe_device_t *device, const void *samples, size_t size);

// Read PCM audio samples from the pipe
bool pipe_read_samples(rr_au_pipe_device_t *device, void *buffer, size_t size);

// Cleanup resources associated with the pipe
void pipe_cleanup(rr_au_pipe_device_t *device);

#endif // __rr_au_pipe_h
