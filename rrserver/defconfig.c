//
// rrserver/defcfg.c: Default configuration for rrserver
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include "build_config.h"
#include <fwdsp/default-pipelines.h>
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
   FWDSP_AUDIO_PIPELINE_DEFAULTS(FWDSP_RIG_PCM_SOURCE)
   { "audio.debug", "false", "Debug audio? [bool]", DEFCONFIG_BOOL, NULL },
   { "atu.max", "4", "Maximum number of ATUs", DEFCONFIG_UINT, NULL },
   { "backend.active", "internal", "Backend to use for rig control", DEFCONFIG_ENUM, "internal|hamlib" },
   { "backend.announce-interval", "30", "How often to send a forced update of VFO state?", DEFCONFIG_UINT, NULL },
   { "backend.state-interval", "15", "Send cat state at most once every N seconds if unchanged?", DEFCONFIG_UINT, NULL },
   { "backend.hamlib-baud", "38400", "Rig baud rate (if serial)", DEFCONFIG_UINT, NULL },
   { "backend.hamlib-debug", "warn", "Hamlib debug level" },
   { "backend.hamlib-model", "2", "Model of hamlib device", DEFCONFIG_UINT, NULL },
   { "backend.poll-interval", "250", "How often to poll the rig in ms", DEFCONFIG_UINT, NULL },
   { "backend.hamlib-port", "127.0.0.1:4532", "What hamlib device to use (def: rigctld localhost)" },
   { "backend.reconnect-interval", "30", "Seconds to wait before retrying hamlib after disconnect; 0 = exit on disconnect (for supervisor/cron restart)", DEFCONFIG_UINT, NULL },
   { "rig.vfos", "2", "How many VFOs does the rig expose? (A-Z; 2 means only A and B exist)", DEFCONFIG_UINT, NULL },
   { "station.name", "rustyrig", "Site name used by authoritative #<station>-rig0 room" },
   { "site:coordinates", NULL, "Station coordinates as latitude,longitude (optional)" },
   { "site:gridsquare", NULL, "Station Maidenhead grid square (optional)" },
   { "chat.log", "true", "Should we log the chat to text files by date/rig?", DEFCONFIG_BOOL, NULL },
   { "chat.replay-lines", "20", "Lines of replay to show on joining chat", DEFCONFIG_UINT, NULL },
   { "callsign-lookup:cache-db", "./db/rrserver-callsigns.db", "Server-local callsign lookup cache database", DEFCONFIG_PATH, NULL },
   { "callsign-lookup:use-cache", "true", "Cache callsign lookup results on the server", DEFCONFIG_BOOL, NULL },
   { "callsign-lookup:path", "./bin/callsign-lookup", "Callsign lookup helper executable", DEFCONFIG_PATH, NULL },
   { "codecs.allowed", FWDSP_DEFAULT_CODECS, "Preferred codec order" },
   { "codecs.allowed.video", "jpeg h264", "Preferred video codec order" },
   { "webcam.enable", "false", "Capture a v4l2 webcam and stream it as a video media channel", DEFCONFIG_BOOL, NULL },
   { "webcam.device", "/dev/video0", "v4l2 device to grab frames from" },
   { "webcam.codec", "jpeg", "4-char codec magic for the video stream", DEFCONFIG_ENUM, "jpeg|h264" },
   { "core.daemonize", "false", "Should we go to background after starting?", DEFCONFIG_BOOL, NULL },
   { "core.tick-interval", "100", "How often to do timer tick?", DEFCONFIG_UINT, NULL },
   { "debug.http", "false", "Show more HTTP debugging", DEFCONFIG_BOOL, NULL },
   { "debug.http.crazy", "false", "Show extreme http debugging", DEFCONFIG_BOOL, NULL },
   { "debug.noisy-eeprom", "false", "Extra debugging msgs from eeprom code?", DEFCONFIG_BOOL, NULL },
   { "debug.mongoose", "false", "Debug mongoose?", DEFCONFIG_BOOL, NULL },
   { "debug.show-ts", "true", "Show timestamps in log? [bool]", DEFCONFIG_BOOL, NULL },
   { "device.serial", NULL, "Device serial # (usually from eeprom)" },
   { "features.auto-block-ptt", "false", "Block PTT at start?", DEFCONFIG_BOOL, NULL },
   { "fwdsp:hangtime", "30", "How long should unused (en|de)coders be kept alive after last used?", DEFCONFIG_UINT, NULL },
   { "fwdsp:path", "./bin/fwdsp", "Path to fwdsp binary", DEFCONFIG_PATH, NULL },
   { "fwdsp:subproc.max", "16", "Maximum server fwdsp processes", DEFCONFIG_UINT, NULL },
   { "fwdsp:pcm-hub", "true", "Route decoded talker PCM through the rig audio hub", DEFCONFIG_BOOL, NULL },
   { "fwdsp:rig0.rx-source", "src.rig0", "fwdsp source endpoint for rig 0 RX PCM" },
   { "log.file", "rrserver.log", "Where to log?" },
   { "log.level", "*:info", "What to log?" },
   { "net.http.404-path", "./www/404.shtml", "Path to 404 file", DEFCONFIG_PATH, NULL },
   { "net.http.enabled", "true", "Enable http?", DEFCONFIG_BOOL, NULL },
   { "net.http.bind", "127.0.0.1", "Address to listen for HTTP" },
   { "net.http.port", "8420", "Port to listen for http on", DEFCONFIG_UINT, NULL },
   { "net.http.authdb", "./config/http.users", "Path to user database", DEFCONFIG_PATH, NULL },
   { "net.http.authdb-dynamic", "true", "Load users from sqlite db instead of authdb file", DEFCONFIG_BOOL, NULL },
   { "net.http.hex-dump", "false", "Hex dump http? (Noisy!)", DEFCONFIG_BOOL, NULL },
   { "net.http.port", "8420", "HTTP listner port", DEFCONFIG_UINT, NULL },
   { "net.http.tls-bind", "127.0.0.1", "Address to listen for HTTPS (TLS)" },
   { "net.http.tls-enabled", "false", "Enable HTTPS (TLS) listener?", DEFCONFIG_BOOL, NULL },
   { "net.http.tls-port", "8443", "Port for TLS listener", DEFCONFIG_UINT, NULL },
   { "net.http.www-root", "./www", "Path to static http content", DEFCONFIG_PATH, NULL },
   // MQTT client
   { "net.mqtt-client.secret-file", "./config/mqtt-client.secrets", "Where is the mqtt client credentials?", DEFCONFIG_PATH, NULL },
   // XXX: Merge this into mqtt://user:password@host:port format, ex: mqtt://rig1:test@10.10.10.10:18383
   { "net.mqtt-client.host", NULL, "To be removed" },
   { "net.mqtt-client.port", NULL, "To be removed" },
   { "net.mqtt-client.user", NULL, "To be removed" },
   // MQTT server
   { "net.mqtt.bind", "127.0.0.1", "Address to listen for mqtt" },
   { "net.mqtt.enabled", "false", "Enable MQTT service listener?", DEFCONFIG_BOOL, NULL },
   { "net.mqtt.port", "48383", "Port for MQTT to listen", DEFCONFIG_UINT, NULL },
   { "net.mqtt.required", "false", "Exit if MQTT listener fails to start?", DEFCONFIG_BOOL, NULL },
   { "net.http.required", "false", "Exit if HTTP/HTTPS listener fails to start?", DEFCONFIG_BOOL, NULL },
   //
   { "net.mtu", NULL, "MTU for network (non-posix hosts)", DEFCONFIG_UINT, NULL },
   { "net.vlan", "4420", "VLAN to use for ethernet interface, 0 for untagged", DEFCONFIG_UINT, NULL },
   { "noob.cool-down", "30", "How long to block noob PTT after elmer overrides it (seconds)", DEFCONFIG_UINT, NULL },
   { "path.db.master", "./db/master.db", "Master database path", DEFCONFIG_PATH, NULL },
   { "path.db.master.template", "./sql/sqlite.master.sql", "Path to sql file to initialize database", DEFCONFIG_PATH, NULL },
   { "path.db.master.preload", "./sql/sqlite.master.preload.sql", "Path to SQL preload data for new databases", DEFCONFIG_PATH, NULL },
   { "path.pid-file", "./rrserver.pid", "Where to store pid file", DEFCONFIG_PATH, NULL },
   { "path.modules", "./modules/", "Where to find modules", DEFCONFIG_PATH, NULL },
   { "path.record-dir", "./recordings", "TX & RX recordings basedir", DEFCONFIG_PATH, NULL },
   { "recording.codec", "ogg", "Recording container/codec: flac or ogg", DEFCONFIG_ENUM, "ogg|flac" },
   { "recording.codec.modem", "flac", "Recording codec for modem recordings: flac or ogg", DEFCONFIG_ENUM, "ogg|flac" },
   { "record.rx", "false", "Record received audio", DEFCONFIG_BOOL, NULL },
   { "record.tx", "false", "Record transmitted audio", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_a", "false", "Record VFO A RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_b", "false", "Record VFO B RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_c", "false", "Record VFO C RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_d", "false", "Record VFO D RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_e", "false", "Record VFO E RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_f", "false", "Record VFO F RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_g", "false", "Record VFO G RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_h", "false", "Record VFO H RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_i", "false", "Record VFO I RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_j", "false", "Record VFO J RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_k", "false", "Record VFO K RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_l", "false", "Record VFO L RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_m", "false", "Record VFO M RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_n", "false", "Record VFO N RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_o", "false", "Record VFO O RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_p", "false", "Record VFO P RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_q", "false", "Record VFO Q RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_r", "false", "Record VFO R RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_s", "false", "Record VFO S RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_t", "false", "Record VFO T RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_u", "false", "Record VFO U RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_v", "false", "Record VFO V RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_w", "false", "Record VFO W RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_x", "false", "Record VFO X RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_y", "false", "Record VFO Y RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.always.vfo_z", "false", "Record VFO Z RX audio with no connected client", DEFCONFIG_BOOL, NULL },
   { "record.buffer-size", "524288", "Raw audio recording ring size in bytes", DEFCONFIG_UINT, NULL },
   { "record.max", "16", "Maximum concurrent audio recordings", DEFCONFIG_UINT, NULL },
   { "quota.enforce", "true", "Require TX credits (tx_credits table) for users to TX?", DEFCONFIG_BOOL, NULL },
   { "quota.warning", "30", "Send a one-time low-credits notice at X min of remaining TX credits", DEFCONFIG_UINT, NULL },
   { "rig.tot", "300", "Time-out timer: max TX time in seconds before PTT is halted", DEFCONFIG_UINT, NULL },
   { "rig.warmup-required", "false", "Does rig require warmup time?", DEFCONFIG_BOOL, NULL },
   { "rig.warmup-time", "30", "Required rig warmup time", DEFCONFIG_UINT, NULL },
   { NULL, NULL, NULL }
};
