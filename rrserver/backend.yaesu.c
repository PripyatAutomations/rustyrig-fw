//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Native Yaesu CAT backend. Part of RustyRig; MIT/GPL per build profile.
// The serial transaction API is deliberately small so it can be exercised
// against rrclient's CAT emulator as well as a physical FT-891.
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <librustyaxe/core.h>
#include <rrserver/backend.h>
#include <rrserver/backend.worker.h>
#include <rrserver/rig.properties.h>

typedef struct yaesu_menu_entry {
   const char *id, *label;
} yaesu_menu_entry_t;
typedef struct yaesu_model_profile {
   int cat_id;
   const char *model;
   const yaesu_menu_entry_t *menu;
   size_t menu_count;
   bool ft891_controls; /* Until other models' control mappings are verified. */
} yaesu_model_profile_t;

typedef struct yaesu_state {
   int fd, baud;
   char *device;
   long frequency[2];
   rr_mode_t mode[2];
   bool ptt;
   int dimmer[4], af;
   bool dimmer_known, af_known;
   int model_id;
   const struct yaesu_model_profile *profile;
} yaesu_state_t;

static yaesu_state_t *ys(rr_backend_t *b) {
   return rr_backend_instance_data(b);
}
static speed_t speed_for(int baud) {
   switch (baud) {
      case 4800: {
         return B4800;
      }
      case 9600: {
         return B9600;
      }
      case 19200: {
         return B19200;
      }
      case 38400: {
         return B38400;
      }
      case 57600: {
         return B57600;
      }
      case 115200: {
         return B115200;
      }
      default: {
         return 0;
      }
   }
}
static bool serial_open(yaesu_state_t *s) {
   speed_t speed = speed_for(s->baud);

   if (!speed) {
      return false;
   }
   int fd = open(s->device, O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);

   if (fd < 0) {
      return false;
   }
   struct termios tty;

   if (tcgetattr(fd, &tty) < 0) {
      close(fd);

      return false;
   }
   cfmakeraw(&tty);
   cfsetispeed(&tty, speed);
   cfsetospeed(&tty, speed);
   tty.c_cflag |= CLOCAL | CREAD;

   if (tcsetattr(fd, TCSANOW, &tty) < 0) {
      close(fd);

      return false;
   }
   tcflush(fd, TCIFLUSH);
   s->fd = fd;

   return true;
}

/* All calls run on the per-backend serialized I/O worker. */
static bool cat(yaesu_state_t *s, const char *command, const char *prefix, char *reply, size_t capacity) {
   if (s->fd < 0 && !serial_open(s)) {
      return false;
   }
   size_t remaining = strlen(command);
   const char *p = command;
   while (remaining) {
      struct pollfd fd = {
         .fd = s->fd, .events = POLLOUT
      };

      if (poll(&fd, 1, 350) <= 0) {
         goto fail;
      }
      ssize_t n = write(s->fd, p, remaining);

      if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
         continue;
      }

      if (n <= 0) {
         goto fail;
      }
      p += n;
      remaining -= n;
   }

   if (!prefix) {
      return true;
   }
   size_t used = 0;

   /* Discard unsolicited frames, accept only the requested command prefix. */
   for (int attempts = 0 ; attempts < 120 ; attempts++) {
      struct pollfd fd = {
         .fd = s->fd, .events = POLLIN
      };

      if (poll(&fd, 1, 25) <= 0) {
         continue;
      }
      char c;
      ssize_t n = read(s->fd, &c, 1);

      if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
         continue;
      }

      if (n <= 0) {
         goto fail;
      }

      if (c == ';') {
         reply[used] = 0;

         if (!strncmp(reply, prefix, strlen(prefix))) {
            return true;
         }
         used = 0;
      } else if (c >= 32 && c <= 126 && used + 1 < capacity) {
         reply[used++] = c;
      } else if (used + 1 >= capacity) {
         used = 0;
      }
   }

