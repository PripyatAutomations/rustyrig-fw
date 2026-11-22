// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_AUDIO_LOGIC_H
#define RRCLIENT_AUDIO_LOGIC_H
#include <stdbool.h>
#include <stddef.h>
/* Boolean control plane primitives, intentionally independent of audio/GTK.
 * Gate evaluation never authorizes PTT: the downstream interlock remains
 * the sole authority for transmitter keying. */
typedef enum {
   RR_GATE_AND, RR_GATE_OR, RR_GATE_XOR, RR_GATE_NOT,
   RR_GATE_NAND, RR_GATE_NOR
} rr_gate_kind_t;
typedef struct {
   rr_gate_kind_t kind;
   unsigned inputs;             /* 1 for NOT, 2..32 otherwise */
   unsigned present;            /* bitmask of connected and fresh inputs */
   unsigned high;               /* bitmask of asserted inputs */
} rr_gate_state_t;
/* Missing or stale inputs are NOT treated as logical zero; evaluation
 * returns false and forces output low (fail closed). */
bool rr_gate_evaluate(const rr_gate_state_t *gate, bool *output);
const char *rr_gate_kind_name(rr_gate_kind_t kind);
bool rr_gate_kind_parse(const char *name, rr_gate_kind_t *out);
#endif
