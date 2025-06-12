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
   { "audio.debug",				":*3" },
   { "audio.pipeline.rx",			"" },
   { "audio.pipeline.rx.format",		"" },
   { "audio.pipeline.tx",			"" },
   { "audio.pipeline.tx.format",		"" },
   { "audio.pipeline.rx.pcm16", 		"" },
   { "audio.pipeline.tx.pcm16",			"" },
   { "audio.pipeline.rx.pcm44",			"" },
   { "audio.pipeline.tx.pcm44",			"" },
   { "audio.pipeline.rx.opus",			"" },
   { "audio.pipeline.tx.opus",			"" },
   { "audio.pipeline.rx.flac",			"" },
   { "audio.pipeline.tx.flac",			"" },
   { "audio.volume.rx",				"30" },
   { "audio.volume.tx",				"20" },
   { "cat.poll-blocking",			"2" },
   { "debug.http",				"false" },
   { "default.tx.power",			"30" },
   { "log.level",				"debug" },
   { "log.show-ts",				"true" },
   { "server.auto-connect",			"" },
   { "ui.main.height",				"600" },
   { "ui.main.width",				"800" },
   { "ui.main.x",				"0" },
   { "ui.main.y",				"200" },
   { "ui.main.on-top",				"false" },
   { "ui.main.raised",				"true" },
   { "ui.userlist.height",			"300" },
   { "ui.userlist.width",			"250" },
   { "ui.userlist.x",				"0" },
   { "ui.userlist.y",				"0" },
   { "ui.userlist.on-top",			"false" },
   { "ui.userlist.raised",			"true" },
   { "ui.userlist.hidden",			"false" },
   { "ui.show-pings",				"true" },
   { NULL,					NULL }
};
