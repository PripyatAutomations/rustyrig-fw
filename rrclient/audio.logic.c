// SPDX-License-Identifier: GPL-2.0-only
#include <rrclient/audio.logic.h>
#include <string.h>
#include <strings.h>
#include <math.h>
static const char *const names[] = {"AND","OR","XOR","NOT","NAND","NOR","XNOR"};
const char *rr_gate_kind_name(rr_gate_kind_t kind) {
   return (unsigned)kind <= RR_GATE_XNOR ? names[kind] : NULL;
}
bool rr_gate_kind_parse(const char *name, rr_gate_kind_t *out) {
   if (!name || !out) return false;
   for (unsigned i=0; i<sizeof(names)/sizeof(names[0]); ++i)
      if (!strcasecmp(name,names[i])) { *out=(rr_gate_kind_t)i; return true; }
   return false;
}
bool rr_gate_evaluate(const rr_gate_state_t *gate, bool *output) {
   if (!output) return false;
   *output=false;
   if (!gate || (unsigned)gate->(unsigned)kind > RR_GATE_XNOR ||
       (gate->kind == RR_GATE_NOT ? gate->inputs != 1 :
          gate->inputs < 2 || gate->inputs > 32))
      return false;
   const unsigned expected = gate->inputs == 32 ? ~0u : (1u << gate->inputs)-1u;
   if ((gate->present & expected) != expected)
      return false;
   const unsigned active=gate->high & expected;
   bool result=false;
   switch (gate->kind) {
      case RR_GATE_AND:  result=active == expected; break;
      case RR_GATE_OR:   result=active != 0; break;
      case RR_GATE_XOR:  result=__builtin_parity(active); break;
      case RR_GATE_NOT:  result=active == 0; break;
      case RR_GATE_NAND: result=active != expected; break;
      case RR_GATE_NOR:  result=active == 0; break;
      case RR_GATE_XNOR: result=!__builtin_parity(active); break;
      default: return false;
   }
   *output=result;
   return true;
}

bool rr_gate_evaluate_wires(rr_gate_kind_t kind, unsigned inputs,
                            const rr_gate_wire_t *wires, size_t wire_count,
                            bool *output) {
   if (!output) return false;
   *output=false;
   if (kind > RR_GATE_XNOR || (kind == RR_GATE_NOT ? inputs != 1 :
       inputs < 2 || inputs > 32) || (wire_count && !wires))
      return false;
   rr_gate_state_t gate = {.kind=kind, .inputs=inputs};
   /* Any stale connected source invalidates the entire gate, even when a
    * different source has already asserted the same pin. Otherwise NOT,
    * NOR or NAND could assert on an incomplete input snapshot. */
   for (size_t i=0; i<wire_count; ++i) {
      if (wires[i].pin >= inputs || !wires[i].valid)
         return false;
      const unsigned bit=1u<<wires[i].pin;
      gate.present |= bit;
      if (wires[i].high) gate.high |= bit;
   }
   return rr_gate_evaluate(&gate,output);
}

bool rr_logic_constant(bool high) {
   return high;
}

rr_threshold_state_t rr_threshold_default(void) {
   return (rr_threshold_state_t){.min=0., .max=INFINITY};
}
bool rr_threshold_evaluate(const rr_threshold_state_t *state, bool *output) {
   if (!output) return false;
   *output=false;
   if (!state || !state->value_valid || !isfinite(state->value) ||
       (state->signal_connected && !state->signal_valid) ||
       (state->invert_connected && !state->invert_valid) ||
       (state->min_connected && !state->min_valid) ||
       (state->max_connected && !state->max_valid))
      return false;

   /* Connected bounds are required to be finite. Unconnected max may be
    * infinity so the default gate is simply value >= 0. */
   if (!isfinite(state->min) || isnan(state->max) ||
       (state->max_connected && !isfinite(state->max)) ||
       state->min > state->max)
      return false;
   /* A disabled valid signal produces a valid FALSE result. */
   if (state->signal_connected && !state->signal_high)
      return true;
   bool inside=state->value >= state->min && state->value <= state->max;
   const bool inverted = state->invert ^ (state->invert_connected && state->invert_high);
   *output=inverted ? !inside : inside;
   return true;
}
