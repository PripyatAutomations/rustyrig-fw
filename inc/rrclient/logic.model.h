/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef RRCLIENT_LOGIC_MODEL_H
#define RRCLIENT_LOGIC_MODEL_H
#include <stdbool.h>
#include <stdint.h>
#include <glib.h>
#include <rrclient/control.graph.h>
#define MAX_UI_GATES RR_CONTROL_MAX_GATES
#define MAX_UI_WIRES RR_CONTROL_MAX_WIRES
typedef struct {
   char id[RR_CONTROL_NAME_LEN];
   rr_gate_kind_t kind;
   unsigned inputs;
   gboolean constant, level; /* Constant nodes are ordinary reusable sources. */
   double x,y;
   gboolean sign;
   char label[64];
   uint32_t color; /* ON background, legacy key */
   uint32_t on_fg,off_fg,off_bg;
   gboolean border;
   char font[96];
} gate_view_t;
typedef struct {
   char from[RR_CONTROL_NAME_LEN],to[RR_CONTROL_NAME_LEN];
   unsigned pin;
} wire_view_t;

extern gate_view_t rr_logic_blocks[MAX_UI_GATES];
extern wire_view_t rr_logic_wires[MAX_UI_WIRES];
extern unsigned rr_logic_block_count,rr_logic_wire_count;
extern rr_control_graph_t *rr_logic_model;
void rr_logic_model_init(void);
gate_view_t *rr_logic_find(const char *id);
void rr_logic_save(void);
gboolean rr_logic_connect(const char *src,const char *dst,unsigned pin,gboolean save);
gboolean rr_logic_disconnect_index(unsigned i);
gboolean rr_logic_remove(const char *id);
gate_view_t *rr_logic_add(rr_gate_kind_t kind,unsigned inputs,bool constant,bool level);
gate_view_t *rr_logic_add_sign(const char *label,uint32_t color);
gboolean rr_logic_sign_configure(const char *id,const char *label,uint32_t color);
gboolean rr_logic_sign_style(const char *id,const char *label,uint32_t on_fg,uint32_t on_bg,uint32_t off_fg,uint32_t off_bg,const char *font,gboolean border);
gboolean rr_logic_sign_state(const char *id,bool *valid,bool *high);
gboolean rr_logic_configure(const char *id,rr_gate_kind_t kind,unsigned inputs);
gboolean rr_logic_output(const char *name,bool *valid,bool *high);
void rr_logic_signal(const char *name,bool valid,bool high);
void rr_logic_reset_sources(void);
gchar *rr_logic_export(void);
gboolean rr_logic_import(const char *serialized);
#endif
