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
   { "station.description", "A misconfigured server", "Public server description for links, WHOIS and Explorer" },
   { "station.location", "Unknown City", "Public city, state, country; use MOBILE for mobile rigs" },
   { "hardware.si5351", "", "Named Si5351A devices to register; restart required" },
   { "si5351:example.bus", "", "Linux I2C adapter path (required); no automatic bus selection" },
   { "si5351:example.address", "0x60", "Seven-bit I2C address" },
   { "si5351:example.crystal-hz", "25000000", "Crystal frequency: 25000000 or 27000000 Hz" },
   { "si5351:example.correction-ppb", "0", "Crystal calibration in ppb; positive means fast (-100000..100000)" },
   { "si5351:example.load-pf", "10", "Crystal load capacitance: 6, 8 or 10 pF" },
   { "si5351:example.drive-ma", "2", "Clock output drive: 2, 4, 6 or 8 mA" },
   { "hardware.gpio-lines", "", "Named GPIO mappings to load (space or comma separated); restart required" },
   { "gpio:example.chip", "", "GPIO chip device path; mappings use [gpio:name]" },
   { "gpio:example.offset", "", "GPIO line offset within the chip, not header or WiringPi pin number" },
   { "gpio:example.direction", "input", "GPIO direction", DEFCONFIG_ENUM, "input|output" },
   { "gpio:example.active-low", "false", "Invert logical GPIO values", DEFCONFIG_BOOL, NULL },
   { "gpio:example.safe-value", "0", "Logical output value on claim and release", DEFCONFIG_ENUM, "0|1" },
   { "files.max-size", "1073741824", "Maximum uploaded file size in bytes", DEFCONFIG_UINT, NULL },
   {
      "websdr.max-temporary", "8", "Maximum concurrent temporary WebSDR rigs"
   },
   {
      "websdr.idle-timeout", "60", "Unused temporary rig lifetime in seconds (1-3600)"
   },
   {
      "bridge.host", "", "Default receiver hostname; named bridge settings override"
   },
   {
      "bridge.port", "8073", "Default receiver service port"
   },
   {
      "bridge.password", "", "Optional receiver password"
   },
   {
      "bridge.frequency", "14074000", "Initial bridge receiver frequency in Hz"
   },
   {
      "bridge.mode", "USB", "Initial bridge receiver mode"
   },
   {
      "bridge.rx-min-hz", "", "Optional offline capability hint; named bridge overrides"
   },
   {
      "bridge.rx-max-hz", "", "Optional offline capability hint; named bridge overrides"
   },
   {
      "bridge.rx-modes", "", "Optional offline capability hint; named bridge overrides"
   },
   {
      "bridge.rx-audio", "", "Optional offline capability hint; named bridge overrides"
   },
   {
      "bridge.rx-iq", "", "Optional offline capability hint; named bridge overrides"
   },
   {
      "bridge.rx-waterfall", "", "Optional offline capability hint; named bridge overrides"
   },
   {
      "bridge.tx-supported", "", "Optional offline capability hint; named bridge overrides"
   },
   {
      "bridge.tx-min-hz", "", "Optional offline capability hint; named bridge overrides"
   },
   {
      "bridge.tx-max-hz", "", "Optional offline capability hint; named bridge overrides"
   },
   {
      "bridge.tx-modes", "", "Optional offline capability hint; named bridge overrides"
   },
   {
      "bridge.tx-audio", "", "Optional offline capability hint; named bridge overrides"
   },
   {
      "bridge.instances", "", "Bridge aliases, separated by spaces or commas"
   },
   {
      "bridge.path", "auto", "Bridge executable path, or auto for PATH then ./bin/rrbridge"
   },
   {
      "bridge.modules", "auto", "Bridge driver directory, or auto for installed modules then ./bin"
   },
   {
      "listener:http.acl", "", "Named HTTP listener ACL; empty denies all"
   },
   {
      "listener:https.acl", "", "Named HTTPS listener ACL; empty denies all"
   },
   {
      "listener:link.acl", "", "Named dedicated server-link listener ACL; empty denies all"
   },
   {
      "listener:mqtt.acl", "", "Named MQTT listener ACL; empty denies all"
   },
   {
      "listener:media.acl", "", "Named direct RTP listener ACL; empty denies all"
   },
   {
      "peer.acl", "", "Named peer ACL; empty denies all"
   },

   {
      "rtp.enabled", "true", "Enable the default RTP pool", DEFCONFIG_BOOL, NULL
   },
   {
      "rtp.bind", "", "RTP bind address; empty inherits media.direct.bind"
   },
   {
      "rtp.host", "", "Advertised RTP host; empty inherits media.direct.host"
   },
   {
      "rtp.ports", "", "Inclusive UDP range or one port; empty inherits media.direct.port"
   },
   {
      "rtp.acl", "", "Named RTP ACL; empty inherits listener:media ACL"
   },
   {
      "rtp.encrypt", "", "RTP encryption; empty inherits media.direct.encrypt"
   },
   {
      "media.direct.enabled", "true", "Enable authenticated direct RTP audio", DEFCONFIG_BOOL, NULL
   },
   {
      "media.direct.bind", "0.0.0.0", "Direct RTP listener address"
   },
   {
      "media.direct.port", "8422", "Direct RTP UDP port", DEFCONFIG_INT, NULL
   },
   {
      "media.direct.host", "", "Client-reachable hostname/address; empty disables advertisement"
   },
   {
      "media.direct.encrypt", "false", "Encrypt RTP (requires WITH_RTP_ENCRYPTION=true)", DEFCONFIG_BOOL, NULL
   },
   {
      "links.shared-listener", "true", "Accept /link on existing client HTTP/HTTPS listeners"
   },
   {
      "links.listen", "", "Dedicated server-link listener URL (https://host:port; /link endpoint)"
   },
   {
      "links.tls-cert", "", "PEM certificate for server-link listener"
   },
   {
      "links.tls-key", "", "PEM private key for server-link listener"
   },
   {
      "links.tls-ca", "/etc/ssl/certs/ca-certificates.crt", "CA bundle for outbound WSS links"
   },
   {
      "links.allow-insecure", "false", "Permit plaintext WS links for explicit local testing"
   },
   {
      "links.max-depth", "5", "Default maximum descendant depth per peer (0=unlimited; leaf-only overrides)", DEFCONFIG_UINT, NULL
   },
   {
      "links.max-servers", "128", "Maximum servers in linked tree (1-256)"
   },
   {
      "links.retry-seconds", "60", "Delay before retrying peer autoconnect"
   },
   {
      "links.handshake-seconds", "10", "Maximum pending link handshake time"
   },
   {
      "peer.enabled", "false", "Enable this peer; named peer sections have the same keys"
   },
   {
      "peer.retry-seconds", "", "Peer reconnect interval in seconds; empty inherits links.retry-seconds (60)"
   },
   {
      "peer.autoconnect", "false", "Connect automatically when URL is provided and peer is not linked"
   },
   {
      "peer.server.url", "", "Peer ws/wss URL; empty means incoming-only"
   },
   {
      "peer.password", "", "Shared link password; no user account required"
   },
   {
      "peer.site.name", "", "Expected remote station name"
   },
   {
      "peer.node.uuid", "", "Optional expected remote server UUID"
   },
   FWDSP_VIDEO_PIPELINE_DEFAULTS
   FWDSP_AUDIO_PIPELINE_DEFAULTS(FWDSP_RIG_PCM_SOURCE) {
      "atu.max", "0", "Maximum number of ATUs", DEFCONFIG_UINT, NULL
   },
   {
      "backend.announce-interval", "30", "How often to send a forced update of VFO state?", DEFCONFIG_UINT, NULL
   },
   {
      "backend.poll-interval", "250", "How often to poll the rig in ms", DEFCONFIG_UINT, NULL
   },
   {
      "backend.hamlib-timeout-ms", "250", "Hamlib transaction timeout (1-5000 ms); per-rig hamlib.timeout-ms overrides", DEFCONFIG_UINT, NULL
   },
   {
      "rig.instances", "rig0", "Configured rig aliases (space or comma separated)"
   },
   {
      "rig.default", "rig0", "Default rig alias for CAT/media clients; required with multiple rigs"
   },
   {
      "rig:rig0.backend", "internal", "Backend for the default rig", DEFCONFIG_ENUM, "internal|hamlib"
   },
   {
      "rig:rig0.room", NULL, "Server-owned TX room; must be #<station.name>-<rig alias>"
   },
   {
      "rig:rig0.rx-independent-vfos", NULL, "Explicit LO-independent RX VFOs (A B etc); overrides rx-independent-tuning"
   },
   {
      "rig:rig0.rx-independent-tuning", "false",
      "Allow RX subroom tuning only when VFO tuning does not move the shared LO"
   },
   {
      "rig:rig0.audio.per-vfo", "false", "Separate RX/TX media channels per supported VFO; false shares one pair per rig", DEFCONFIG_BOOL, NULL
   },
   {
      "rig:rig0.audio.rx.per-vfo", NULL, "Override audio.per-vfo for RX: true exposes independent receiver channels", DEFCONFIG_BOOL, NULL
   },
   {
      "rig:rig0.audio.tx.per-vfo", NULL, "Override audio.per-vfo for TX: true exposes independent transmit channels", DEFCONFIG_BOOL, NULL
   },
   {
      "rig:rig0.audio.source", NULL, "Rig PCM source pipeline; defaults to src.<rig alias>"
   },
   {
      "rig:rig0.audio.sink", NULL, "Rig PCM sink pipeline; defaults to sink.<rig alias>"
   },
   {
      "rig:rig0.name", "rig0", "Display name for the default rig"
   },
   {
      "rig:rig0.vfos", "A B", "VFO aliases for the default rig (space or comma separated)"
   },
   {
      "rig:rig0.state-interval", "15", "Seconds between unchanged CAT state updates for the default rig",
      DEFCONFIG_UINT, NULL
   },
   {
      "rig:rig0.hamlib.model", "2", "Hamlib model for the default rig", DEFCONFIG_UINT, NULL
   },
   {
      "rig:rig0.hamlib.baud", "38400", "Serial baud rate for the default rig", DEFCONFIG_UINT, NULL
   },
   {
      "rig:rig0.hamlib.device", "127.0.0.1:4532", "Hamlib device or rigctld endpoint for the default rig"
   },
   {
      "rig:rig0.reconnect-interval", "30", "Seconds before reconnecting the default rig; 0 exits on disconnect",
      DEFCONFIG_UINT, NULL
   },
   {
      "rig.identity-namespace", NULL, "Stable node/config namespace for persistent rig UUIDs; defaults to station.name"
   },
   {
      "station.name", "rustyrig", "Site name used by #<station> lobby and default rig room names"
   },
   {
      "audio.test-mode", "true", "Advertise tone and pink-noise test codec variants", DEFCONFIG_BOOL, NULL
   },
   {
      "site:coordinates", NULL, "Station coordinates as latitude,longitude (optional)"
   },
   {
      "site:gridsquare", NULL, "Station Maidenhead grid square (optional)"
   },
   {
      "chat.log", "true", "Should we log the chat to text files by date/rig?", DEFCONFIG_BOOL, NULL
   },
   {
      "chat.replay-lines", "20", "Lines of replay to show on joining chat", DEFCONFIG_UINT, NULL
   },
   {
      "callsign-lookup:cache-db", "./db/rrserver-callsigns.db", "Server-local callsign lookup cache database",
      DEFCONFIG_PATH, NULL
   },
   {
      "callsign-lookup:use-cache", "true", "Cache callsign lookup results on the server", DEFCONFIG_BOOL, NULL
   },
   {
      "callsign-lookup:path", "./bin/callsign-lookup", "Callsign lookup helper executable", DEFCONFIG_PATH, NULL
   },
   {
      "codecs.allowed", FWDSP_DEFAULT_CODECS, "Preferred codec order"
   },
   {
      "codecs.allowed.video", "h264 h265 jpeg", "Preferred video codec order"
   },
   {
      "webcam.enable", "false", "Offer configured video capture to subscribed webcam viewers", DEFCONFIG_BOOL, NULL
   },
   {
      "webcam.device", "/dev/video0", "Legacy camera hint; configure the device in its capture pipeline"
   },
   {
      "webcam.codec", "h264", "Four-character capture pipeline and video codec tag"
   },
   {
      "core.daemonize", "false", "Should we go to background after starting?", DEFCONFIG_BOOL, NULL
   },
   {
      "core.tick-interval", "100", "How often to do timer tick?", DEFCONFIG_UINT, NULL
   },
   {
      "debug.noisy-eeprom", "false", "Extra debugging msgs from eeprom code?", DEFCONFIG_BOOL, NULL
   },
   {
      "rig:rig0.gps.position", NULL, "Manual rig position: latitude,longitude in decimal degrees"
   },
   {
      "station.gps.position", NULL, "Manual station position inherited by rigs without their own GPS"
   },
   {
      "gps.output", "position", "Default GPS serial output: position summary or complete receiver nmea"
   },
   {
      "serial:ttyGPS0.gps-output", NULL, "Override GPS serial output mode for this endpoint"
   },
   {
      "gpsd.target", "station", "GPS source supplied by the gpsd module: station or rig alias"
   },
   {
      "gpsd.url", "tcp://127.0.0.1:2947", "GPS daemon WATCH endpoint"
   },
   {
      "gpsd.device", NULL, "Optional gpsd receiver device selector"
   },
   {
      "serial.enable", "true", "Enable configured WebSocket serial exports"
   },
   {
      "modem.path", "/var/lib/rustyrig/modems", "Directory for external modem executables", DEFCONFIG_PATH, NULL
   },
   {
      "serial.ttyMODEM0", NULL, "Optional modem export; e.g. modem:/var/lib/rustyrig/modems/rrmodem-ft8"
   },
   {
      "serial.ttyHOST0", NULL, "Optional exported real serial device path"
   },
   {
      "serial:ttyHOST0.buffer-bytes", "16384", "Bounded serial RX buffer; 0 keeps one transfer block"
   },
   {
      "serial:ttyHOST0.baud", "9600", "Initial exported serial baud rate"
   },
   {
      "serial:ttyHOST0.mode", "8n1", "Exported serial data/parity/stop mode"
   },
   {
      "device.serial", NULL, "Device serial # (usually from eeprom)"
   },
   {
      "features.auto-block-ptt", "false", "Block PTT at start?", DEFCONFIG_BOOL, NULL
   },
   {
      "fwdsp:hangtime", "30", "How long should unused (en|de)coders be kept alive after last used?", DEFCONFIG_UINT,
      NULL
   },
   {
      "fwdsp:path", "./bin/fwdsp", "Path to fwdsp binary", DEFCONFIG_PATH, NULL
   },
   {
      "fwdsp:subproc.max", "16", "Maximum server fwdsp processes", DEFCONFIG_UINT, NULL
   },
   {
      "fwdsp:pcm-hub", "true", "Route decoded talker PCM through the rig audio hub", DEFCONFIG_BOOL, NULL
   },
   {
      "log.file", "rrserver.log", "Where to log?"
   },
   {
      "log.level", "*:info", "What to log?"
   },
   {
      "log.show-ts", "true", "Show timestamps in log?", DEFCONFIG_BOOL, NULL
   },
   {
      "net.http.404-path", "/var/lib/rustyrig/www/404.shtml", "Path to 404 file", DEFCONFIG_PATH, NULL
   },
   {
      "net.http.enabled", "true", "Enable http?", DEFCONFIG_BOOL, NULL
   },
   {
      "net.http.bind", "127.0.0.1", "Address to listen for HTTP"
   },
   {
      "net.http.port", "8420", "Port to listen for http on", DEFCONFIG_UINT, NULL
   },
   {
      "net.http.authdb", "/etc/rustyrig/http.users", "Path to user database", DEFCONFIG_PATH, NULL
   },
   {
      "net.http.authdb-dynamic", "true", "Load users from sqlite db instead of authdb file", DEFCONFIG_BOOL, NULL
   },
   {
      "net.http.hex-dump", "false", "Hex dump http? (Noisy!)", DEFCONFIG_BOOL, NULL
   },
   {
      "net.http.tls-bind", "127.0.0.1", "Address to listen for HTTPS (TLS)"
   },
   {
      "net.http.tls-enabled", "false", "Enable HTTPS (TLS) listener?", DEFCONFIG_BOOL, NULL
   },
   {
      "net.http.tls-port", "8443", "Port for TLS listener", DEFCONFIG_UINT, NULL
   },
   {
      "net.http.www-root", "./www", "Path to static http content", DEFCONFIG_PATH, NULL
   },
   // MQTT client
   {
      "net.mqtt-client.enabled", "false", "Enable the outbound MQTT client", DEFCONFIG_BOOL, NULL
   },
   {
      "net.mqtt-client.secret-file", "./config/mqtt-client.secrets", "Where are the MQTT client credentials?",
      DEFCONFIG_PATH, NULL
   },
   {
      "net.mqtt-client.host", NULL, "Outbound MQTT broker host"
   },
   {
      "net.mqtt-client.port", "1883", "Outbound MQTT broker port", DEFCONFIG_UINT, NULL
   },
   {
      "net.mqtt-client.user", NULL, "Outbound MQTT username"
   },
   // MQTT server
   {
      "net.mqtt.bind", "127.0.0.1", "Address to listen for mqtt"
   },
   {
      "net.mqtt.enabled", "false", "Enable MQTT service listener?", DEFCONFIG_BOOL, NULL
   },
   {
      "net.mqtt.port", "48383", "Port for MQTT to listen", DEFCONFIG_UINT, NULL
   },
   {
      "net.mqtt.required", "false", "Exit if MQTT listener fails to start?", DEFCONFIG_BOOL, NULL
   },
   {
      "net.http.required", "false", "Exit if HTTP/HTTPS listener fails to start?", DEFCONFIG_BOOL, NULL
   },
   //
   {
      "net.mtu", NULL, "MTU for network (non-posix hosts)", DEFCONFIG_UINT, NULL
   },
   {
      "net.vlan", "4420", "VLAN to use for ethernet interface, 0 for untagged", DEFCONFIG_UINT, NULL
   },
   {
      "noob.supervisor-idle-seconds", "300", "Seconds without deliberate operator activity before supervision expires", DEFCONFIG_UINT, NULL
   }, {
      "noob.cool-down", "30", "How long to block noob PTT after elmer overrides it (seconds)", DEFCONFIG_UINT, NULL
   },
   {
      "path.db.master", "./db/master.db", "Master database path", DEFCONFIG_PATH, NULL
   },
   {
      "path.db.master.template", "./sql/sqlite.master.sql", "Path to sql file to initialize database", DEFCONFIG_PATH,
      NULL
   },
   {
      "path.db.master.preload", "./sql/sqlite.master.preload.sql", "Path to SQL preload data for new databases",
      DEFCONFIG_PATH, NULL
   },
   {
      "path.pid-file", "./rrserver.pid", "Where to store pid file", DEFCONFIG_PATH, NULL
   },
   {
      "path.modules", "/usr/lib/rustyrig/modules/rrserver", "Where to find modules", DEFCONFIG_PATH, NULL
   },
   {
      "path.record-dir", "./recordings", "TX & RX recordings basedir", DEFCONFIG_PATH, NULL
   },
   {
      "recording.codec", "ogg", "Recording container/codec: flac or ogg", DEFCONFIG_ENUM, "ogg|flac"
   },
   {
      "recording.codec.modem", "flac", "Recording codec for modem recordings: flac or ogg", DEFCONFIG_ENUM, "ogg|flac"
   },
   {
      "security.max-pw-age", "90", "Report passwords older than this many days with /user oldpw (0 disables)",
      DEFCONFIG_UINT, NULL
   },
   {
      "record.rx", "false", "Record received audio", DEFCONFIG_BOOL, NULL
   },
   {
      "record.tx", "true", "Record transmitted audio", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_a", "false", "Record VFO A RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_b", "false", "Record VFO B RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_c", "false", "Record VFO C RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_d", "false", "Record VFO D RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_e", "false", "Record VFO E RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_f", "false", "Record VFO F RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_g", "false", "Record VFO G RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_h", "false", "Record VFO H RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_i", "false", "Record VFO I RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_j", "false", "Record VFO J RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_k", "false", "Record VFO K RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_l", "false", "Record VFO L RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_m", "false", "Record VFO M RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_n", "false", "Record VFO N RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_o", "false", "Record VFO O RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_p", "false", "Record VFO P RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_q", "false", "Record VFO Q RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_r", "false", "Record VFO R RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_s", "false", "Record VFO S RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_t", "false", "Record VFO T RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_u", "false", "Record VFO U RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_v", "false", "Record VFO V RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_w", "false", "Record VFO W RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_x", "false", "Record VFO X RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_y", "false", "Record VFO Y RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.always.vfo_z", "false", "Record VFO Z RX audio with no connected client", DEFCONFIG_BOOL, NULL
   },
   {
      "record.buffer-size", "524288", "Raw audio recording ring size in bytes", DEFCONFIG_UINT, NULL
   },
   {
      "record.max", "16", "Maximum concurrent audio recordings", DEFCONFIG_UINT, NULL
   },
   {
      "accounting.strict", "false", "Lock PTT until restart after quota, usage or audit database write failure", DEFCONFIG_BOOL, NULL
   },
   {
      "quota.enforce", "true", "Require TX credits (tx_credits table) for users to TX?", DEFCONFIG_BOOL, NULL
   },
   {
      "quota.bandwidth.default", "1G", "Bandwidth allowance restored by /quota BW RESET (decimal M/G/T/P units)", DEFCONFIG_STRING, NULL
   },
   {
      "quota.warning", "30", "Send a one-time low-credits notice at X min of remaining TX credits", DEFCONFIG_UINT, NULL
   },
   {
      "rig.tot", "300", "Time-out timer: max TX time in seconds before PTT is halted", DEFCONFIG_UINT, NULL
   },
   {
      "rig.warmup-required", "false", "Does rig require warmup time?", DEFCONFIG_BOOL, NULL
   },
   {
      "rig.warmup-time", "30", "Required rig warmup time", DEFCONFIG_UINT, NULL
   },
   {
      NULL, NULL, NULL
   }
};
