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
//   { "server.auto-connect",		NULL,	"Profile name to autoconnect to" },
#ifdef _WIN32
   { "ui.gtk.theme",        "Windows10", "Chosen light theme" },
   { "ui.gtk.theme.dark",   "Windows10-Dark", "Chosen dark theme" },
//#else
//   { "ui.gtk.theme",        NULL, 	"Chosen light theme" },
//   { "ui.gtk.theme.dark",   NULL,   "Chosen dark theme" },
#endif
   { "ui.show-pings",			"true", "Show Ping? Pong! notices" },
   { NULL,				NULL, 		NULL }
};
