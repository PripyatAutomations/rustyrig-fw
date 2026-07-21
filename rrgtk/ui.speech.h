//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rrgtk_ui_speech_h)
#define	__rrgtk_ui_speech_h
#include <librustyaxe/config.h>
#pragma once
#include <gtk/gtk.h>
#include <stdbool.h>

typedef enum {
   UI_ROLE_GENERIC,
   UI_ROLE_BUTTON,
   UI_ROLE_ENTRY,
   UI_ROLE_LABEL,
   UI_ROLE_CHECKBOX,
   UI_ROLE_SLIDER,
   UI_ROLE_COMBOBOX
} ui_role_t;

typedef struct {
   const char *name;
   const char *description;
   ui_role_t role;
   bool focusable;
} ui_speech_hint_t;

extern void ui_speech_apply(GtkWidget *widget, const ui_speech_hint_t *hint);

/**
 * Convenience wrapper: pass fields directly instead of building a struct.
 */
extern void ui_speech_set(GtkWidget *widget,
                   const char *name,
                   const char *description,
                   ui_role_t role,
                   bool focusable);


#endif	// !defined(__rrgtk_ui_speech_h)
