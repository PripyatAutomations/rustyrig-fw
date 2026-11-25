/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef RRCLIENT_UI_LAYOUT_H
#define RRCLIENT_UI_LAYOUT_H
#include <librustyui/form.h>
typedef struct {
   const char *id;
   rrui_kind_t kind;
   bool masked;
} rrclient_ui_required_t;
/* Load first usable layout, user > administrator > distribution. */
rrui_form_t *rrclient_ui_layout_load(const char *file,rrui_kind_t root,
   const rrclient_ui_required_t *required,size_t count,rrui_action_fn action);
#endif
