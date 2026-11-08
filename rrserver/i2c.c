//
// rrserver/i2c.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
/*
 * Here we deal with i2c messages, most importantly logging them
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/i2c-dev.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/i2c.h>
#include <rrserver/i2c.hal.h>

static uint32_t file_descriptor = -1;
static const char *i2c_bus = "/dev/i2c-1";

// Initialize i2c communications
// addr: My address
/*
 *  uint32_t i2c_init(uint32_t bus, uint32_t addr) {
 *   Log(LOG_INFO, "i2c", "Initializing i2c bus %d, my address is [0x%x]", bus, addr);
 *   return 0;
 *  }
 */
