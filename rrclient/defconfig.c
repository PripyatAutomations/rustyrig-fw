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
   "~/.config/rrclient.cfg",
   "/etc/rustyrig/rrclient.cfg"
#endif
};

const int num_configs = sizeof(configs) / sizeof(configs[0]);

#define DEFAULT_CSS \
        /* Fonts: GTK/Pango picks family + size; override these in [gtk-css] */ \
        "button { font-family: \"Sans\"; font-size: 11pt; }\n" \
        "label { font-family: \"Sans\"; font-size: 11pt; }\n" \
        "@define-color rr_chat_highlight_nick yellow;\n" \
        "#chat-view { font-family: \"Monospace\"; font-size: 12pt; }\n" \
        "#log-view, #host-log-view { font-family: \"Monospace\"; font-size: 12pt; }\n" \
        "#freq-digit, #freq-digit-button { font-family: \"Monospace\"; font-size: 12pt; }\n" \
        "#room-vfo-frequency { font-family: \"DSEG7 Classic\", \"Digital-7\", \"DejaVu Sans Mono\", \"Monospace\"; font-size: 14pt; font-weight: bold; }\n" \
        "#room-vfo-row.room-vfo-active { background-color: #092b59; color: #f3faff; border: 1px solid #4d81b3; border-radius: 3px; }\n" \
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

