//
// rrclient/defconfig.c: Here we store the hard-coded default configuration
//                    which is used for keys missing in the user's config
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <fwdsp/default-pipelines.h>
#include <rrclient/ui.statusbar.h>

const char *configs[] = {
#ifdef _WIN32
   "%APPDATA%\\rustyrigs\\rrclient.cfg",
   ".\\config\\rrclient.cfg"
#else
   "./config/rrclient.cfg",
   "~/.config/rrclient.cfg",
   "~/.rrclient.cfg",
   "/etc/rustyrig/rrclient.cfg"
#endif
};

const int num_configs = sizeof(configs) / sizeof(configs[0]);

#ifdef	USE_GTK
// Default GTK CSS for the client UI. The [gtk-css] section of the config
// file overrides this, and can be reloaded at runtime with /css-reload
// Use dark green with bold white text so the online/PTT buttons are readable
#define	DEFAULT_CSS \
   /* Fonts: GTK/Pango picks family + size; override these in [gtk-css] */ \
   "button { font-family: \"Sans\"; font-size: 11pt; }\n" \
   "label { font-family: \"Sans\"; font-size: 11pt; }\n" \
   "#chat-view { font-family: \"Monospace\"; font-size: 12pt; }\n" \
   "#log-view, #host-log-view { font-family: \"Monospace\"; font-size: 12pt; }\n" \
   "#freq-digit, #freq-digit-button { font-family: \"Monospace\"; font-size: 12pt; }\n" \
   "#room-vfo-frequency { font-family: \"Monospace\"; font-size: 16pt; font-weight: bold; }\n" \
   ".ptt-active { background: #b00000; color: white; font-weight: bold; }\n" \
   ".ptt-idle { background: #0a7a0a; color: white; font-weight: bold; }\n" \
   ".ptt-pending { background: #e6c200; color: black; font-weight: bold; }\n" \
   ".ptt-offline { background: #555555; color: white; font-weight: bold; }\n" \
   ".ptt-tot { background: #e07000; color: black; font-weight: bold; }\n" \
   ".conn-active { background: #0a7a0a; color: white; font-weight: bold; }\n" \
   ".conn-pending { background: #e6c200; color: black; font-weight: bold; }\n" \
   ".conn-idle { background: #b00000; color: white; font-weight: bold; }\n" \
   "/* Userlist flag icons (👑⭐👤👀🎤🙊🧙🐣): size the columns/cells here */\n" \
   "#userlist-tree, #userlist-tree.userlist-icon { font-family: \"Sans\"; font-size: 12pt; }"

const char *default_css = DEFAULT_CSS;
#endif	// USE_GTK