fail:
   close(s->fd);
   s->fd = -1;

   return false;
}
static bool decimal(const char *str, int n, int *value) {
   int x = 0;

   for (int i = 0 ; i < n ; i++) {
      if (!isdigit((unsigned char)str[i])) {
         return false;
      }
      x = x * 10 + str[i] - '0';
   }

   *value = x;

   return true;
}
static bool get_dimmer(yaesu_state_t *s) {
   char reply[64];

   if (!cat(s, "DA;", "DA", reply, sizeof(reply)) || strlen(reply) != 10) {
      return false;
   }
   int values[4];

   for (int i = 0 ; i < 4 ; i++) {
      if (!decimal(reply + 2 + i * 2, 2, &values[i])) {
         return false;
      }
   }

   if (values[0] < 1 || values[0] > 15 || values[1] < 1 || values[1] > 15 || values[2] > 15 || values[3] > 15) {
      return false;
   }
   memcpy(s->dimmer, values, sizeof(values));
   s->dimmer_known = true;

   return true;
}
static bool get_af(yaesu_state_t *s) {
   char reply[64];

   if (!cat(s, "AG0;", "AG0", reply, sizeof(reply)) || strlen(reply) != 6) {
      return false;
   }
   int value;

   if (!decimal(reply + 3, 3, &value) || value > 255) {
      return false;
   }
   s->af = value;
   s->af_known = true;

   return true;
}
/* FT-891 documented CAT controls. UI values are normalized percentages. */
typedef struct yaesu_control_spec {
   const char *name, *command;
   int minimum, maximum, digits;
} yaesu_control_spec_t;
static const yaesu_control_spec_t yaesu_controls[] = {
   { "receiver.rf-gain", "RG0", 0, 30, 3 },
   { "receiver.attenuator", "RA0", 0, 1, 1 },
   { "receiver.noise-blanker", "NB0", 0, 1, 1 },
   { "receiver.noise-blanker-level", "NL0", 0, 10, 3 },
   { "receiver.noise-reduction", "NR0", 0, 1, 1 },
   { "receiver.noise-reduction-level", "RL0", 1, 15, 2 },
   { "transmit.mic-gain", "MG", 0, 100, 3 },
   { "transmit.power", "PC", 5, 100, 3 },
   { "receiver.squelch", "SQ0", 0, 100, 3 },
   { "receiver.ipo", "PA0", 0, 1, 1 },
   { "receiver.auto-notch", "BC0", 0, 1, 1 },
   { "receiver.narrow", "NA0", 0, 1, 1 },
   { "receiver.agc", "GT0", 0, 4, 1 },
   { "cw.keyer", "KR", 0, 1, 1 },
   { "cw.speed", "KS", 4, 60, 3 },
   { "cw.pitch", "KP", 0, 75, 2 },
   { "cw.break-in", "BI", 0, 1, 1 },
   { "transmit.monitor-level", "ML", 0, 100, 3 },
   { "transmit.processor-level", "PL", 0, 100, 3 },
   { "transmit.vox", "VX", 0, 1, 1 },
   { "transmit.vox-gain", "VG", 0, 100, 3 },
   { "panel.dial-lock", "LK", 0, 1, 1 },
};
static const yaesu_control_spec_t *yaesu_control_find(const char *name) {
   for (size_t i = 0; i < sizeof(yaesu_controls) / sizeof(yaesu_controls[0]); ++i)
      if (!strcmp(name, yaesu_controls[i].name)) return &yaesu_controls[i];
   return NULL;
}
static bool yaesu_control_read(yaesu_state_t *s, const yaesu_control_spec_t *spec, int *percent) {
   char cmd[16], reply[32];
   snprintf(cmd, sizeof(cmd), "%s;", spec->command);
   if (!cat(s, cmd, spec->command, reply, sizeof(reply))) return false;
   size_t prefix = strlen(spec->command);
   if (strlen(reply) != prefix + (size_t)spec->digits) return false;
   int value;
   if (!decimal(reply + prefix, spec->digits, &value) || value < spec->minimum || value > spec->maximum) return false;
   *percent = (value - spec->minimum) * 100 / (spec->maximum - spec->minimum);
   return true;
}
static void observe(rr_backend_t *b, const char *name, int percent) {
   dict_value_t value = {
      .i = percent
   };
   rr_backend_property_observe(b, NULL, name, VAL_INT, &value);
}
static bool control(rr_backend_t *b, const char *name, int percent) {
   yaesu_state_t *s = ys(b);

   if (!s || percent < 0 || percent > 100) {
      return true;
   }
   char command[40];

   /* DA fields: P1 contrast (1..15), P2 key backlight, P3 LCD,
    * P4 TX/BUSY indicator. Read-modify-write retains untouched fields. */
   int field = !strcmp(name, "display.contrast") ? 0 :
      !strcmp(name, "display.key-backlight") ? 1 :
      !strcmp(name, RR_PROP_DISPLAY_BRIGHTNESS) ? 2 :
      !strcmp(name, "display.tx-indicator") ? 3 : -1;
   if (field >= 0) {
      if (!get_dimmer(s)) return true;
      int minimum = field <= 1 ? 1 : 0;
      s->dimmer[field] = minimum + (percent * (15 - minimum) + 50) / 100;
      snprintf(command, sizeof(command), "DA%02d%02d%02d%02d;",
         s->dimmer[0], s->dimmer[1], s->dimmer[2], s->dimmer[3]);
      if (!cat(s, command, NULL, NULL, 0) || !get_dimmer(s)) return true;
      observe(b, name, (s->dimmer[field] - minimum) * 100 / (15 - minimum));
      return false;
   }

   if (!strcmp(name, RR_PROP_SPEAKER_VOLUME)) {
      snprintf(command, sizeof(command), "AG0%03d;", (percent * 255 + 50) / 100);

      if (!cat(s, command, NULL, NULL, 0) || !get_af(s)) {
         return true;
      }
      observe(b, name, (s->af * 100 + 127) / 255);

      return false;
   }

   const yaesu_control_spec_t *spec = s->profile && s->profile->ft891_controls ? yaesu_control_find(name) : NULL;
   if (spec) {
      int value = spec->minimum + (percent * (spec->maximum - spec->minimum) + 50) / 100;
      snprintf(command, sizeof(command), "%s%0*d;", spec->command, spec->digits, value);
      if (!cat(s, command, NULL, NULL, 0)) return true;
      int actual;
      if (!yaesu_control_read(s, spec, &actual)) return true;
      observe(b, name, actual);
      return false;
   }
   return true;
}
static bool index_of(rr_server_vfo_t *vfo, rr_vfo_t *i) {
   return vfo && rr_server_vfo_native_index(vfo, i) && (*i == VFO_A || *i == VFO_B);
}
static bool supported(rr_backend_t *b, rr_server_vfo_t *v) {
   rr_vfo_t i;

   return ys(b) && index_of(v, &i);
}
static rr_mode_t mode_decode(char value) {
   switch (value) {
      case '1': {
         return MODE_LSB;
      }
      case '2': {
         return MODE_USB;
      }
      case '3':
      case '7': {
         return MODE_CW;
      }
      case '4':
      case 'B': {
         return MODE_FM;
      }
      case '5':
      case 'D': {
         return MODE_AM;
      }
      case '8': {
         return MODE_DL;
      }
      case 'C': {
         return MODE_DU;
      }
      default: {
         return MODE_NONE;
      }
   }
}
static char mode_encode(rr_mode_t mode) {
   switch (mode) {
      case MODE_LSB: {
         return '1';
      }
      case MODE_USB: {
         return '2';
      }
      case MODE_CW: {
         return '3';
      }
      case MODE_FM: {
         return '4';
      }
      case MODE_AM: {
         return '5';
      }
      case MODE_DL: {
         return '8';
      }
      case MODE_DU: {
         return 'C';
      }
      default: {
         return 0;
      }
   }
}
static bool mode_set(rr_backend_t *b, rr_server_vfo_t *v, rr_mode_t mode) {
   yaesu_state_t *s = ys(b);
   rr_vfo_t index;
   char digit = mode_encode(mode), command[8];

   /* FT-891 MD0 addresses main RX only. Do not silently set a different bank. */
   if (!s || !index_of(v, &index) || index != b->active_vfo || !digit) {
      return true;
   }
   snprintf(command, sizeof(command), "MD0%c;", digit);

   if (!cat(s, command, NULL, NULL, 0)) {
      return true;
   }
   s->mode[index] = mode;

   return false;
}
static rr_vfo_data_t *state(rr_backend_t *b, rr_server_vfo_t *v) {
   yaesu_state_t *s = ys(b);
   rr_vfo_t index;

   if (!s || !index_of(v, &index)) {
      return NULL;
   }
   char reply[64], cmd[8];
   snprintf(cmd, sizeof(cmd), "F%c;", index == VFO_A ? 'A' : 'B');

   if (!cat(s, cmd, index == VFO_A ? "FA" : "FB", reply, sizeof(reply)) || strlen(reply) != 11) {
      return NULL;
   }
   int frequency = 0;

   if (!decimal(reply + 2, 9, &frequency) || frequency <= 0) {
      return NULL;
   }
   s->frequency[index] = frequency;

   if (index == b->active_vfo) {
      char md[24];

      if (cat(s, "MD0;", "MD0", md, sizeof(md)) && strlen(md) == 4) {
         rr_mode_t parsed = mode_decode(md[3]);

         if (parsed != MODE_NONE) {
            s->mode[index] = parsed;
            dict_value_t m = {
               .s = vfo_mode_name(parsed)
            };
            rr_backend_property_observe(b, v, RR_PROP_VFO_MODE, VAL_STR, &m);
         }
      }
   }
   dict_value_t observed = {
      .l = frequency
   };
   rr_backend_property_observe(b, v, RR_PROP_VFO_FREQUENCY, VAL_LONG, &observed);

   /* Control properties are advertised during create(), not queried during
    * frequency/mode polling. A future per-property read subscription can
    * refresh only open Resource Explorer controls and summoned room widgets.
    * Successful writes already perform readback and publish observations. */
   rr_vfo_data_t *result = calloc(1, sizeof(*result));

   if (!result) {
      return NULL;
   }
   result->id = index;
   result->freq = frequency;
   result->mode = s->mode[index];

   return result;
}
static bool frequency_set(rr_backend_t *b, rr_server_vfo_t *v, int hz) {
   yaesu_state_t *s = ys(b);
   rr_vfo_t i;
   char cmd[32];

   if (!s || !index_of(v, &i) || hz <= 0 || hz > 999999999) {
      return true;
   }
   snprintf(cmd, sizeof(cmd), "F%c%09d;", i == VFO_A ? 'A' : 'B', hz);

   if (!cat(s, cmd, NULL, NULL, 0)) {
      return true;
   }
   s->frequency[i] = hz;

   return false;
}
static float frequency_get(rr_backend_t *b, rr_server_vfo_t *v) {
   yaesu_state_t *s = ys(b);
   rr_vfo_t i;

   return s && index_of(v, &i) ? s->frequency[i] : 0;
}
static bool ptt_set(rr_backend_t *b, rr_server_vfo_t *v, bool on) {
   yaesu_state_t *s = ys(b);
   rr_vfo_t i;

   if (!s || !index_of(v, &i) || i != b->active_vfo) {
      return true;
   }

   if (!cat(s, on ? "TX1;" : "TX0;", NULL, NULL, 0)) {
      return true;
   }
   s->ptt = on;

   return false;
}
static bool ptt_get(rr_backend_t *b, rr_server_vfo_t *v) {
   yaesu_state_t *s = ys(b);

   return s && s->ptt;
}
static rr_mode_t mode_get(rr_backend_t *b, rr_server_vfo_t *v) {
   yaesu_state_t *s = ys(b);
   rr_vfo_t i;

   return s && index_of(v, &i) ? s->mode[i] : MODE_NONE;
}
static const char *mode_get_str(rr_backend_t *b, rr_server_vfo_t *v) {
   return vfo_mode_name(mode_get(b, v));
}
/* Adding another Yaesu model requires one profile and its validated catalog.
 * Keep model identification separate from protocol mechanics. */