defconfig_t defcfg[] = {
   {
      "chat.send.burst", "4", "Client chat messages allowed in an immediate burst (1-100)", DEFCONFIG_UINT, NULL
   },
   {
      "chat.send.interval-ms", "2000", "Milliseconds between replenished outgoing chat tokens (100-60000)", DEFCONFIG_UINT, NULL
   },
   {
      "chat.send.queue-limit", "64", "Maximum locally queued outgoing chat messages (1-1000)", DEFCONFIG_UINT, NULL
   },
   {
      "receiver-directory.url", "https://cdn.aethersdr.com/kiwi.json", "Temporary HTTPS receiver directory mirror; empty disables fetching"
   },
   {
      "receiver-directory.cache", "", "Local directory cache file; empty uses the user cache directory"
   },
   {
      "dcc.bind", "0.0.0.0", "DCC CHAT/SEND TCP bind address"
   },
   {
      "dcc.host", "", "Advertised DCC CHAT/SEND IPv4 address; empty uses the IRC socket address"
   },
   {
      "dcc.port-min", "10000", "First DCC CHAT/SEND TCP listening port", DEFCONFIG_UINT, NULL
   },
   {
      "dcc.port-max", "10099", "Last DCC CHAT/SEND TCP listening port", DEFCONFIG_UINT, NULL
   },
   {
      "dcc.audio.bind", "0.0.0.0", "Client-to-client DCC audio UDP bind address"
   },
   {
      "dcc.audio.host", "", "Advertised DCC IPv4 address; empty uses IRC connection's local address"
   },
   {
      "dcc.audio.port-min", "10000", "First client DCC audio UDP port", DEFCONFIG_UINT, NULL
   },
   {
      "dcc.audio.port-max", "10099", "Last client DCC audio UDP port", DEFCONFIG_UINT, NULL
   },
   {
      "dcc.audio.encrypt", "false", "Encrypt DCC audio when compiled with RTP encryption", DEFCONFIG_BOOL, NULL
   },
   {
      "dcc.audio.mute", "false", "Mute microphone on client-to-client DCC audio sessions", DEFCONFIG_BOOL, NULL
   },
   {
      "media.direct.prefer", "true", "Prefer authenticated direct RTP audio when available", DEFCONFIG_BOOL, NULL
   },
   {
      "server.reconnect-interval", "1", "Initial reconnect delay (1-59 seconds; legacy 60 resets to 1), doubles to 60; server profiles may override"
   },
   {
      "server.proxy", "", "Native SOCKS5 proxy URL; empty connects directly"
   },
   {
      "server.proxy.user", "", "Optional SOCKS5 authentication username"
   },
   {
      "server.proxy.pass", "", "Optional SOCKS5 authentication password"
   },
   FWDSP_VIDEO_PIPELINE_DEFAULTS FWDSP_AUDIO_PIPELINE_DEFAULTS(FWDSP_RIG_PCM_SOURCE) {
      "audio.test-mode", "true", "Show and advertise tone/pink test codecs", DEFCONFIG_BOOL, NULL
   },
   {
      "audio.mute-talkback", "true", "Mute RX stream during TX to suppress talkback", DEFCONFIG_BOOL, NULL
   },
   {
      "audio.volume.rx", "30", "Default RX volume", DEFCONFIG_UINT, NULL
   },
   {
      "audio.volume.tx", "40", "Default TX out vol", DEFCONFIG_UINT, NULL
   },
   {
      "codecs.allowed", FWDSP_DEFAULT_CODECS, "CODECs to support by default"
   },
   {
      "client.role", "", "Connection role: set to video-source for webcam feed connections"
   },
   {
      "callsign-lookup:cache-db", "$HOME/.local/state/rustyrig/rrclient-callsigns.db", "Client-local callsign lookup cache database"
   },
   {
      "callsign-lookup:use-cache", "true", "Cache local callsign lookup results"
   },
   {
      "callsign-lookup:path", "/usr/bin/callsign-lookup", "Callsign lookup helper executable"
   },
   {
      "webcam.device", "/dev/video0", "v4l2 device to grab frames from"
   },
   {
      "serial.ttyCAT0", NULL, "Optional pinned CAT endpoint; numbered ports are never created automatically"
   },
   {
      "serial.ttyHOST0", NULL, "Optional remote serial binding: host:<server endpoint>"
   },
   {
      "serial:ttyHOST0.baud", NULL, "Optional initial baud override; otherwise inherit server settings"
   },
   {
      "serial:ttyHOST0.mode", NULL, "Optional line-mode override; otherwise inherit server settings"
   },
   {
      "serial:ttyHOST0.buffer-bytes", "65536", "Bounded serial buffer; 0 keeps only one transfer block", DEFCONFIG_INT,
      NULL
   },
   {
      "serial:ttyCAT0.buffer-bytes", "8192", "Bounded local serial write buffer; 0 keeps one transfer block",
      DEFCONFIG_INT, NULL
   },
   {
      "serial:ttyCAT0.type", "pty", "Serial transport: pty, serial device or append-only capture file", DEFCONFIG_ENUM, "pty serial file"
   },
   {
      "serial:ttyCAT0.path", NULL, "PTY link or real serial device; defaults to cat.pty.path for ttyCAT0"
   },
   {
      "serial:ttyCAT0.mode", "8n1", "Serial data/parity/stop mode"
   },
   {
      "serial:ttyCAT0.vfo", "A", "CAT VFO for commands without an explicit selector"
   },
   {
      "serial:ttyCAT0.baud", "9600", "Serial baud rate (1200 through 115200 supported rates)", DEFCONFIG_INT, NULL
   },
   {
      "cat.pty.enable", "true", "Legacy CAT PTY option (floating ttyCAT is always available)",
      DEFCONFIG_BOOL, NULL
   },
   {
      "cat.pty.path", "$HOME/dev/ttyCAT", "Legacy floating ttyCAT PTY symlink path"
   },
   {
      "default.tx.power", "30", "Initial TX power slider percentage (0 inhibits PTT ON)", DEFCONFIG_FLOAT, NULL
   },
   {
      "log.audio", ":*3", "GStreamer debug level"
   },
   {
      "log.file", "rrclient.log", "Where to log"
   },
   {
      "log.http", "false", "Extra HTTP logging", DEFCONFIG_BOOL, NULL
   },
   {
      "log.http.crazy", "false", "HTTP wire logging", DEFCONFIG_BOOL, NULL
   },
   {
      "log.level", "info,event:debug", "What level of log events to keep"
   },
   {
      "log.show-ts", "true", "Show timestamps in log", DEFCONFIG_BOOL, NULL
   },
   {
      "net.http.hex-dump", "false", "Should we hexdump all http traffic?", DEFCONFIG_BOOL, NULL
   },
   {
      "networks.auto", NULL, "Which networks to autoconnect to"
   },
   {
      "path.help-dir", "/var/lib/rustyrig/help", "Path to find help-files"
   },
   {
      "fwdsp:path", "/usr/bin/fwdsp", "Path to fwdsp binary"
   },
   {
      "fwdsp:subproc.max", "24", "Maximum client fwdsp processes", DEFCONFIG_UINT, NULL
   },
   {
      "fwdsp:hangtime", "5", "Seconds to keep unused client fwdsp alive", DEFCONFIG_UINT, NULL
   },
   {
      "fwdsp:pcm-hub", "true", "Route decoded client RX PCM to sink.client.dsp0", DEFCONFIG_BOOL, NULL
   },
   {
      "site:coordinates", NULL, "Station coordinates as latitude,longitude (optional)"
   },
   {
      "site:gridsquare", NULL, "Station Maidenhead grid square (optional)"
   },
   {
      "path.modules", "/usr/lib/rustyrig/modules/rrclient", "Where to find loadable modules"
   },
   {
      "ui.edit-delay", "3", "Seconds to suppress freq echoes after a local freq edit", DEFCONFIG_UINT, NULL
   },
   {
      "server.auto-connect", NULL, "Profile name to autoconnect on start"
   },
   {
      "server.sasl.user", "", "IRC SASL account; empty disables PLAIN authentication"
   },
   {
      "server.sasl.pass", "", "IRC SASL password (separate from server.pass)"
   },
   {
      "server.sasl.mechanism", "PLAIN", "IRC SASL mechanism: PLAIN or EXTERNAL"
   },
   {
      "irc.nick", "nonick", "Fallback IRC nickname when the server profile has no server.user"
   },
   {
      "tui.status-line", RRCLIENT_DEFAULT_STATUS_LINE, "Top row template with live ${variable} and IRC formatting escapes"
   },
   {
      "tui.room-status-line", RRCLIENT_DEFAULT_ROOM_STATUS_LINE, "Top row template for rooms without an attached VFO"
   },
   {
      "tui.status-chat", "false", "Allow unprefixed text from the status view to use the authoritative room",
      DEFCONFIG_BOOL, NULL
   },
   {
      "tui.use-color", "true", "Enable color in the TUI?", DEFCONFIG_BOOL, NULL
   },
   {
      "tui.use-mouse", "true", "Enable mouse in the TUI?", DEFCONFIG_BOOL, NULL
   },
/* GTK is a loadable frontend; the host must retain its defaults even when rrclient itself is compiled without USE_GTK. */
   {
      "ui.full-screen", "true", "Go full-screen at start?", DEFCONFIG_BOOL, NULL
   },
   {
      "ui.screensaver", "matrix", "Optional fullscreen idle screensaver (matrix or none)", DEFCONFIG_ENUM, "matrix|none"
   },
   {
      "ui.screensaver.timeout", "3600", "Idle seconds before screensaver; 0 disables automatic launch", DEFCONFIG_UINT, NULL
   },
   {
      "ui.gtk.scale-on-resize", "true", "Scale GTK controls to window size relative to its monitor", DEFCONFIG_BOOL, NULL
   },
   {
      "ui.gtk.zoom", "100", "GTK interface zoom percent (25-300); Ctrl +/- resizes window and fonts, Alt +/- fonts only", DEFCONFIG_UINT, NULL
   },
   {
      "ui.gtk.vfo-on-top", "false", "Place VFO controls at top of the rig window?", DEFCONFIG_BOOL, NULL
   },
   {
      "ui.gtk.main-tabstrip", "bottom", "Placement of main tabstrip: left,right,bottom,top", DEFCONFIG_ENUM,
      "left|right|bottom|top"
   },
   {
      "ui.gtk.vfo-docked", "true", "NYI: Docked or floating VFO?", DEFCONFIG_BOOL, NULL
   },
   {
      "ui.gtk.scrollback.chat", "200", "Max chat scrollback lines (0 = unlimited)", DEFCONFIG_UINT, NULL
   },
   {
      "ui.gtk.scrollback.syslog", "200", "Max syslog tab scrollback lines (0 = unlimited)", DEFCONFIG_UINT, NULL
   },
#ifdef _WIN32
   // windows hosts usually dont already have a gtk3 theme, so default to the included
   // windows 10 theme
   {
      "ui.gtk.theme", "Windows10", "Chosen light theme"
   },
   {
      "ui.gtk.theme.dark", "Windows10-Dark", "Chosen dark theme"
   },
#else // _WIN32
   // On non-windows hosts, prefer the active theme in gtk3
   {
      "ui.gtk.theme", NULL, "Chosen light theme"
   },
   {
      "ui.gtk.theme.dark", NULL, "Chosen dark theme"
   },
#endif // _WIN32
   {
      "ui.theme.completion", "\00310", "mIRC color prefix for tab-completion candidates"
   },
   {
      "ui.win-digit-wait", "500", "Milliseconds to wait for a second Escape/Alt window number digit", DEFCONFIG_UINT, NULL
   },
   {
      "ui.ptt-ack-timeout", "2", "How long to wait for server to ACK ptt button?", DEFCONFIG_UINT, NULL
   },
   {
      "ui.ptt-hold-delay", "500", "Milliseconds before a PTT shortcut becomes hold-to-talk", DEFCONFIG_UINT, NULL
   },
   {
      "ui.bell.chat", "false", "Dings in chat for new messages?", DEFCONFIG_BOOL, NULL
   },
   {
      "ui.bell.chat-other", "", "Sound to play for normal chat messages"
   },
   {
      "ui.bell.chat-highlight", "",
      "Sound to play instead of a ding for msgs with our username in them"
   },
   {
      "ui.freqentry.scroll-divider", "1.0", "Scroll divider for VFO widgets, if needed", DEFCONFIG_FLOAT, NULL
   },
   {
      "ui.show-pings", "false", "Show Ping? Pong! notices", DEFCONFIG_BOOL, NULL
   },
   {
      "ui.auto-show-userlist", "true", "Restore the userlist when connected?", DEFCONFIG_BOOL, NULL
   },
   {
      "ui.userlist-width", "220", "Default width in pixels for GTK room user lists", DEFCONFIG_UINT, NULL
   },
   {
      "ui.userlist.irc.width", "0", "GTK irc roster width (0 fits all fields initially)", DEFCONFIG_UINT, NULL
   },
   {
      "ui.userlist.irc.height", "320", "GTK irc roster height", DEFCONFIG_UINT, NULL
   },
   {
      "ui.userlist.irc.dock-width", "0", "GTK irc roster dock-width (0 fits all fields initially)", DEFCONFIG_UINT, NULL
   },
   {
      "ui.userlist.irc.columns", "0,1,2,3,4", "GTK irc roster columns", DEFCONFIG_STRING, NULL
   },
   {
      "ui.userlist.vfo.width", "0", "GTK vfo roster width (0 fits all fields initially)", DEFCONFIG_UINT, NULL
   },
   {
      "ui.userlist.vfo.height", "320", "GTK vfo roster height", DEFCONFIG_UINT, NULL
   },
   {
      "ui.userlist.vfo.dock-width", "0", "GTK vfo roster dock-width (0 fits all fields initially)", DEFCONFIG_UINT, NULL
   },
   {
      "ui.userlist.vfo.columns", "0,1,2,3,4", "GTK vfo roster columns", DEFCONFIG_STRING, NULL
   },
   {
      "ui.userlist.room.width", "0", "GTK room roster width (0 fits all fields initially)", DEFCONFIG_UINT, NULL
   },
   {
      "ui.userlist.room.height", "320", "GTK room roster height", DEFCONFIG_UINT, NULL
   },
   {
      "ui.userlist.room.dock-width", "0", "GTK room roster dock-width (0 fits all fields initially)", DEFCONFIG_UINT, NULL
   },
   {
      "ui.userlist.room.columns", "0,1,2,3,4", "GTK room roster columns", DEFCONFIG_STRING, NULL
   },
   {
      "ui.save-on-exit", "false", "Save window placements and config on exit?", DEFCONFIG_BOOL, NULL
   },
   {
      "ui.shared-input-history", "true", "Share chat input history between rooms", DEFCONFIG_BOOL, NULL
   },
   {
      "ui.vfo.visocity", "1000", "Second to block CAT poll messages for input debouncing", DEFCONFIG_UINT, NULL
   },
   {
      NULL, NULL, NULL
   }
};
