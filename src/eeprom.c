/*
 * Here we deal with a few different ways of accessing flash
 * depending on how it's connected.
 *
 * We support the following:
 *	mmaping a file on HOST_BUILD,
 *	memory mapped eeprom/flash devices with direct reading/writing
 *	i2c connected parts
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#if	defined(HOST_BUILD)
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#endif
#include "state.h"
#include "logger.h"
#include "eeprom.h"
#include "i2c.h"

extern struct GlobalState rig;	// Global state

int eeprom_init(void) {
   size_t bytes_read = 0;

// emulation
#if	defined(HOST_BUILD)
   struct stat sb;
   size_t eeprom_len;
   ssize_t s;

   int fd = open(HOST_EEPROM_FILE, O_RDWR);
   if (fd == -1) {
      Log(CRIT, "EEPROM Initialization failed: %s: %d: %s", HOST_EEPROM_FILE, errno, strerror(errno));
      return -1;
   }
   
   if (fstat(fd, &sb) == -1) {
      Log(CRIT, "EEPROM image %s does not exist, run 'make eeprom' and try again", HOST_EEPROM_FILE);
      return -1;
   }
   eeprom_len = sb.st_size;
   rig.eeprom_fd = fd;
   rig.eeprom_mmap = mmap(NULL, eeprom_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
#else
   // XXX: Add support for memory mapped or i2c EEPROMs here
#endif
   rig.eeprom_ready = 1;
   Log(INFO, "EEPROM Initialized (%s) %s", (rig.eeprom_fd > 0 ? "mmap" : "i2c"),
                                           (rig.eeprom_fd > 0 ? HOST_EEPROM_FILE : ""));
   return 0;
}

int eeprom_read_block(u_int8_t *buf, size_t offset, size_t len) {
   int res = 0;
   ssize_t myoff = 0;
   u_int8_t *p = buf;

   if (buf == NULL || offset <= 0 || len <= 0) {
      return -1;
   }

#if	defined(HOST_BUILD)
   while (myoff <= len) {
      buf[myoff] = *rig.eeprom_mmap + offset + myoff;

      myoff++;
   }
#else
   // XXX: Add support for memory mapped or i2c EEPROMs here
#endif
   return res;
}

int eeprom_write(size_t offset, u_int8_t data) {
   int res = 0;
   ssize_t myoff = 0;
#if	0
   u_int8_t *ptr = NULL;

   if (rig.eeprom_mmap != NULL) {
      &ptr = *(rig.eeprom_mmap + offset);
      ptr = (u_int8_t)data;
   }
#endif
   return res;
}

u_int8_t eeprom_read(size_t offset) {
   u_int8_t res = 0;

   if (offset <= 0) {
      errno = EADDRNOTAVAIL;
      return res;
   }

/*
   // for host mode or other memory mapped
   if (rig.eeprom_map != NULL) {
      res = (u_int8_t)(rig.eeprom_mmap + offset);
   }
*/
   return res;
}

int eeprom_write_block(void *buf, size_t offset, size_t len) {
   int res = -1;

#if	defined(HOST_BUILD)
#else
   // XXX: Add support for memory mapped or i2c EEPROMs here
#endif
   return res;
}

u_int32_t eeprom_checksum_generate(void) {
   u_int32_t sum = 0;

   return sum;
}

// Check the checksum
int eeprom_validate_checksum(void) {
   u_int32_t calc_sum = eeprom_checksum_generate();
   u_int32_t curr_sum = 0;
// XXX: memory mapped --   eeprom[EEOFF_CHECKSUM];

   if (calc_sum != curr_sum)
      return -1;

   return 0;
}

// Load the EEPROM data into the state structures
int eeprom_load_config(void) {
   // Validate EEPROM checksum before applying
   if (eeprom_validate_checksum() != 0) {
      Log(CRIT, "Ignoring saved configuration due to EEPROM checksum mismatch");

      // Set EEPROM corrupt flag
      rig.eeprom_corrupted = 1;
   }

   Log(INFO, "Configuration successfully loaded from EEPROM");
   return 0;
}

// Write the state structures out to EEPROM
int eeprom_write_config(int force) {
   u_int32_t sum = 0;

   // If we do not have any pending changes, don't bother
   if (rig.eeprom_dirty == 0) {
      return 0;
   }

   // We are running defaults if we got here, so prompt the user first
   if (rig.eeprom_corrupted && !force) {
      Log(WARN, "Not saving EEPROM since corrupt flag set");
      return -1;
   }

   sum = eeprom_checksum_generate();

   Log(INFO, "Saving to EEPROM not yet supported");
   return 0;
}

int get_serial_number(void) {
   return 0;
}
