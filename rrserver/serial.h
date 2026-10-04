#ifndef RRSERVER_SERIAL_H
#define RRSERVER_SERIAL_H
#include <stdbool.h>
void rrserver_serial_init(void);
void rrserver_serial_fini(void);
bool rrserver_serial_poll(void);
#endif
