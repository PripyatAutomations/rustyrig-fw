//
// rrclient/defconfig.c: Here we store the hard-coded default configuration
//                    which is used for keys missing in the user's config
//
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// XXX: This needs updated!
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
   { "cat.poll-blocking", "2", "Sec to block CAT poll messages for input debouncing" },
   { "debug.audio", ":*3", "gstreamer debug level" },
   { "debug.http", "false", "Extra HTTP debugging" },
   { "debug.http.crazy", "false", "Insane level of HTTP debugging" },
   { "debug.sockets", "false", "Extra SOCKET debugging" },
   { "default.tx.power", "30", "Default TX power in watts (float)" },
   { "debug.loglevel", "debug", "Log level (audit | crit | warn | info | debug | crazy)" },
   { "debug.show-ts", "true", "Show timestamps in log" },
   { "path.help-dir", "./help", "Path to find help-files" },
   { "rig0.volume.rx", "50", "rig0: Speaker volume" },
   { "server.auto-connect", NULL, "Profile name to autoconnect on start" },
   { "ui.full-screen", "false", "Go full-screen at start?" },
#ifdef _WIN32
   // windows hosts usually dont already have a gtk3 theme, so default to the included windows 10 theme
   { "ui.gtk.theme", "Windows10", "Chosen light theme" },
   { "ui.gtk.theme.dark", "Windows10-Dark", "Chosen dark theme" },
#else
   // On non-windows hosts, prefer the active theme in gtk3
   { "ui.gtk.theme", NULL, "Chosen light theme" },
   { "ui.gtk.theme.dark", NULL, "Chosen dark theme" },
#endif
   { "ui.show-pings", "true", "Show Ping? Pong! notices" },
   {
      NULL, NULL, NULL
   }
};
