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
   RR_GATE_NAND, RR_GATE_NOR, RR_GATE_XNOR
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
/* One pin may receive any number of boolean wires. A pin is asserted if
 * ANY attached wire is high (wired-OR); a gate then applies its operation
 * across its *pins*. Unconnected pins or stale/invalid wire values make
 * the whole evaluation unavailable and force output low, including NOT,
 * NAND and NOR. PTT is ordinary boolean signal data here.
 *
 * The caller owns wires and is responsible for validating pin indexes,
 * preventing combinational feedback cycles and enforcing TX authorization
 * at the eventual physical sink.
 */
typedef struct {
   unsigned pin;             /* 0 .. gate.inputs-1 */
   bool valid;               /* false = disconnected/stale/unavailable */
   bool high;
} rr_gate_wire_t;
bool rr_gate_evaluate_wires(rr_gate_kind_t kind, unsigned inputs,
                            const rr_gate_wire_t *wires, size_t wire_count,
                            bool *output);

const char *rr_gate_kind_name(rr_gate_kind_t kind);
bool rr_gate_kind_parse(const char *name, rr_gate_kind_t *out);
/* Numeric threshold/comparator. Boundaries are inclusive: min <= value <= max.
 * Each numeric pin may override its configured constant. Unconnected min
 * defaults to 0; unconnected max defaults to +INFINITY (no upper bound).
 * The optional boolean signal pin enables comparison (unconnected = true).
 * Connected pins whose data is stale/invalid fail LOW, even for NOT/NAND-like
 * downstream logic. The validity return indicates whether output is usable. */
typedef struct {
   double value, min, max;
   bool value_valid;           /* The numeric INPUT pin is required. */
   bool min_connected, min_valid;
   bool max_connected, max_valid;
   bool signal_connected, signal_valid, signal_high;
   bool invert;                /* Property default; XORed with active INVERT pin. */
   bool invert_connected, invert_valid, invert_high; /* Optional boolean input. */
} rr_threshold_state_t;
/* Constant TRUE/FALSE outputs for reusable control blocks; independent
 * of a comparator's own input validity. */
bool rr_logic_constant(bool high);
rr_threshold_state_t rr_threshold_default(void);
bool rr_threshold_evaluate(const rr_threshold_state_t *state, bool *output);
#endif
