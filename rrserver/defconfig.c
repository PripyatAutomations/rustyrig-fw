//
// defcfg.c: Default configuration for rrserver
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include <librustyaxe/config.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/logger.h>
#include <librustyaxe/util.file.h>
#include <librustyaxe/posix.h>

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
   { "audio.debug", "false", "Debug audio? [bool]" },
   { "backend.active", "internal", "Backend to use for rig control" },
   { "backend.announce-interval", "30", "How often to send a forced update of VFO state?" },
   { "backend.hamlib-baud", "38400", "Rig baud rate (if serial)" },
   { "backend.hamlib-debug", "warn", "Hamlib debug level" },
   { "backend.poll-interal", "1000", "How often in ms to poll the rig state?" },
   { "backend.hamlib-port", "127.0.0.1:4532", "What hamlib device to use (def: rigctld localhost)" },
   { "chat.log", "true", "Should we log the chat to text files by date/rig?" },
   { "chat.replay-lines", "20", "Lines of replay to show on joining chat" },
   { "codecs.allowed", "mu16 pc16 mu08", "Preferred codec order" },
   { "core.daemonize", "false", "Should we go to background after starting?" },
   { "debug.noisy-eeprom", "false", "Extra debugging msgs from eeprom code?" },
   { "debug.mongoose", "false", "Debug mongoose?" },
   { "debug.show-ts", "false", "Show timestamps in log? [bool]" },
   { "features.auto-block-ptt", "false", "Block PTT at start?" },
   { "fwdsp.hangtime", "60", "How long should unused (en|de)coders be kept alive after last used?" },
   { "log.file", "rrserver.log", "Where to log?" },
   { "log.level", "*:info", "What to log?" },
   { "net.http.enabled", "true", "Enable http?" },
   { "net.http.bind", "127.0.0.1", "Address to listen for HTTP" },
   { "net.http.port", "8420", "Port to listen for http on" },
   { "net.http.authdb", "./config/http.users", "Path to user database" },
   { "net.http.authdb_dynamic", "false", "NYI: SQL user storage" },
   { "net.http.hex-dump", "false", "Hex dump http? (Noisy!)" },
   { "net.http.port", "8420", "HTTP listner port" },
   { "net.http.tls-bind", "127.0.0.1", "Address to listen for HTTPS (TLS)" },
   { "net.http.tls-enabled", "false", "Enable HTTPS (TLS) listener?" },
   { "net.http.tls-port", "8443", "Port for TLS listener" },
   { "net.http.www-root", "./www", "Path to static http content" },
   // MQTT client
   { "net.mqtt-client.secret-file", "./config/mqtt-client.secrets", "Where is the mqtt client credentials?" },
   // XXX: Merge this into mqtt://user:password@host:port format, ex: mqtt://rig1:test@10.10.10.10:18383
   { "net.mqtt-client.host", NULL, "To be removed" },
   { "net.mqtt-client.port", NULL, "To be removed" },
   { "net.mqtt-client.user", NULL, "To be removed" },
   // MQTT server
   { "net.mqtt.bind", "127.0.0.1", "Address to listen for mqtt" },
   { "net.mqtt.enabled", "false", "Enable MQTT service listener?" },
   { "net.mqtt.port", "48383", "Port for MQTT to listen" },
   // [Embedded] This stuff only applies to embedded targets
   { "net.vlan", "4420", "VLAN to use for ethernet interface, 0 for untagged" },
   // [/Embedded] This stuff only applies to embedded targets
   { "path.db.master", "./db/master.db", "Master database path" },
   { "path.db.master.template", "./sql/sqlite.master.sql", "Path to sql file to initialize database" },
   { "path.record-dir", "./recordings", "TX & RX recordings basedir" },
   { "record.max", "16", "Maximum concurrent audio recordings" },
   { "rig.warmup-required", "false", "Does rig require warmup time?" },
   { "rig.warmup-time", "30", "Required rig warmup time" },
//  { "testkey.defcfg.main", " 1",  "Test key for defconfig" },
   { NULL, NULL, NULL }
};
