// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_CONTROL_GRAPH_H
#define RRCLIENT_CONTROL_GRAPH_H
#include <rrclient/audio.logic.h>
#include <stdbool.h>
#include <stddef.h>
#define RR_CONTROL_MAX_GATES 32
#define RR_CONTROL_MAX_SOURCES 32
#define RR_CONTROL_MAX_WIRES 256
#define RR_CONTROL_NAME_LEN 64
/* Canonical public port suffixes: in_a..in_z, in_aa..in_af, out_a.
 * Index-based stored graph wires remain unchanged for compatibility. */
bool rr_control_graph_port_name(bool output,unsigned index,char *buf,size_t size);
bool rr_control_graph_port_index(const char *name,bool *output,unsigned *index);
typedef struct rr_control_graph rr_control_graph_t;
/* A source can stand for PTT, COS or any other boolean value; type is
 * intentionally not encoded in its name. No sink can key the transmitter
 * without the independent, existing TX authorization/interlock checks. */
/* Validate a versioned, untrusted graph snapshot against the same bounded
 * topology evaluator on client and server. No physical outputs are applied. */
bool rr_control_graph_text_validate(const char *snapshot,char **error);
rr_control_graph_t *rr_control_graph_new(void);
void rr_control_graph_free(rr_control_graph_t *g);
bool rr_control_graph_source_add(rr_control_graph_t *g,const char *name);
bool rr_control_graph_source_set(rr_control_graph_t *g,const char *name,bool valid,bool high);
bool rr_control_graph_source_remove(rr_control_graph_t *g,const char *name);
bool rr_control_graph_gate_add(rr_control_graph_t *g,const char *name,rr_gate_kind_t kind,unsigned inputs);
bool rr_control_graph_gate_configure(rr_control_graph_t *g,const char *name,rr_gate_kind_t kind,unsigned inputs);
bool rr_control_graph_gate_remove(rr_control_graph_t *g,const char *name);
bool rr_control_graph_connect(rr_control_graph_t *g,const char *from,const char *to,unsigned pin);
bool rr_control_graph_disconnect(rr_control_graph_t *g,const char *from,const char *to,unsigned pin);
bool rr_control_graph_output(rr_control_graph_t *g,const char *name,bool *valid,bool *high);
size_t rr_control_graph_wire_count(const rr_control_graph_t *g);
#endif
