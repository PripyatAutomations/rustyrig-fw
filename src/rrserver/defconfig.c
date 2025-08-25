//
// defcfg.c: Default configuration for rrserver
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "common/logger.h"
#include "common/util.file.h"
#include "common/posix.h"

const char *configs[] = { 
#ifndef _WIN32
   "~/.config/rrserver.cfg",
   "config/rrserver.cfg",
   "rrserver.cfg",
   "/etc/rustyrig/rrserver.cfg"
#else
   "%APPDATA%\\rrserver\\rrserver.cfg",
   ".\\rrserver.cfg"
#endif
};

const int num_configs = sizeof(configs) / sizeof(configs[0]);

defconfig_t defcfg[] = {
  { "audio.debug",	"false",	"Debug audio? [bool]" },
  { "backend.active",   "dummy",        "Backend to use for rig control" },
  { "codecs.allowed",   "mu16 pc16 mu08", "Preferred codec order" },
  { "debug.noisy-eeprom", "false",      "Extra debugging msgs from eeprom code?" },
  { "fwdsp.hangtime" ,  "60",		"How long should unused (en|de)coders be kept alive after last used?" },
  { "log.level",	"debug",	"How noisy should log be?"  }, 
  { "log.show-ts",	"false",        "Show timestamps in log? [bool]" },
  { "net.http.enabled", "true",         "Enable http?" },
  { "net.http.bind",    "127.0.0.1",    "Address to listen for HTTP" },
  { "net.http,port",    "8420",         "Port to listen for http on" },
  { "net.http.authdb",  "./config/http.users", "Path to user database" },
  { "net.http.authdb_dynamic", "false", "NYI: SQL user storage" },
  { "net.http.tls-bind", "127.0.0.1",   "Address to listen for HTTPS (TLS)" },
  { "net.http.tls-enabled", "false",    "Enable HTTPS (TLS) listener?" },
  { "net.http.www-root","./www",       "Path to static http content" },
  { "net.mqtt.enabled", "false",       "Enable MQTT service listener?" },
  { "net.mqtt.bind",    "127.0.0.1",   "Address to listen for mqtt" },
  { "path.db.master",   "./db/master.db", "Master database path" },
  { "path.db.master.template", "./sql/sqlite.master.sql", "Path to sql file to initialize database" },
  { "path.record-dir",  "./recordings", "TX & RX recordings basedir" },
//  { "testkey.defcfg.main", " 1",	"Test key for defconfig" },
  { NULL,		NULL,		NULL }
};