#define ARRAY_COUNT(x) (sizeof(x) / sizeof((x)[0]))
static const yaesu_menu_entry_t ft891_menu[] = {
   /* 01 AGC */
   { "0101", "AGC FAST DELAY" },
   { "0102", "AGC MID DELAY" },
   { "0103", "AGC SLOW DELAY" },
   /* 02 DISPLAY */
   { "0201", "LCD CONTRAST" },
   { "0202", "DIMMER BACKLIT" },
   { "0203", "DIMMER LCD" },
   { "0204", "DIMMER TX/BUSY" },
   { "0205", "PEAK HOLD" },
   { "0206", "ZIN LED" },
   { "0207", "POP-UP MENU" },
   /* 03 DVS */
   { "0301", "DVS RX OUT LVL" },
   { "0302", "DVS TX OUT LVL" },
   /* 04 KEYER */
   { "0401", "KEYER TYPE" },
   { "0402", "KEYER DOT/DASH" },
   { "0403", "CW WEIGHT" },
   { "0404", "BEACON INTERVAL" },
   { "0405", "NUMBER STYLE" },
   { "0406", "CONTEST NUMBER" },
   { "0407", "CW MEMORY 1" },
   { "0408", "CW MEMORY 2" },
   { "0409", "CW MEMORY 3" },
   { "0410", "CW MEMORY 4" },
   { "0411", "CW MEMORY 5" },
   /* 05 GENERAL */
   { "0501", "NB WIDTH" },
   { "0502", "NB REJECTION" },
   { "0503", "NB LEVEL" },
   { "0504", "BEEP LEVEL" },
   { "0505", "RF/SQL VR" },
   { "0506", "CAT RATE" },
   { "0507", "CAT TOT" },
   { "0508", "CAT RTS" },
   { "0509", "MEM GROUP" },
   { "0510", "FM SETTING" },
   { "0511", "REC SETTING" },
   { "0512", "ATAS SETTING" },
   { "0513", "QUICK SPL FREQ" },
   { "0514", "TX TOT" },
   { "0515", "MIC SCAN" },
   { "0516", "MIC SCAN RESUME" },
   { "0517", "REF FREQ ADJ" },
   { "0518", "CLAR SELECT" },
   { "0519", "APO" },
   { "0520", "FAN CONTROL" },
   /* 06 MODE AM */
   { "0601", "AM LCUT FREQ" },
   { "0602", "AM LCUT SLOPE" },
   { "0603", "AM HCUT FREQ" },
   { "0604", "AM HCUT SLOPE" },
   { "0605", "AM MIC SELECT" },
   { "0606", "AM OUT LEVEL" },
   { "0607", "AM PTT SELECT" },
   /* 07 MODE CW */
   { "0701", "CW LCUT FREQ" },
   { "0702", "CW LCUT SLOPE" },
   { "0703", "CW HCUT FREQ" },
   { "0704", "CW HCUT SLOPE" },
   { "0705", "CW OUT LEVEL" },
   { "0706", "CW AUTO MODE" },
   { "0707", "CW BFO" },
   { "0708", "CW BK-IN TYPE" },
   { "0709", "CW BK-IN DELAY" },
   { "0710", "CW WAVE SHAPE" },
   { "0711", "CW FREQ DISPLAY" },
   { "0712", "PC KEYING" },
   { "0713", "QSK DELAY TIME" },
   /* 08 MODE DAT */
   { "0801", "DATA MODE" },
   { "0802", "PSK TONE" },
   { "0803", "OTHER DISP" },
   { "0804", "OTHER SHIFT" },
   { "0805", "DATA LCUT FREQ" },
   { "0806", "DATA LCUT SLOPE" },
   { "0807", "DATA HCUT FREQ" },
   { "0808", "DATA HCUT SLOPE" },
   { "0809", "DATA IN SELECT" },
   { "0810", "DATA PTT SELECT" },
   { "0811", "DATA OUT LEVEL" },
   { "0812", "DATA BFO" },
   /* 09 MODE FM */
   { "0901", "FM MIC SELECT" },
   { "0902", "FM OUT LEVEL" },
   { "0903", "PKT PTT SELECT" },
   { "0904", "RPT SHIFT 28MHz" },
   { "0905", "RPT SHIFT 50MHz" },
   { "0906", "DCS POLARITY" },
   /* 10 MODE RTY */
   { "1001", "RTTY LCUT FREQ" },
   { "1002", "RTTY LCUT SLOPE" },
   { "1003", "RTTY HCUT FREQ" },
   { "1004", "RTTY HCUT SLOPE" },
   { "1005", "RTTY SHIFT PORT" },
   { "1006", "RTTY POLARITY-R" },
   { "1007", "RTTY POLARITY-T" },
   { "1008", "RTTY OUT LEVEL" },
   { "1009", "RTTY SHIFT FREQ" },
   { "1010", "RTTY MARK FREQ" },
   { "1011", "RTTY BFO" },
   /* 11 MODE SSB */
   { "1101", "SSB LCUT FREQ" },
   { "1102", "SSB LCUT SLOPE" },
   { "1103", "SSB HCUT FREQ" },
   { "1104", "SSB HCUT SLOPE" },
   { "1105", "SSB MIC SELECT" },
   { "1106", "SSB OUT LEVEL" },
   { "1107", "SSB BFO" },
   { "1108", "SSB PTT SELECT" },
   { "1109", "SSB TX BPF" },
   /* 12 RX DSP */
   { "1201", "APF WIDTH" },
   { "1202", "CONTOUR LEVEL" },
   { "1203", "CONTOUR WIDTH" },
   { "1204", "IF NOTCH WIDTH" },
   /* 13 SCOPE */
   { "1301", "SCP START CYCLE" },
   { "1302", "SCP SPAN FREQ" },
   /* 14 TUNING */
   { "1401", "QUICK DIAL" },
   { "1402", "SSB DIAL STEP" },
   { "1403", "AM DIAL STEP" },
   { "1404", "FM DIAL STEP" },
   { "1405", "DIAL STEP" },
   { "1406", "AM CH STEP" },
   { "1407", "FM CH STEP" },
   /* 15 TX AUDIO */
   { "1501", "EQ1 FREQ" },
   { "1502", "EQ1 LEVEL" },
   { "1503", "EQ1 BWTH" },
   { "1504", "EQ2 FREQ" },
   { "1505", "EQ2 LEVEL" },
   { "1506", "EQ2 BWTH" },
   { "1507", "EQ3 FREQ" },
   { "1508", "EQ3 LEVEL" },
   { "1509", "EQ3 BWTH" },
   { "1510", "P-EQ1 FREQ" },
   { "1511", "P-EQ1 LEVEL" },
   { "1512", "P-EQ1 BWTH" },
   { "1513", "P-EQ2 FREQ" },
   { "1514", "P-EQ2 LEVEL" },
   { "1515", "P-EQ2 BWTH" },
   { "1516", "P-EQ3 FREQ" },
   { "1517", "P-EQ3 LEVEL" },
   { "1518", "P-EQ3 BWTH" },
   /* 16 TX GNRL */
   { "1601", "HF SSB PWR" },
   { "1602", "HF AM PWR" },
   { "1603", "HF PWR" },
   { "1604", "50M SSB PWR" },
   { "1605", "50M AM PWR" },
   { "1606", "50M PWR" },
   { "1607", "SSB MIC GAIN" },
   { "1608", "AM MIC GAIN" },
   { "1609", "FM MIC GAIN" },
   { "1610", "DATA MIC GAIN" },
   { "1611", "SSB DATA GAIN" },
   { "1612", "AM DATA GAIN" },
   { "1613", "FM DATA GAIN" },
   { "1614", "DATA DATA GAIN" },
   { "1615", "TUNER SELECT" },
   { "1616", "VOX SELECT" },
   { "1617", "VOX GAIN" },
   { "1618", "VOX DELAY" },
   { "1619", "ANTI VOX GAIN" },
   { "1620", "DATA VOX GAIN" },
   { "1621", "DATA VOX DELAY" },
   { "1622", "ANTI DVOX GAIN" },
   { "1623", "EMERGENCY FREQ" },
   /* 17 RESET */
   { "1701", "RESET" },
   /* 18 VERSION */
   { "1801", "MAIN VERSION" },
   { "1802", "DSP VERSION" },
   { "1803", "LCD VERSION" },
};
static const yaesu_menu_entry_t ft991a_menu[] = {
   { "001", "AGC Fast Delay" }, { "002", "AGC Mid Delay" },
   { "003", "AGC Slow Delay" }, { "006", "Display Color" },
   { "007", "LED Dimmer" }, { "008", "TFT Dimmer" },
   { "009", "Meter Peak Hold" }, { "012", "Keyer Type" },
   { "023", "Noise Blanker Width" }, { "024", "Noise Blanker Rejection" },
   { "025", "Noise Blanker Level" }
};
static const yaesu_model_profile_t yaesu_models[] = {
   { 650, "FT-891", ft891_menu, ARRAY_COUNT(ft891_menu), true },
   { 670, "FT-991A", ft991a_menu, ARRAY_COUNT(ft991a_menu), false }
};
static const yaesu_model_profile_t *yaesu_model_by_id(int id) {
   for (size_t i = 0; i < ARRAY_COUNT(yaesu_models); i++)
      if (yaesu_models[i].cat_id == id) return &yaesu_models[i];
   return NULL;
}
static void yaesu_menu_foreach(rr_backend_t *b,
   void (*emit)(const char *id, const char *label, void *user), void *user) {
   yaesu_state_t *s = ys(b);
   if (!s || !s->profile || !emit) return;
   for (size_t i = 0; i < s->profile->menu_count; i++)
      emit(s->profile->menu[i].id, s->profile->menu[i].label, user);
}
/* Read exactly one advertised menu entry. EXnnnn; is the FT-891 CAT
 * read form; its answer is EXnnnn<parameter>;. Never issue EX setters here. */
