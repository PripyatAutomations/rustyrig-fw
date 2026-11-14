//
// util.vna.c
//    This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
/*
 * util_vna.c: Lightweight Vector Network Analyzer features
 *
 * Here we support using the DDS to sweep the antenna and provide a data set for calculating L&C values in the ant_tuner code later.
 *
 * Someone implement this ;)
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <rrserver/util.vna.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
