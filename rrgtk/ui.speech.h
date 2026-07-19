#if	!defined(__rrclient_ui_speech_h)
#define	__rrclient_ui_speech_h
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


#endif	// !defined(__rrclient_ui_speech_h)
