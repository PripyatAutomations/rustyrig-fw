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
   FWDSP_AUDIO_PIPELINE_DEFAULTS(FWDSP_NOISE_SOURCE)
   { "audio.debug", "false", "Debug audio? [bool]" },
   { "atu.max", "4", "Maximum number of ATUs" },
   { "backend.active", "internal", "Backend to use for rig control" },
   { "backend.announce-interval", "30", "How often to send a forced update of VFO state?" },
   { "backend.state-interval", "15", "Send cat state at most once every N seconds if unchanged?" },
   { "backend.hamlib-baud", "38400", "Rig baud rate (if serial)" },
   { "backend.hamlib-debug", "warn", "Hamlib debug level" },
   { "backend.hamlib-model", "2", "Model of hamlib device" },
   { "backend.poll-interval", "250", "How often to poll the rig in ms" },
   { "backend.hamlib-port", "127.0.0.1:4532", "What hamlib device to use (def: rigctld localhost)" },
   { "backend.reconnect-interval", "30", "Seconds to wait before retrying hamlib after disconnect; 0 = exit on disconnect (for supervisor/cron restart)" },
   { "rig.vfos", "2", "How many VFOs does the rig expose? (A-Z; 2 means only A and B exist)" },
   { "chat.log", "true", "Should we log the chat to text files by date/rig?" },
   { "chat.replay-lines", "20", "Lines of replay to show on joining chat" },
   { "codecs.allowed", FWDSP_DEFAULT_CODECS, "Preferred codec order" },
   { "codecs.allowed.video", "jpeg h264", "Preferred video codec order" },
   { "webcam.enable", "false", "Capture a v4l2 webcam and stream it as a video media channel" },
   { "webcam.device", "/dev/video0", "v4l2 device to grab frames from" },
   { "webcam.codec", "jpeg", "4-char codec magic for the video stream" },
   { "core.daemonize", "false", "Should we go to background after starting?" },
   { "core.tick-interval", "100", "How often to do timer tick?" },
   { "debug.http", "false", "Show more HTTP debugging" },
   { "debug.http.crazy", "false", "Show extreme http debugging" },
   { "debug.noisy-eeprom", "false", "Extra debugging msgs from eeprom code?" },
   { "debug.mongoose", "false", "Debug mongoose?" },
   { "debug.show-ts", "true", "Show timestamps in log? [bool]" },
   { "device.serial", NULL, "Device serial # (usually from eeprom)" },
   { "features.auto-block-ptt", "false", "Block PTT at start?" },
   { "fwdsp.hangtime", "30", "How long should unused (en|de)coders be kept alive after last used?" },
   { "fwdsp.path", "./bin/fwdsp", "Path to fwdsp binary" },
   { "fwdsp.subproc.max", "16", "Maximum server fwdsp processes" },
   { "log.file", "rrserver.log", "Where to log?" },
   { "log.level", "*:info", "What to log?" },
   { "net.http.404-path", NULL, "Path to 404 file" },
   { "net.http.enabled", "true", "Enable http?" },
   { "net.http.bind", "127.0.0.1", "Address to listen for HTTP" },
   { "net.http.port", "8420", "Port to listen for http on" },
   { "net.http.authdb", "./config/http.users", "Path to user database" },
   { "net.http.authdb-dynamic", "true", "Load users from sqlite db instead of authdb file" },
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
   { "net.mqtt.required", "false", "Exit if MQTT listener fails to start?" },
   { "net.http.required", "false", "Exit if HTTP/HTTPS listener fails to start?" },
   //
   { "net.mtu", NULL, "MTU for network (non-posix hosts)" },
   { "net.vlan", "4420", "VLAN to use for ethernet interface, 0 for untagged" },
   { "noob.cool-down", "30", "How long to block noob PTT after elmer overrides it (seconds)" },
   { "path.db.master", "./db/master.db", "Master database path" },
   { "path.db.master.template", "./sql/sqlite.master.sql", "Path to sql file to initialize database" },
   { "path.pid-file", "./rrserver.pid", "Where to store pid file" },
   { "path.modules", "./modules/", "Where to find modules" },
   { "path.record-dir", "./recordings", "TX & RX recordings basedir" },
   { "record.rx", "false", "Record received audio" },
   { "record.tx", "false", "Record transmitted audio" },
   { "record.always.vfo_a", "false", "Record VFO A RX audio with no connected client" },
   { "record.always.vfo_b", "false", "Record VFO B RX audio with no connected client" },
   { "record.always.vfo_c", "false", "Record VFO C RX audio with no connected client" },
   { "record.always.vfo_d", "false", "Record VFO D RX audio with no connected client" },
   { "record.always.vfo_e", "false", "Record VFO E RX audio with no connected client" },
   { "record.always.vfo_f", "false", "Record VFO F RX audio with no connected client" },
   { "record.always.vfo_g", "false", "Record VFO G RX audio with no connected client" },
   { "record.always.vfo_h", "false", "Record VFO H RX audio with no connected client" },
   { "record.always.vfo_i", "false", "Record VFO I RX audio with no connected client" },
   { "record.always.vfo_j", "false", "Record VFO J RX audio with no connected client" },
   { "record.always.vfo_k", "false", "Record VFO K RX audio with no connected client" },
   { "record.always.vfo_l", "false", "Record VFO L RX audio with no connected client" },
   { "record.always.vfo_m", "false", "Record VFO M RX audio with no connected client" },
   { "record.always.vfo_n", "false", "Record VFO N RX audio with no connected client" },
   { "record.always.vfo_o", "false", "Record VFO O RX audio with no connected client" },
   { "record.always.vfo_p", "false", "Record VFO P RX audio with no connected client" },
   { "record.always.vfo_q", "false", "Record VFO Q RX audio with no connected client" },
   { "record.always.vfo_r", "false", "Record VFO R RX audio with no connected client" },
   { "record.always.vfo_s", "false", "Record VFO S RX audio with no connected client" },
   { "record.always.vfo_t", "false", "Record VFO T RX audio with no connected client" },
   { "record.always.vfo_u", "false", "Record VFO U RX audio with no connected client" },
   { "record.always.vfo_v", "false", "Record VFO V RX audio with no connected client" },
   { "record.always.vfo_w", "false", "Record VFO W RX audio with no connected client" },
   { "record.always.vfo_x", "false", "Record VFO X RX audio with no connected client" },
   { "record.always.vfo_y", "false", "Record VFO Y RX audio with no connected client" },
   { "record.always.vfo_z", "false", "Record VFO Z RX audio with no connected client" },
   { "record.buffer-size", "524288", "Raw audio recording ring size in bytes" },
   { "record.max", "16", "Maximum concurrent audio recordings" },
   { "quota.enforce", "true", "Require TX credits (tx_credits table) for users to TX?" },
   { "quota.warning", "30", "Send a one-time low-credits notice at X min of remaining TX credits" },
   { "rig.tot", "300", "Time-out timer: max TX time in seconds before PTT is halted" },
   { "rig.warmup-required", "false", "Does rig require warmup time?" },
   { "rig.warmup-time", "30", "Required rig warmup time" },
   { NULL, NULL, NULL }
};
