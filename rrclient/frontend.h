//
// rrclient/frontend.h: frontend module host interface
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// The core client never links a GUI toolkit. A frontend (today only GTK)
// loads as a module and registers an ops table here at init; core code that
// previously reached into GTK widgets now calls these ops instead. The ops
// are NULL when no frontend is loaded, and every call site must treat that
// as a no-op.
//
// The module side talks back to the core ONLY through the event bus
// (event_on*/event_emit*) and public API (cfg, ui_print, cmd parsing). It
// must not reach into core state structs.
//
// Ownership: exactly one frontend may hold the slot. frontend_ops_register()
// returns false if a frontend is already registered. The module's shutdown
// MUST call frontend_ops_unregister() before its code is unloaded; the
// unregister also resets ui_mode so the core falls back safely.
//
#if     !defined(__rrclient_frontend_h)
#define __rrclient_frontend_h

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct rr_frontend_ops {
   const char *name;                        // "gtk" etc, for diagnostics

   /* Formatted output into a named chat/window (va_list form). The module owns formatting/colorization. */
   void (*vprint)(const char *window, const char *fmt, va_list ap);

   /* Present per-VFO observation state to the widgets. vfo is the VFO letter string ("A".."D"). Fields may be NULL/0 when unknown. The module decides
    * edit-delay suppression internally. */
   void (*vfo_state)(const char *vfo, long freq, const char *mode, int width, int power, bool ptt);

   /* Single-field echoes (server poll echo / ws.msg.cat). */
   void (*freq_set)(long freq);
   void (*mode_set)(const char *mode);
   void (*vfo_widths)(const char *vfo, const char *widths);

   /* Connection lifecycle. connected: 0 disconnected, 1 online. */
   void (*connection_state)(int connected);

   /* Chat rooms. */
   void (*chat_room_add)(const char *room);
   void (*chat_room_remove)(const char *room);
   void (*chat_room_topic)(const char *room, const char *topic);
   void (*chat_show_status)(void);
   void (*chat_set_authoritative_room)(const char *room);
   void (*chat_room_vfos_changed)(const char *room);
   /* Selected chat tab's room, or NULL. The returned string remains owned by the frontend and is valid until the next frontend call. */
   const char *(*chat_current_room)(void);

   /* Private-query tab management. */
   void (*chat_query_add)(const char *who);

   /* Server connection button reflects connect/auth state. */
   void (*conn_button_update)(int state);

   /* PTT presentation. */
   void (*ptt_set_online)(bool online);
   void (*ptt_set_state)(bool active);      // reflect server state
   void (*ptt_tot_expired)(int tot_secs);
   void (*ptt_refresh)(void);               // users TX state changed
   /* Hotkey toggle entry point (alt-enter/ctrl-space). Returns false when the frontend handled it. */
   bool (*ptt_hotkey_toggle)(void);

   /* Codec picker reflects the current selected channel's codec. */
   void (*codec_set_active)(bool is_tx, const char *codec);

   /* Syslog tab clear (client /clearlog). */
   void (*syslog_clear)(void);

   /* Tab/notebook focus by tab name: "admin"|"config"|"log"|"status". */
   void (*focus_tab)(const char *tab);
   /* Switch to notebook page by 1-based index (client /win N). */
   void (*switch_window)(int id);

   /* Modal confirmation; returns false when the user declined or no frontend can ask (callers must treat "no frontend" as confirmed). */
   bool (*confirm_dialog)(const char *message);

   /* Open the config editor window (path may be NULL for default). */
   void (*edit_config)(const char *path);

   /* Non-fatal alert/error dialog (server alerts, protocol errors). */
   void (*alert)(const char *message);

   /* Bell: audio alert on chat if supported. */
   void (*bell)(void);

   /* Desktop notification (best effort). */
   void (*notify)(const char *title, const char *message);

   /* Server chooser window (client /server with no args). */
   void (*show_server_chooser)(void);

   /* Webcam viewer window visibility (client /webcam SHOW|HIDE). */
   void (*webcam_show)(bool show);

   /* Userlist presentation changed (redraw). */
   void (*userlist_redraw)(void);
   void (*userlist_set_visible)(bool visible);
   void (*userlist_room_vfos_changed)(const char *room);

   /* Clear the active chat scrollback (client /clear). */
   void (*chat_clear)(void);

   /* Volume slider set by /vol (0-100). */
   void (*rx_volume)(int value);

   /* Main loop: initialize the frontend (creates windows), then run the frontend's main loop until exit. run() returns when the loop ends. */
   bool (*init)(int *argc, char ***argv);
   void (*run)(void);
   /* Request the frontend main loop to quit (idempotent). */
   void (*quit)(void);

   /* Called by the module glue when shutting down: destroy windows/sources.
    * (Internal to the GTK module.) */
   void (*stop)(void);

   /* True only for the shared status chat tab, not other frontend pages. */
   bool (*chat_status_active)(void);
} rr_frontend_ops_t;

/* Register the frontend ops. Returns false if a frontend is already registered. Takes no ownership of ops; the pointed-to table must outlive the registration
 * (static storage in the module). */
extern bool frontend_ops_register(const rr_frontend_ops_t *ops);

/* Unregister. Safe to call when nothing is registered. After this the core treats every op as a no-op and ui_mode falls back to TUI. */
extern void frontend_ops_unregister(void);

/* The currently registered ops, or NULL. */
extern const rr_frontend_ops_t *frontend_ops(void);

/* True when a frontend module owns the UI. */
extern bool frontend_present(void);

#endif // !defined(__rrclient_frontend_h)