defconfig_t defcfg[] = {
   FWDSP_AUDIO_PIPELINE_DEFAULTS(FWDSP_RIG_PCM_SOURCE)
   { "audio.prefer-codecs", FWDSP_DEFAULT_CODECS, "Preferred codec order" },
   { "audio.volume.rx", "30", "Default RX volume", DEFCONFIG_UINT, NULL },
   { "audio.volume.tx", "40", "Default TX out vol", DEFCONFIG_UINT, NULL },
   { "codecs.allowed", FWDSP_DEFAULT_CODECS, "CODECs to support by default" },
   { "client.role", "", "Connection role: set to video-source for webcam feed connections" },
   { "callsign-lookup:cache-db", "./db/rrclient-callsigns.db", "Client-local callsign lookup cache database" },
   { "callsign-lookup:use-cache", "true", "Cache local callsign lookup results" },
   { "callsign-lookup:path", "./bin/callsign-lookup", "Callsign lookup helper executable" },
   { "webcam.device", "/dev/video0", "v4l2 device to grab frames from" },
   { "cat.pty.enable", "true", "Create a PTY (e.g. ~/ttyCAT0) for external CAT software (hamlib/rigctl)", DEFCONFIG_BOOL, NULL },
   { "cat.pty.path", "~/ttyCAT0", "Path to symlink the CAT PTY slave to" },
   { "debug.audio", ":*3", "gstreamer debug level" },
   { "debug.http", "false", "Extra HTTP debugging", DEFCONFIG_BOOL, NULL },
   { "debug.http.crazy", "false", "Insane level of HTTP debugging", DEFCONFIG_BOOL, NULL },
   { "debug.sockets", "false", "Extra SOCKET debugging", DEFCONFIG_BOOL, NULL },
   { "default.tx.power", "30", "Default TX power in watts (float)", DEFCONFIG_FLOAT, NULL },
   { "debug.loglevel", "debug", "Log level (audit | crit | warn | info | debug | crazy)", DEFCONFIG_ENUM, "audit|crit|warn|info|debug|crazy" },
   { "debug.show-ts", "true", "Show timestamps in log", DEFCONFIG_BOOL, NULL },
   { "log.file", "rrclient.log", "Where to log" },
   { "log.level", "info", "What level of log events to keep" },
   { "net.http.hex-dump", "false", "Should we hexdump all http traffic?", DEFCONFIG_BOOL, NULL },
   { "networks.auto", NULL, "Which networks to autoconnect to" },
   { "path.help-dir", "./help", "Path to find help-files" },
   { "path.fwdsp", "./bin/fwdsp", "Path to fwdsp binary" },
   { "fwdsp:path", "./bin/fwdsp", "Path to fwdsp binary" },
   { "path.fwdsp.config", "./config/fwdsp.cfg", "Path to fwdsp configuration" },
   { "fwdsp:subproc.max", "4", "Maximum client fwdsp processes", DEFCONFIG_UINT, NULL },
   { "fwdsp:hangtime", "5", "Seconds to keep unused client fwdsp alive", DEFCONFIG_UINT, NULL },
   { "fwdsp:pcm-hub", "true", "Route decoded client RX PCM to sink.client.dsp0", DEFCONFIG_BOOL, NULL },
   { "path.record-dir", "./recordings", "Audio recording directory" },
   { "site:coordinates", NULL, "Station coordinates as latitude,longitude (optional)" },
   { "site:gridsquare", NULL, "Station Maidenhead grid square (optional)" },
   { "record.rx", "false", "Record received audio", DEFCONFIG_BOOL, NULL },
   { "record.tx", "false", "Record transmitted audio", DEFCONFIG_BOOL, NULL },
   { "record.buffer-size", "524288", "Raw audio recording ring size in bytes", DEFCONFIG_UINT, NULL },
   { "path.modules", "./modules", "Where to store modules" },
   { "rig0.volume.rx", "50", "rig0: Speaker volume", DEFCONFIG_UINT, NULL },
   { "ui.edit-delay", "3", "Seconds to suppress freq echoes after a local freq edit", DEFCONFIG_UINT, NULL },
   { "server.auto-connect", NULL, "Profile name to autoconnect on start" },
   { "tui.status-line", RRCLIENT_DEFAULT_STATUS_LINE, "Top row template with live ${variable} and {color} escapes" },
   { "tui.room-status-line", RRCLIENT_DEFAULT_ROOM_STATUS_LINE, "Top row template for rooms without an attached VFO" },
   { "tui.status-chat", "false", "Allow unprefixed text from the status view to use the authoritative room", DEFCONFIG_BOOL, NULL },
   { "tui.use-color", "true", "Enable color in the TUI?", DEFCONFIG_BOOL, NULL },
   { "tui.use-mouse", "true", "Enable mouse in the TUI?", DEFCONFIG_BOOL, NULL },
#ifdef	USE_GTK
   { "ui.full-screen", "false", "Go full-screen at start?", DEFCONFIG_BOOL, NULL },
   { "ui.gtk.vfo-on-top", "false", "Place VFO controls at top of the rig window?", DEFCONFIG_BOOL, NULL },
   { "ui.gtk.main-tabstrip", "bottom", "Placement of main tabstrip: left,right,bottom,top", DEFCONFIG_ENUM, "left|right|bottom|top" },
   // All of the GTK CSS lives here; the [gtk-css] section of the config file
   // overrides this, and can be reloaded at runtime with /css-reload
   { "ui.gtk.css", DEFAULT_CSS, "GTK CSS for the client UI (see also [gtk-css] config section)" },
   { "ui.gtk.vfo-docked", "true", "NYI: Docked or floating VFO?", DEFCONFIG_BOOL, NULL },
   { "ui.gtk.scrollback.chat", "200", "Max chat scrollback lines (0 = unlimited)", DEFCONFIG_UINT, NULL },
   { "ui.gtk.scrollback.syslog", "200", "Max syslog tab scrollback lines (0 = unlimited)", DEFCONFIG_UINT, NULL },
#endif	// USE_GTK
#ifdef _WIN32
   // windows hosts usually dont already have a gtk3 theme, so default to the included windows 10 theme
   { "ui.gtk.theme", "Windows10", "Chosen light theme" },
   { "ui.gtk.theme.dark", "Windows10-Dark", "Chosen dark theme" },
#else	// _WIN32
   // On non-windows hosts, prefer the active theme in gtk3
   { "ui.gtk.theme", NULL, "Chosen light theme" },
   { "ui.gtk.theme.dark", NULL, "Chosen dark theme" },
#endif	// _WIN32
   { "ui.theme.completion", "cyan", "Color tag used for tab-completion candidates" },
   { "ui.theme.headers", "cyan", "Color tag used for headers (help banner, section titles)" },
   { "ui.ptt-ack-timeout", "2", "How long to wait for server to ACK ptt button?", DEFCONFIG_UINT, NULL },
   { "ui.bell.chat", "false", "Dings in chat for new messages?", DEFCONFIG_BOOL, NULL },
   { "ui.bell.chat-other", "./sounds-ding.wav", "Sound to play for normal chat messages" },
   { "ui.bell.chat-highlight", "./sounds/uh-oh.wav", "Sound to play instead of a ding for msgs with our username in them" },
   { "ui.freqentry.scroll-divider", "1.0", "Scroll divider for VFO widgets, if needed", DEFCONFIG_FLOAT, NULL },
   { "ui.show-pings", "false", "Show Ping? Pong! notices", DEFCONFIG_BOOL, NULL },
   { "ui.auto-show-userlist", "true", "Show the userlist when connected, hide when disconnected?", DEFCONFIG_BOOL, NULL },
   { "ui.userlist-width", "220", "Default width in pixels for GTK room user lists", DEFCONFIG_UINT, NULL },
   { "ui.save-on-exit", "false", "Save window placements and config on exit?", DEFCONFIG_BOOL, NULL },
   { "ui.shared-input-history", "true", "Share chat input history between rooms", DEFCONFIG_BOOL, NULL },
   { "ui.vfo.visocity", "1000", "Second to block CAT poll messages for input debouncing", DEFCONFIG_UINT, NULL },
   { NULL, NULL, NULL }
};
