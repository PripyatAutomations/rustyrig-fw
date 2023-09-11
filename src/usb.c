/*
 * This file contains support for presenting as a USB device to the host
 *
 * For now, we only support stm32, feel free to write linux USB gadget support
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#if defined(HOST_POSIX)
#endif
#include "state.h"
#include "usb.h"
extern struct GlobalState rig;	// Global state
