//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* External SDR directory snapshots; distinct from live rig inventory. */
#ifndef RRSERVER_RECEIVER_DIRECTORY_H
#define RRSERVER_RECEIVER_DIRECTORY_H
#include <stdbool.h>
bool rrserver_receiver_directory_init(void);
void rrserver_receiver_directory_shutdown(void);
#endif
