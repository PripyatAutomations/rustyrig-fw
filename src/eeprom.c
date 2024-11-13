/*
 * Here we deal with a few different ways of accessing flash
 * depending on how it's connected.
 *
 * We support the following:
 *	mmaping a file on HOST_POSIX,
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
#if	defined(HOST_POSIX)
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#endif
#include "state.h"
#include "logger.h"
#include "eeprom.h"
#include "i2c.h"
#include "crc32.h"
#define	EEPROM_C
#include "eeprom_layout.h"		// in $builddir/ and contains offset/size/type data

extern struct GlobalState rig;	// Global state

// Returns either the index of they key in eeprom_layout or -1 if not found
uint32_t eeprom_offset_index(const char *key) {
   uint32_t max_entries = -1, idx = -1;

   max_entries = (sizeof(eeprom_layout) / sizeof(eeprom_layout[0]));

   if (max_entries == 0)
      return -1;

   for (idx = 0; idx < max_entries; idx++) {
      if (strncasecmp(key, eeprom_layout[idx].key, strlen(key)) == 0) {
         Log(LOG_DEBUG, "match for key %s at index %d", key, idx);
         return idx;
      }
   }

   Log(LOG_DEBUG, "No match found for key %s in eeprom_layout", key);
   return -1;
}

uint32_t eeprom_get_int(uint32_t idx) {
   if (idx < 0)
      return -1;

   // Extract the 32 bits (uint32_t)
   return -1;
}

const char *eeprom_get_str(uint32_t idx) {
   char *ret = NULL;

   // uint32_t eeprom_read_block(uint8_t *buf, size_t offset, size_t len)
   return ret;
}

uint32_t eeprom_get_int_i(const char *key) {
   return eeprom_get_int(eeprom_offset_index(key));
}

const char *eeprom_get_str_i(const char *key) {
   return eeprom_get_str(eeprom_offset_index(key));
}

uint32_t eeprom_init(void) {
   size_t bytes_read = 0;

#if	defined(HOST_POSIX)
   struct stat sb;
   size_t eeprom_len;
   ssize_t s;

   uint32_t fd = open(HOST_EEPROM_FILE, O_RDWR);
   if (fd == -1) {
      Log(LOG_CRIT, "EEPROM Initialization failed: %s: %d: %s", HOST_EEPROM_FILE, errno, strerror(errno));
      return -1;
   }

// we do not have fstat (or a file system at all) on the radio...   
#if	defined(HOST_POSIX)
   if (fstat(fd, &sb) == -1) {
      Log(LOG_CRIT, "EEPROM image %s does not exist, run 'make eeprom' and try again", HOST_EEPROM_FILE);
      return -1;
   }
#endif
   eeprom_len = sb.st_size;
   rig.eeprom_fd = fd;
   rig.eeprom_mmap = mmap(NULL, eeprom_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
   rig.eeprom_ready = 1;
   Log(LOG_INFO, "EEPROM Initialized (%s) %s", (rig.eeprom_fd > 0 ? "mmap" : "i2c"),
                                           (rig.eeprom_fd > 0 ? HOST_EEPROM_FILE : ""));
#endif	// defined(HOST_POSIX)

   return 0;
}

uint32_t eeprom_read_block(uint8_t *buf, size_t offset, size_t len) {
   uint32_t res = 0;
   ssize_t myoff = 0;
   uint8_t *p = buf;

   // Check for memory mapped style EEPROMs
   if (rig.eeprom_mmap != NULL) {
      if (buf == NULL || offset <= 0 || len <= 0) {
         return -1;
      }

      while (myoff <= len) {
         buf[myoff] = *rig.eeprom_mmap + offset + myoff;

         myoff++;
      }
   }

   return res;
}

uint32_t eeprom_write(size_t offset, uint8_t data) {
   uint32_t res = 0;
   ssize_t myoff = 0;
#if	0
   uint8_t *ptr = NULL;

   // Check for memory mapped style EEPROMs
   if (rig.eeprom_mmap != NULL) {
      &ptr = *(rig.eeprom_mmap + offset);
      ptr = (uint8_t)data;
   }
#endif
   return res;
}

uint8_t eeprom_read(size_t offset) {
   uint8_t res = 0;

   if (offset <= 0) {
      errno = EADDRNOTAVAIL;
      return res;
   }

/*
   // for host mode or other memory mapped
   if (rig.eeprom_map != NULL) {
      res = (uint8_t)(rig.eeprom_mmap + offset);
   }
*/
   return res;
}

uint32_t eeprom_write_block(void *buf, size_t offset, size_t len) {
   uint32_t res = -1;

   return res;
}

uint32_t eeprom_checksum_generate(void) {
   uint32_t sum = 0;

   // Check for memory mapped style EEPROMs
   if (rig.eeprom_mmap != NULL) {
      sum = crc32buf((char *)rig.eeprom_mmap, EEPROM_SIZE - 4);
   }

   return sum;
}

// Check the checksum
uint32_t eeprom_validate_checksum(void) {
   uint32_t calc_sum = 0, curr_sum = 0;
   uint32_t *chksum_addr = (uint32_t *)(&rig.eeprom_mmap[EEPROM_SIZE - 4]);

   // Check for memory mapped style EEPROMs
   if (rig.eeprom_mmap != NULL) {
      curr_sum = *(uint32_t *)chksum_addr;
      calc_sum = eeprom_checksum_generate();
   }

   // return -1 if the checksums do not match
   if (calc_sum != curr_sum) {
      Log(LOG_WARN, "* Verify checksum failed: calculated <%x> but read <%x> *", calc_sum, curr_sum);
      return -1;
   } else {
      Log(LOG_INFO, "EEPROM checkum <%x> is correct, loading settings...", calc_sum);
   }

   return 0;
}

// Load the EEPROM data into the state structures
uint32_t eeprom_load_config(void) {
   // Validate EEPROM checksum before applying configuration...
   if (eeprom_validate_checksum() != 0) {
      Log(LOG_WARN, "Ignoring saved configuration due to EEPROM checksum mismatch");

      // Set EEPROM corrupt flag
      rig.eeprom_corrupted = 1;

      // XXX: Here we should load some default network and authentication settings...
      return -1;
   }

   // walk over the eeprom_layout and apply each setting to our state object (rig)
   uint32_t cfg_rows = sizeof(eeprom_layout) / sizeof(eeprom_layout[0]);
   for (uint32_t i = 0; i < cfg_rows; i++) {
       Log(LOG_DEBUG, "key: %s type: %d offset: %d size: %d", eeprom_layout[i].key,
           eeprom_layout[i].type, eeprom_layout[i].offset, eeprom_layout[i].size);
   }
   Log(LOG_INFO, "Configuration successfully loaded from EEPROM");
   return 0;
}

// Write the state structures out to EEPROM
uint32_t eeprom_write_config(uint32_t force) {
   uint32_t sum = 0;

   // If we do not have any pending changes, don't bother
   if (rig.eeprom_dirty == 0) {
      return 0;
   }

   // We are running defaults if we got here, so prompt the user first
   if (rig.eeprom_corrupted && !force) {
      Log(LOG_WARN, "Not saving EEPROM since corrupt flag set");
      return -1;
   }

   sum = eeprom_checksum_generate();

   Log(LOG_INFO, "Saving to EEPROM not yet supported");
   return 0;
}

char *get_serial_number(void) {
   uint32_t idx = eeprom_offset_index("device/serial");
   Log(LOG_INFO, "Device serial number: %s", eeprom_get_str(idx));
   return NULL;
}
