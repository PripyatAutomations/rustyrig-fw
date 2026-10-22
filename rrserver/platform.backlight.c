//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// rrserver/platform.backlight.c: optional Linux sysfs LCD control.
#include <rrserver/platform.backlight.h>
#include <librustyaxe/core.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef __linux__
static bool backlight_device(char *name, size_t cap) {
   const char *configured = cfg_get("platform.backlight.device");

   if (configured && *configured) {
      size_t n = strlen(configured);

      if (!n || n >= cap || n > 63 || configured[0] == '.') {
         return false;
      }

      for (const unsigned char *p = (const unsigned char *)configured ; *p ; ++p) {
         if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.')) {
            return false;
         }
      }

      snprintf(name, cap, "%s", configured);

      return true;
   }
   /* Auto-select only if exactly one device exists. Multiple devices require a locally configured name, not a guess supplied over the network. */
   DIR *dir = opendir("/sys/class/backlight");

   if (!dir) {
      return false;
   }
   int count = 0;
   struct dirent *entry;
   while ((entry = readdir(dir))) {
      if (entry->d_name[0] == '.') {
         continue;
      }

      if (++count > 1) {
         break;
      }

      if (strlen(entry->d_name) >= cap) {
         count = 2;
         break;
      }
      snprintf(name, cap, "%s", entry->d_name);
   }
   closedir(dir);

   return count == 1;
}
static bool read_number(const char *name, const char *attr, int *out) {
   char path[256];
   snprintf(path, sizeof(path), "/sys/class/backlight/%s/%s", name, attr);
   FILE *fp = fopen(path, "r");

   if (!fp) {
      return false;
   }
   long value;
   bool ok = fscanf(fp, "%ld", &value) == 1 && value >= 0 && value <= INT_MAX;
   fclose(fp);

   if (ok) {
      *out = (int)value;
   }

   return ok;
}
#endif

bool rrserver_backlight_writable(void) {
#ifdef __linux__
   char device[64], path[256];

   if (!backlight_device(device, sizeof(device))) {
      return false;
   }
   snprintf(path, sizeof(path), "/sys/class/backlight/%s/brightness", device);

   return access(path, W_OK) == 0;
#else

   return false;
#endif
}
bool rrserver_backlight_control(int requested, rr_backlight_state_t *state) {
   if (!state) {
      return false;
   }
   memset(state, 0, sizeof(*state));
#ifdef __linux__

   if (!backlight_device(state->name, sizeof(state->name))) {
      return false;
   }

   if (!read_number(state->name, "max_brightness", &state->maximum) || state->maximum < 1) {
      return false;
   }
   state->minimum = cfg_get_int("platform.backlight.minimum", 1);

   if (state->minimum < 1) {
      state->minimum = 1;
   }

   if (state->minimum > state->maximum) {
      state->minimum = state->maximum;
   }

   if (requested >= 0) {
      if (requested < state->minimum || requested > state->maximum) {
         return false;
      }
      char path[256];
      snprintf(path, sizeof(path), "/sys/class/backlight/%s/brightness", state->name);
      int fd = open(path, O_WRONLY | O_CLOEXEC);

      if (fd < 0) {
         Log(LOG_WARN, "platform.backlight", "Cannot open %s for writing: %s (rrserver needs write permission)", path, strerror(errno));

         return false;
      }
      char value[32];
      int len = snprintf(value, sizeof(value), "%d\n", requested);
      bool written = len > 0 && write(fd, value, (size_t)len) == len;
      int saved_errno = errno;
      close(fd);

      if (!written) {
         errno = saved_errno;
         Log(LOG_WARN, "platform.backlight", "Cannot write %s: %s", path, strerror(saved_errno));

         return false;
      }
   }

   return read_number(state->name, "brightness", &state->current);
#else
   (void)requested;

   return false;
#endif
}
