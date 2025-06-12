
//
// au.pipe.c: Pipe/socket interface for audio samples
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we deal with common GUI operations between HMI and framebuffer
//
#include "build_config.h"
#include "rustyrig/au.h"
#include "../ext/libmongoose//mongoose.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>


typedef struct {
    int pipe_fd;  // File descriptor for the pipe/socket
} rr_au_pipe_device_t;

bool pipe_init(rr_au_pipe_device_t *device, const char *pipe_name) {
    device->pipe_fd = open(pipe_name, O_RDWR);
    return device->pipe_fd >= 0;
}

bool pipe_write_samples(rr_au_pipe_device_t *device, const void *samples, size_t size) {
    return write(device->pipe_fd, samples, size) == size;
}

bool pipe_read_samples(rr_au_pipe_device_t *device, void *buffer, size_t size) {
    return read(device->pipe_fd, buffer, size) == size;
}

void pipe_cleanup(rr_au_pipe_device_t *device) {
    if (device->pipe_fd >= 0) {
       close(device->pipe_fd);
    }
}

rr_au_backend_interface_t au_backend_pipe = {
    .backend_type = AU_BACKEND_PIPE,
//    .init = pipe_init,
//    .write_samples = pipe_write_samples,
//    .read_samples = pipe_read_samples,
//    .cleanup = pipe_cleanup
};

