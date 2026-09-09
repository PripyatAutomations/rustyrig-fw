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

const char *configs[] = {
#ifdef _WIN32
   "%APPDATA%\\rustyrigs\\rrclient.cfg",
   ".\\config\\rrclient.cfg"
#else
   "./config/rrclient.cfg",
   "~/.config/rrclient.cfg",
   "~/.rrclient.cfg",
   "/etc/rrclient.cfg"
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
   "#log-view { font-family: \"Monospace\"; font-size: 12pt; }\n" \
   "#freq-digit, #freq-digit-button { font-family: \"Monospace\"; font-size: 12pt; }\n" \
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
   { "audio.pipeline.rx", "", "User choice pipeline for RX" },
   { "audio.pipeline.rx.format", "", "User pipeline format (bytes|time) for RX " },
   { "audio.pipeline.tx", "", "User choice pipeline for gstreamer TX" },
   { "audio.pipeline.tx.format", "", "User pipeline format (bytes|time) for TX" },
   { "audio.pipeline.rx.pcm16", "", "Pipeline: PCM @ 16khz RX" },
   { "audio.pipeline.tx.pcm16", "", "Pipeline: PCM @ 16khz TX" },
   { "audio.pipeline.rx.pcm44", "", "Pipeline: PCM @ 44khz RX" },
   { "audio.pipeline.tx.pcm44", "", "Pipeline: PCM @ 44khz TX" },
   { "audio.pipeline.rx.opus", "", "Pipeline: OPUS RX" },
   { "audio.pipeline.tx.opus", "", "Pipeline: OPUS TX" },
   { "audio.pipeline.rx.flac", "", "Pipeline: FLAC RX" },
   { "audio.pipeline.tx.flac", "", "Pipeline: FLAC TX" },
   { "audio.prefer-codecs", "mu16 pc16 mu08", "Preferred codec order" },
   { "audio.volume.rx", "30", "Default RX volume" },
   { "audio.volume.tx", "20", "Default TX out vol" },
   { "codecs.allowed", "mu08 mu16", "CODECs to support by default" },
   { "cat.pty.enable", "true", "Create a PTY (e.g. ./dev/ttyCAT0) for external CAT software (hamlib/rigctl)" },
   { "cat.pty.path", "./dev/ttyCAT0", "Path to symlink the CAT PTY slave to" },
   { "debug.audio", ":*3", "gstreamer debug level" },
   { "debug.http", "false", "Extra HTTP debugging" },
   { "debug.http.crazy", "false", "Insane level of HTTP debugging" },
   { "debug.sockets", "false", "Extra SOCKET debugging" },
   { "default.tx.power", "30", "Default TX power in watts (float)" },
   { "debug.loglevel", "debug", "Log level (audit | crit | warn | info | debug | crazy)" },
   { "debug.show-ts", "true", "Show timestamps in log" },
   { "log.file", "rrclient.log", "Where to log" },
   { "log.level", "info", "What level of log events to keep" },
   { "net.http.hex-dump", "false", "Should we hexdump all http traffic?" },
   { "networks.auto", NULL, "Which networks to autoconnect to" },
   { "path.help-dir", "./help", "Path to find help-files" },
   { "path.modules", "./modules", "Where to store modules" },
   { "rig0.volume.rx", "50", "rig0: Speaker volume" },
   { "ui.edit-delay", "3", "Seconds to suppress freq echoes after a local freq edit" },
   { "server.auto-connect", NULL, "Profile name to autoconnect on start" },
   { "tui.use-color", "true", "Enable color in the TUI?" },
#ifdef	USE_GTK
   { "ui.full-screen", "false", "Go full-screen at start?" },
   { "ui.gtk.vfo-on-top", "false", "Place VFO controls at top of the rig window?" },
   { "ui.gtk.main-tabstrip", "bottom", "Placement of main tabstrip: left,right,bottom,top" },
   // All of the GTK CSS lives here; the [gtk-css] section of the config file
   // overrides this, and can be reloaded at runtime with /css-reload
   { "ui.gtk.css", DEFAULT_CSS, "GTK CSS for the client UI (see also [gtk-css] config section)" },
   { "ui.gtk.vfo-docked", "true", "NYI: Docked or floating VFO?" },
   { "ui.gtk.scrollback.chat", "200", "Max chat scrollback lines (0 = unlimited)" },
   { "ui.gtk.scrollback.syslog", "200", "Max syslog tab scrollback lines (0 = unlimited)" },
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
   { "ui.theme.completion", "bright-magenta", "Color tag used for tab-completion candidates" },
   { "ui.theme.headers", "cyan", "Color tag used for headers (help banner, section titles)" },
   { "ui.ptt-ack-timeout", "2", "How long to wait for server to ACK ptt button?" },
   { "ui.bell.chat", "true", "Dings in chat for new messages?" },
   { "ui.bell.chat-highlight", "./sounds/uh-oh.wav", "Sound to play instead of a ding for msgs with our username in them" },
   { "ui.freqentry.scroll-divider", "1.0", "Scroll divider for VFO widgets, if needed" },
   { "ui.show-pings", "true", "Show Ping? Pong! notices" },
   { "ui.auto-show-userlist", "true", "Show the userlist when connected, hide when disconnected?" },
   { "ui.vfo.visocity", "1000", "Second to block CAT poll messages for input debouncing" },
   { NULL, NULL, NULL }
};
