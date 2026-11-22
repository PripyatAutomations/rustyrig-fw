/* GTK-neutral, value-only RustyScript GUI messages. SPDX-License-Identifier: GPL-2.0-only */
#ifndef RRCLIENT_SCRIPT_GUI_H
#define RRCLIENT_SCRIPT_GUI_H
#include <stdint.h>
#define RR_SCRIPT_GUI_COMMAND "client.script.gui.command"
#define RR_SCRIPT_GUI_CHANGE "client.script.gui.change"
#define RR_SCRIPT_STARTUP_ERRORS_EVENT "client.script.startup-errors"
#define RR_SCRIPT_GUI_PROTOCOL_VERSION 1u
/* All messages are owned by the event bus; never contain toolkit pointers. */
enum {
   RR_GUI_PANEL=1, RR_GUI_DIALOG, RR_GUI_ADD, RR_GUI_SET,
   RR_GUI_SHOW, RR_GUI_HIDE, RR_GUI_DESTROY, RR_GUI_RELEASE,
   RR_GUI_MESSAGE /* prebuilt info/confirmation dialog */
};
typedef struct rr_script_gui_command {
   uint32_t version;
   uint32_t action;
   char owner[32]; /* opaque VM lifetime token, never a GTK address */
   char id[64];
   char parent[64];
   char kind[24];
   char text[256];
   char value[256];
} rr_script_gui_command_t;
typedef struct rr_script_gui_change {
   uint32_t version;
   char owner[32];
   char id[64];
   char kind[24];
   char value[256];
} rr_script_gui_change_t;
#endif
