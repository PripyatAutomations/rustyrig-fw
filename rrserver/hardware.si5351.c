// Linux I2C adapter for Si5351A. Part of RustyRig; MIT license.
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <rrserver/si5351.h>
#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#endif

typedef struct {
   char *bus;
   unsigned address;
   int fd;
   rr_si5351_config_t config;
   rr_si5351_t clock;
} oscillator_t;

static bool transfer(oscillator_t *o, uint8_t reg, uint8_t *bytes, size_t count, bool reading) {
#ifdef __linux__
   uint8_t buffer[9] = { reg };
   if (!count || count > 8) {
      errno = EINVAL;
      return false;
   }
   struct i2c_msg messages[2] = {
      {.addr = o->address,.len = reading ? 1 : count + 1,.buf = buffer },
      {.addr = o->address,.flags = I2C_M_RD,.len = count,.buf = bytes }
   };
   if (!reading)
      memcpy(buffer + 1, bytes, count);
   struct i2c_rdwr_ioctl_data transaction = {.msgs = messages,.nmsgs = reading ? 2 : 1 };
   int result = ioctl(o->fd, I2C_RDWR, &transaction);
   if (result == (int)transaction.nmsgs)
      return true;
   if (result >= 0)
      errno = EIO;
   return false;
#else
   errno = ENOTSUP;
   return false;
#endif
}
static bool read_register(void *context, uint8_t reg, uint8_t *value) {
   return transfer(context, reg, value, 1, true);
}
static bool write_register(void *context, uint8_t reg, const uint8_t *value, size_t count) {
   return transfer(context, reg, (uint8_t *) value, count, false);
}
static void wait_register(void *context) {
   (void)context;
   g_usleep(1000);
}

static bool claim(void *data, const char *owner) {
   oscillator_t *o = data;
   (void)owner;
#ifdef __linux__
   o->fd = open(o->bus, O_RDWR | O_CLOEXEC);
   if (o->fd < 0)
      return false;
   unsigned long functions = 0;
   // Do not use I2C_SLAVE_FORCE: refuse addresses owned by kernel drivers.
   bool ok = ioctl(o->fd, I2C_FUNCS, &functions) == 0;
   if (ok && !(functions & I2C_FUNC_I2C)) {
      errno = ENOTSUP;
      ok = false;
   }
   if (ok)
      ok = ioctl(o->fd, I2C_SLAVE, o->address) == 0;
   rr_si5351_io_t io = { o, read_register, write_register, wait_register };
   if (ok)
      ok = rr_si5351_start(&o->clock, &o->config, &io);
   if (ok)
      return true;
   int saved = errno;
   close(o->fd);
   o->fd = -1;
   errno = saved;
   return false;
#else
   errno = ENOTSUP;
   return false;
#endif
}
static bool release(void *data) {
   oscillator_t *o = data;
   if (o->fd < 0)
      return true;
   if (!rr_si5351_stop(&o->clock))
      return false;
   close(o->fd);
   o->fd = -1;
   return true;
}
static void destroy(void *data) {
   oscillator_t *o = data;
   if (!release(o)) {
      Log(LOG_CRIT, "hardware.si5351", "Cannot disable clocks on %s address 0x%02x: %s", o->bus, o->address, strerror(errno));
      close(o->fd);
   }
   g_free(o->bus);
   g_free(o);
}
static const rr_hardware_driver_t api = { claim, release, destroy };

bool rr_si5351_register(rr_hardware_t *r, const char *id, const char *bus, unsigned address, const rr_si5351_config_t *config) {
   rr_si5351_plan_t plan;
   if (!r || !id || !*id || !bus || !*bus || address < 0x08 || address > 0x77 || !rr_si5351_plan(config, 10000000, &plan) || rr_hardware_find(r, id)) {
      errno = EINVAL;
      return false;
   }
   char *path = realpath(bus, NULL);
   if (!path)
      return false;
   for (size_t i = 0; i < rr_hardware_count(r); i++) {
      const rr_hardware_device_t *d = rr_hardware_at(r, i);
      if (strcmp(rr_hardware_driver(d), "si5351a"))
         continue;
      oscillator_t *existing = rr_hardware_data(d);
      if (!strcmp(existing->bus, path) && existing->address == address) {
         free(path);
         errno = EBUSY;
         return false;
      }
   }
   if (!rr_hardware_find(r, path)) {
      rr_hardware_spec_t parent = {.id = path,.kind = "i2c-bus",.driver = "i2c-dev",.path = path };
      if (!rr_hardware_add(r, &parent)) {
         free(path);
         errno = EINVAL;
         return false;
      }
   }
   oscillator_t *o = g_new0(oscillator_t, 1);
   *o = (oscillator_t) {
   .bus = path,.address = address,.fd = -1,.config = *config};
   rr_hardware_spec_t spec = {.id = id,.kind = "oscillator",.driver = "si5351a",.path = path,.parent = path,.api = &api,.data = o };
   if (rr_hardware_add(r, &spec))
      return true;
   destroy(o);
   errno = EINVAL;
   return false;
}

bool rr_si5351_tune(rr_hardware_device_t *device, unsigned output, uint32_t hz) {
   if (!device || strcmp(rr_hardware_driver(device), "si5351a") || !*rr_hardware_owner(device)) {
      errno = EPERM;
      return false;
   }
   oscillator_t *o = rr_hardware_data(device);
   return rr_si5351_set(&o->clock, output, hz);
}