static bool yaesu_menu_read(rr_backend_t *b, const char *id, char *value, size_t capacity) {
   yaesu_state_t *s = ys(b);
   if (!s || !s->profile || s->model_id != 650 || !id || strlen(id) != 4 ||
       !capacity || !value) return false;
   bool advertised = false;
   for (size_t i = 0; i < s->profile->menu_count; ++i)
      if (!strcmp(id, s->profile->menu[i].id)) { advertised = true; break; }
   if (!advertised) return false;
   char command[16], prefix[16], reply[128];
   snprintf(prefix, sizeof(prefix), "EX%s", id);
   snprintf(command, sizeof(command), "%s;", prefix);
   if (!cat(s, command, prefix, reply, sizeof(reply))) return false;
   size_t n = strlen(prefix);
   if (strlen(reply) <= n || strlen(reply + n) >= capacity) return false;
   snprintf(value, capacity, "%s", reply + n);
   return true;
}
static bool create(rr_backend_t *b) {
   yaesu_state_t *s = calloc(1, sizeof(*s));

   if (!s) {
      return true;
   }
   s->fd = -1;
   s->device = rr_backend_config_get_exp(b, "yaesu.device");

   if (!s->device) {
      s->device = rr_backend_config_get_exp(b, "hamlib.device");
   }
   s->baud = rr_backend_config_get_int(b, "yaesu.baud", rr_backend_config_get_int(b, "hamlib.baud", 38400));

   if (!s->device || !speed_for(s->baud)) {
      free(s->device);
      free(s);

      return true;
   }
   rr_backend_instance_set_data(b, s);

   if (!serial_open(s)) {
      rr_backend_log(b, LOG_WARN, "backend.yaesu", "%s: CAT device %s unavailable; retry on next poll", rr_backend_instance_alias(b), s->device);
   }
   /* ID0650=FT-891, ID0670=FT-991A. Unknown models are not guessed. */
   char id_reply[24];
   if (cat(s, "ID;", "ID", id_reply, sizeof(id_reply)) && strlen(id_reply) == 6) {
      int id;
      if (decimal(id_reply + 2, 4, &id)) {
         s->profile = yaesu_model_by_id(id);
         if (s->profile) s->model_id = id;
      }
   }
   if (!s->model_id)
      rr_backend_log(b, LOG_WARN, "backend.yaesu",
         "Unrecognized or unavailable Yaesu CAT ID; model-specific MENU disabled");
   const char *props[] = {
      "display.contrast", "display.key-backlight", RR_PROP_DISPLAY_BRIGHTNESS,
      "display.tx-indicator", RR_PROP_SPEAKER_VOLUME
   };

   for (unsigned i = 0 ; s->profile && s->profile->ft891_controls && i < sizeof(props) / sizeof(props[0]) ; i++) {
      rr_property_descriptor_t d = {
         .name = props[i], .type = VAL_INT, .readable = true,
         .writable = true, .unit = "%", .has_min = true, .has_max = true, .has_step = true,
         .minimum.i = 0, .maximum.i = 100, .step.i = 1
      };
      rr_rig_property_define(b->owner, &d);
   }

   for (size_t i = 0; s->profile && s->profile->ft891_controls && i < sizeof(yaesu_controls) / sizeof(yaesu_controls[0]); ++i) {
      rr_property_descriptor_t d = {
         .name = yaesu_controls[i].name, .type = VAL_INT, .readable = true,
         .writable = true, .unit = "%", .has_min = true, .has_max = true, .has_step = true,
         .minimum.i = 0, .maximum.i = 100, .step.i = 1
      };
      rr_rig_property_define(b->owner, &d);
   }
   if (s->profile && s->model_id == 650) {
      for (size_t i = 0; i < s->profile->menu_count; ++i) {
         char property[32];
         snprintf(property, sizeof(property), "menu.%s", s->profile->menu[i].id);
         rr_property_descriptor_t d = {
            .name = property, .type = VAL_STR, .readable = true,
            .writable = false
         };
         rr_rig_property_define(b->owner, &d);
      }
   }
   rr_property_descriptor_t meter = {
      .name = "meter.signal", .type = VAL_INT, .readable = true,
      .writable = false, .unit = "%", .has_min = true, .has_max = true,
      .minimum.i = 0, .maximum.i = 100
   };
   if (s->profile && s->profile->ft891_controls)
      rr_rig_property_define(b->owner, &meter);
   return false;
}
static void destroy(rr_backend_t *b) {
   yaesu_state_t *s = ys(b);

   if (!s) {
      return;
   }

   if (s->fd >= 0) {
      close(s->fd);
   }
   free(s->device);
   free(s);
   rr_backend_instance_set_data(b, NULL);
}
static const char *manufacturer(rr_backend_t *b) {
   (void)b;

   return "Yaesu";
}
static const char *model_name(rr_backend_t *b) {
   (void)b;

   yaesu_state_t *s = ys(b);
   return s && s->profile ? s->profile->model : "Unknown Yaesu";
}

static const rr_backend_funcs_t api = {
   .create = create, .destroy = destroy, .poll_state = state, .vfo_supported = supported,
   .freq_set = frequency_set, .freq_get = frequency_get,
   .ptt_set = ptt_set, .ptt_get = ptt_get,
   .mode_get = mode_get, .mode_get_str = mode_get_str, .mode_set = mode_set,
   .rig_control_set = control, .manufacturer = manufacturer, .model_name = model_name,
   .menu_foreach = yaesu_menu_foreach, .menu_read = yaesu_menu_read
};

const rr_backend_type_t rr_backend_yaesu = {
   .name = "yaesu", .description = "Native Yaesu FT-891 CAT",
   .uses_property_state = true, .api = &api
};
void rr_backend_yaesu_register(void) {
   rr_backend_type_register(&rr_backend_yaesu);
}
