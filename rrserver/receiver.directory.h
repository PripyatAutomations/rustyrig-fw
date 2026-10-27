/* External SDR directory snapshots; distinct from live rig inventory. */
#ifndef RRSERVER_RECEIVER_DIRECTORY_H
#define RRSERVER_RECEIVER_DIRECTORY_H
#include <stdbool.h>
bool rrserver_receiver_directory_init(void);
void rrserver_receiver_directory_shutdown(void);
#endif
