// rrserver/gpio.c: libgpiod v2 adapter for any kernel GPIO controller
// SPDX-License-Identifier: MIT
#include <errno.h>
#include <glob.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <rrserver/gpio.private.h>
#ifdef USE_GPIO
#include <gpiod.h>

typedef struct {
   char *path;
   unsigned offset;
   bool output, active_low, safe;
   struct gpiod_line_request *request;
} gpio_line_t;

static bool line_release(void *data) {
   gpio_line_t *line = data;
   if (!line->request)
      return true;
   if (line->output && gpiod_line_request_set_value(line->request, line->offset, line->safe ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) < 0)
      return false;
   gpiod_line_request_release(line->request);
   line->request = NULL;
   return true;
}

static void line_destroy(void *data) {
   gpio_line_t *line = data;
   if (!line_release(line)) {
      Log(LOG_CRIT, "hardware.gpio", "Cannot restore safe value on %s:%u: %s", line->path, line->offset, strerror(errno));
      gpiod_line_request_release(line->request);
   }
   g_free(line->path);
   g_free(line);
}

static bool line_claim(void *data, const char *owner) {
   gpio_line_t *line = data;
   struct gpiod_chip *chip = gpiod_chip_open(line->path);
   if (!chip)
      return false;
   struct gpiod_line_settings *settings = gpiod_line_settings_new();
   struct gpiod_line_config *config = gpiod_line_config_new();
   struct gpiod_request_config *request = gpiod_request_config_new();
   bool ok = settings && config && request;
   if (ok) {
      gpiod_request_config_set_consumer(request, owner);
      gpiod_line_settings_set_active_low(settings, line->active_low);
      ok = gpiod_line_settings_set_direction(settings, line->output ? GPIOD_LINE_DIRECTION_OUTPUT : GPIOD_LINE_DIRECTION_INPUT) == 0 &&
         gpiod_line_settings_set_output_value(settings, line->safe ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) == 0 &&
         gpiod_line_config_add_line_settings(config, &line->offset, 1, settings) == 0;
      if (ok)
         line->request = gpiod_chip_request_lines(chip, request, config);
      ok = ok && line->request;
   }
   int saved = errno;
   if (request)
      gpiod_request_config_free(request);
   if (config)
      gpiod_line_config_free(config);
   if (settings)
      gpiod_line_settings_free(settings);
   gpiod_chip_close(chip);
   errno = saved;
   return ok;
}

static const rr_hardware_driver_t gpio_api = { line_claim, line_release, line_destroy };

bool rr_gpio_discover(rr_hardware_t *registry) {
   glob_t paths = { 0 };
   int result = glob("/dev/gpiochip*", 0, NULL, &paths);
   if (result == GLOB_NOMATCH) {
      globfree(&paths);
      return true;
   }
   if (result) {
      globfree(&paths);
      return false;
   }
   bool ok = true;
   for (size_t i = 0; i < paths.gl_pathc; i++) {
      const char *path = paths.gl_pathv[i];
      struct gpiod_chip *chip = gpiod_chip_open(path);
      if (!chip) {
         Log(LOG_WARN, "hardware.gpio", "Cannot inspect %s: %s", path, strerror(errno));
         continue;
      }
      struct gpiod_chip_info *info = gpiod_chip_get_info(chip);
      if (info) {
         rr_hardware_spec_t spec = {.id = path,.path = path,.kind = "gpio-chip",.driver = "gpiod" };
         if (!rr_hardware_find(registry, path) && !rr_hardware_add(registry, &spec))
            ok = false;
         Log(LOG_INFO, "hardware.gpio", "%s: %s (%zu lines)", path, gpiod_chip_info_get_label(info), gpiod_chip_info_get_num_lines(info));
         gpiod_chip_info_free(info);
      } else {
         Log(LOG_WARN, "hardware.gpio", "Cannot inspect %s metadata: %s", path, strerror(errno));
         ok = false;
      }
      gpiod_chip_close(chip);
   }
   globfree(&paths);
   return ok;
}

/* Called by the configuration loader after validating values. */
bool rr_gpio_add_line(rr_hardware_t *registry, const char *id, const char *path, unsigned offset, bool output, bool active_low, bool safe) {
   for (size_t i = 0; i < rr_hardware_count(registry); i++) {
      const rr_hardware_device_t *d = rr_hardware_at(registry, i);
      if (strcmp(rr_hardware_driver(d), "gpiod-line"))
         continue;
      gpio_line_t *other = rr_hardware_data(d);
      if (!strcmp(other->path, path) && other->offset == offset) {
         errno = EBUSY;
         return false;
      }
   }
   struct gpiod_chip *chip = gpiod_chip_open(path);
   if (!chip)
      return false;
   struct gpiod_line_info *info = gpiod_chip_get_line_info(chip, offset);
   int saved = errno;
   if (info)
      gpiod_line_info_free(info);
   gpiod_chip_close(chip);
   if (!info) {
      errno = saved;
      return false;
   }
   if (!rr_hardware_find(registry, path)) {
      rr_hardware_spec_t parent = {.id = path,.path = path,.kind = "gpio-chip",.driver = "gpiod" };
      if (!rr_hardware_add(registry, &parent))
         return false;
   }
   gpio_line_t *line = g_new0(gpio_line_t, 1);
   *line = (gpio_line_t) {
   .path = g_strdup(path),.offset = offset,.output = output,.active_low = active_low,.safe = safe};
   rr_hardware_spec_t spec = {.id = id,.path = path,.parent = path,.kind = output ? "gpio-output" : "gpio-input",.driver = "gpiod-line",.api = &gpio_api,.data =
         line };
   if (rr_hardware_add(registry, &spec))
      return true;
   line_destroy(line);
   errno = EINVAL;
   return false;
}

static gpio_line_t *claimed_line(const rr_hardware_device_t *device) {
   if (!device || strcmp(rr_hardware_driver(device), "gpiod-line") || !*rr_hardware_owner(device)) {
      errno = EPERM;
      return NULL;
   }
   return rr_hardware_data(device);
}

int rr_gpio_read(const rr_hardware_device_t *device) {
   gpio_line_t *line = claimed_line(device);
   return line ? gpiod_line_request_get_value(line->request, line->offset) : -1;
}
bool rr_gpio_write(const rr_hardware_device_t *device, bool active) {
   gpio_line_t *line = claimed_line(device);
   if (!line)
      return false;
   if (!line->output) {
      errno = EPERM;
      return false;
   }
   return gpiod_line_request_set_value(line->request, line->offset, active ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) == 0;
}
#else
bool rr_gpio_discover(rr_hardware_t *registry) {
   (void)registry;
   return true;
}
bool rr_gpio_add_line(rr_hardware_t *registry, const char *id, const char *path, unsigned offset, bool output, bool active_low, bool safe) {
   errno = ENOTSUP;
   return false;
}
int rr_gpio_read(const rr_hardware_device_t *device) {
   errno = ENOTSUP;
   return -1;
}
bool rr_gpio_write(const rr_hardware_device_t *device, bool active) {
   errno = ENOTSUP;
   return false;
}
#endif
