#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "common/dict.h"
#include "common/config.h"
#include "common/logger.h"
#include "common/util.file.h"
#include "common/posix.h"

defconfig_t defcfg[] = {
   { "audio.debug",			":*3",	"gstreamer debug level" },
   { "audio.pipeline.rx",		"",	"User choice pipeline for RX" },
   { "audio.pipeline.rx.format",	"",	"User pipeline format (bytes|time) for RX " },
   { "audio.pipeline.tx",		"",	"User choice pipeline for gstreamer TX" },
   { "audio.pipeline.tx.format",	"",	"User pipeline format (bytes|time) for TX"},
   { "audio.pipeline.rx.pcm16", 	"",	"Pipeline: PCM @ 16khz RX" },
   { "audio.pipeline.tx.pcm16",		"",	"Pipeline: PCM @ 16khz TX" },
   { "audio.pipeline.rx.pcm44",		"",	"Pipeline: PCM @ 44khz RX" },
   { "audio.pipeline.tx.pcm44",		"",	"Pipeline: PCM @ 44khz TX" },
   { "audio.pipeline.rx.opus",		"",	"Pipeline: OPUS RX" },
   { "audio.pipeline.tx.opus",		"",	"Pipeline: OPUS TX" },
   { "audio.pipeline.rx.flac",		"",	"Pipeline: FLAC RX" },
   { "audio.pipeline.tx.flac",		"",	"Pipeline: FLAC TX" },
   { "audio.prefer-codecs",		"mu16 pc16 mu08", "Preferred codec order" },
   { "audio.volume.rx",			"30",	"Default RX volume" },
   { "audio.volume.tx",			"20",	"Default TX out vol" },
   { "cat.poll-blocking",		"2",	"Sec to block CAT poll messages for input debouncing" },
   { "debug.http",			"false", "Extra HTTP debugging" },
   { "default.tx.power",		"30",	"Default TX power in watts (float)" },
   { "log.level",			"debug", "Log level (audit | crit | warn | info | debug | crazy)" },
   { "log.show-ts",			"true", "Show timestamps in log" },
   { "server.auto-connect",		NULL,	"Profile name to autoconnect to" },
 #ifdef _WIN32
   { "ui.gtk.theme",        "Windows10", "Chosen light theme" },
   { "ui.gtk.theme.dark",   "Windows10-Dark", "Chosen dark theme" },
#else
   { "ui.gtk.theme",        NULL, 	"Chosen light theme" },
   { "ui.gtk.theme.dark",   NULL,   "Chosen dark theme" },
#endif
   { "ui.main.height",			"600",	"Height (px) of main window" },
   { "ui.main.width",			"800",	"Width (px) of main window" },
   { "ui.main.x",			"0",	"X position of main window" },
   { "ui.main.y",			"200",	"Y position of main window" },
   { "ui.main.on-top",			"false", "Main window stays above others" },
   { "ui.main.raised",			"true", "Show the window at startup" },
   { "ui.userlist.height",		"300",	"Height of userlist" },
   { "ui.userlist.width",		"250",	"Width of userlist" },
   { "ui.userlist.x",			"0",	"X pos of userlist" },
   { "ui.userlist.y",			"0",	"Y pos of userlist" },
   { "ui.userlist.on-top",		"false", "Userlist window stays above others" },
   { "ui.userlist.raised",		"true", "Show userlist at startup" },
   { "ui.userlist.hidden",		"false", "Hide the userlist?" },
   { "ui.show-pings",			"true", "Show Ping? Pong! notices" },
   { NULL,				NULL, 		NULL }
};
