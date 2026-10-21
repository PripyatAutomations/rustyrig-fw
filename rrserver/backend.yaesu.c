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

typedef struct yaesu_state {
   int fd, baud;
   char *device;
   long frequency[2];
   rr_mode_t mode[2];
   bool ptt;
   int dimmer[4], af;
   bool dimmer_known, af_known;
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

   if (index == b->active_vfo) {
      if (get_dimmer(s)) {
         observe(b, "display.contrast", (s->dimmer[0] - 1) * 100 / 14);
         observe(b, "display.key-backlight", (s->dimmer[1] - 1) * 100 / 14);
         observe(b, RR_PROP_DISPLAY_BRIGHTNESS, s->dimmer[2] * 100 / 15);
         observe(b, "display.tx-indicator", s->dimmer[3] * 100 / 15);
      } else {
         rr_backend_property_unavailable(b, NULL, "display.contrast");
         rr_backend_property_unavailable(b, NULL, "display.key-backlight");
         rr_backend_property_unavailable(b, NULL, RR_PROP_DISPLAY_BRIGHTNESS);
         rr_backend_property_unavailable(b, NULL, "display.tx-indicator");
      }

      if (get_af(s)) {
         observe(b, RR_PROP_SPEAKER_VOLUME, (s->af * 100 + 127) / 255);
      } else {
         rr_backend_property_unavailable(b, NULL, RR_PROP_SPEAKER_VOLUME);
      }
   }
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
   const char *props[] = {
      "display.contrast", "display.key-backlight", RR_PROP_DISPLAY_BRIGHTNESS,
      "display.tx-indicator", RR_PROP_SPEAKER_VOLUME
   };

   for (unsigned i = 0 ; i < sizeof(props) / sizeof(props[0]) ; i++) {
      rr_property_descriptor_t d = {
         .name = props[i], .type = VAL_INT, .readable = true,
         .writable = true, .unit = "%", .has_min = true, .has_max = true, .has_step = true,
         .minimum.i = 0, .maximum.i = 100, .step.i = 1
      };
      rr_rig_property_define(b->owner, &d);
   }

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

   return "FT-891";
}

static const rr_backend_funcs_t api = {
   .create = create, .destroy = destroy, .poll_state = state, .vfo_supported = supported,
   .freq_set = frequency_set, .freq_get = frequency_get,
   .ptt_set = ptt_set, .ptt_get = ptt_get,
   .mode_get = mode_get, .mode_get_str = mode_get_str, .mode_set = mode_set,
   .rig_control_set = control, .manufacturer = manufacturer, .model_name = model_name
};

const rr_backend_type_t rr_backend_yaesu = {
   .name = "yaesu", .description = "Native Yaesu FT-891 CAT",
   .uses_property_state = true, .api = &api
};
void rr_backend_yaesu_register(void) {
   rr_backend_type_register(&rr_backend_yaesu);
}
