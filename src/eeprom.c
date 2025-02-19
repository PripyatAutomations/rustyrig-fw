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
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

// support for file-backed emulated eeproms
#if	defined(HOST_POSIX)
// some of these headers may be required for other platforms, edit as needed
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h> // For inet_ntop() - probably need to stub on non-posix
#endif
// support for stm32 flash-backed eeprom emulation
#if	defined(HOST_STM32)
//
#endif

#include "state.h"
#include "logger.h"
#include "eeprom.h"
#include "i2c.h"
#include "crc32.h"

#define	EEPROM_C		// Let the header know we're in the C file
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
         Log(LOG_DEBUG, "match for key %s at index %d: type=%d, offset=%lu, sz=%lu", key, idx,
            eeprom_layout[idx].type, eeprom_layout[idx].offset, eeprom_layout[idx].size);
         return idx;
      }
   }

   Log(LOG_DEBUG, "No match found for key %s in eeprom_layout", key);
   return -1;
}

const char *eeprom_offset_name(uint32_t idx) {
   if (idx <= 0)
      return NULL;

   const char *rv = eeprom_layout[idx].key;

   Log(LOG_DEBUG, "match for index %d: %s\n", idx, rv);
   return rv;
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
   rig.eeprom_size = eeprom_len = sb.st_size;
   rig.eeprom_fd = fd;
   rig.eeprom_mmap = mmap(NULL, eeprom_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
   rig.eeprom_ready = 1;
   Log(LOG_INFO, "EEPROM Initialized (%s%s)", (rig.eeprom_fd > 0 ? "mmap:" : "phys"),
                                           (rig.eeprom_fd > 0 ? HOST_EEPROM_FILE : ""));
#endif	// defined(HOST_POSIX)

   return 0;
}

uint32_t eeprom_read_block(uint8_t *buf, size_t offset, size_t len) {
   uint32_t res = 0;
   ssize_t myoff = 0;
   uint8_t *p = buf;

   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return -1;
   }

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

   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return -1;
   }

   // Check for memory-mapped EEPROM
   if (rig.eeprom_mmap != NULL) {
      uint8_t *ptr = (uint8_t *)rig.eeprom_mmap + offset; // Correct pointer arithmetic
      *ptr = data; // Write data to the mapped EEPROM memory
   } else {
      res = 1; // Indicate failure (e.g., EEPROM not mapped)
   }

   return res;
}

void *eeprom_read(size_t offset) {
   void *res = NULL;

   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return NULL;
   }

   if (offset <= 0) {
      errno = EADDRNOTAVAIL;
      return res;
   }

   // for host mode or other memory mapped
   if (rig.eeprom_mmap != NULL) {
      res = (void *)(rig.eeprom_mmap + offset);
   }
   return res;
}

uint32_t eeprom_write_block(void *buf, size_t offset, size_t len) {
   uint32_t res = -1;

   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return -1;
   }

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
bool eeprom_validate_checksum(void) {
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

      // if the eeprom is mmapped, free it
      if (rig.eeprom_mmap != NULL) {
         munmap(rig.eeprom_mmap, rig.eeprom_size);
         rig.eeprom_mmap = NULL;
      }

      //  if there's a file handle open for the eeprom.bin, close it
      if (rig.eeprom_fd >= 0) {
         close(rig.eeprom_fd);
         rig.eeprom_fd = -1;
      }

      rig.eeprom_ready = -1;
      rig.eeprom_corrupted = 1;
      return true;
   } else {
      Log(LOG_INFO, "EEPROM checkum <%x> is correct, loading settings...", calc_sum);
   }

   return false;
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

   Log(LOG_DEBUG, "*** Configuration Dump ***");

   // walk over the eeprom_layout and apply each setting to our state object (rig)
   uint32_t cfg_rows = sizeof(eeprom_layout) / sizeof(eeprom_layout[0]);
   int chan_slots_loaded = 0;

   for (uint32_t i = 0; i < cfg_rows; i++) {
       // Common types: string, int, float
       int mb_sz = 512;
       char mbuf[mb_sz];
       unsigned char *addr;
       memset(mbuf, 0, sizeof(mbuf));
       switch(eeprom_layout[i].type) {
           case EE_BOOL:
                snprintf(mbuf, mb_sz, "%s", (eeprom_get_int_i(i) ? "true" : "false"));
                break;
           case EE_CALL:
           case EE_GRID:
           case EE_STR:
                snprintf(mbuf, mb_sz, "%s", eeprom_get_str_i(i));
                break;
           case EE_CHAN_HEADER:
                Log(LOG_DEBUG, "! Found Chan Mem Header");
                break;
           case EE_CHAN_GROUPS:
                Log(LOG_DEBUG, "! Found Chan Grp Table");
                break;
           case EE_CHAN_SLOT:
                // XXX: we need to parse channel data - needs buildconf to add structs to eeprom_types.h
                chan_slots_loaded++;
                Log(LOG_DEBUG, "! Channel Slot: %d\n", chan_slots_loaded);
                break;
           case EE_CLASS:
                // XXX: we need to implement license classes
                addr = rig.eeprom_mmap + eeprom_layout[i].offset;
                memcpy(mbuf, addr, 2);
                *(addr + 2) = '\0';
                break;
           case EE_FLOAT:
           case EE_FREQ:
                snprintf(mbuf, mb_sz, "%0.3f", eeprom_get_float_i(i));
                break;
           case EE_INT:
                snprintf(mbuf, mb_sz, "%d", eeprom_get_int_i(i));
                break;
           case EE_IP4: {
                uint8_t ip4[4];
                memcpy(ip4, rig.eeprom_mmap + eeprom_layout[i].offset, 4);
                snprintf(mbuf, mb_sz, "%u.%u.%u.%u", ip4[0], ip4[1], ip4[2], ip4[3]);
                break;
           }
           case EE_IP6: {
                uint8_t ip6[16];
                memcpy(ip6, rig.eeprom_mmap + eeprom_layout[i].offset, 16);
                inet_ntop(AF_INET6, ip6, mbuf, mb_sz);
                break;
           }
           default:
                Log(LOG_DEBUG, "unhandled type %d", eeprom_layout[i].type);
                break;
//   EE_MODE,                     /* Operating mode (modulation) */
       }
       Log(LOG_DEBUG, "key: %s type: %d offset: %d size: %d |%s|", eeprom_layout[i].key,
           eeprom_layout[i].type, eeprom_layout[i].offset, eeprom_layout[i].size, mbuf);
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

uint32_t get_serial_number(void) {
   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return -1;
   }
   uint32_t val = eeprom_get_int("device/serial");
   Log(LOG_INFO, "Device serial number: %lu", val);
   return val;
}

////////////////////////////////
// Support for delayed writes //
////////////////////////////////
// Do we have any changes to write?
bool check_pending_eeprom_changes(void) {
    return false;
}

// How long ago was the eeprom changed?
uint32_t get_eeprom_change_age(void) {
   return 0;
}

// Write the pending changes if any
bool write_pending_eeprom_changes(void) {
   if (check_pending_eeprom_changes()) { 
      // XXx: this should come from config
      int max_age = 60;
      if (get_eeprom_change_age() > max_age) {
         // Commit changes
      }
   }
   return false;
}

////////////////
uint32_t eeprom_get_int_i(uint32_t idx) {
   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return -1;
   }

   if (idx == -1)
      return -1;

   uint32_t value = 0;
   memcpy(&value, rig.eeprom_mmap + eeprom_layout[idx].offset, sizeof(uint32_t));
   return value;
}

uint32_t eeprom_get_int(const char *key) {
   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return -1;
   }

   return eeprom_get_int_i(eeprom_offset_index(key));
}


float eeprom_get_float_i(uint32_t idx) {
   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return -1;
   }

   if (idx == -1)
      return -1;

   float value = 0;
   memcpy(&value, rig.eeprom_mmap + eeprom_layout[idx].offset, sizeof(float));
   return value;
}

float eeprom_get_float(const char *key) {
   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return -1;
   }

   return eeprom_get_float_i(eeprom_offset_index(key));
}

const char *eeprom_get_str_i(uint32_t idx) {
   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return NULL;
   }

   if (idx == -1)
      return NULL;

   static char buf[256];
   size_t len = eeprom_layout[idx].size;
   if (len == (size_t)-1) len = 255; // Arbitrary max size for flexible strings

   memset(buf, 0, sizeof(buf));
   u_int8_t *myaddr = rig.eeprom_mmap + eeprom_layout[idx].offset;
//   Log(LOG_DEBUG, "eeprom_get_str EEPROM[%i] at %x with offset %d with final addr %x", idx, rig.eeprom_mmap,  eeprom_layout[idx].offset, myaddr);
   memcpy(buf, myaddr, len);

   // Ensure null termination
   buf[len] = '\0';
   return buf;
}

const char *eeprom_get_str(const char *key) {
   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return NULL;
   }
   return eeprom_get_str_i(eeprom_offset_index(key));
}

struct in_addr *eeprom_get_ip4(const char *key, struct in_addr *sin) {
   if (sin == NULL || rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return NULL;
   }

   // this is stored as 4 packed bytes by buildconf
   unsigned char packed_ip[4];
   int idx = eeprom_offset_index(key);

   if (idx == -1) {
      Log(LOG_WARN, "error in eeprom_get_ipv4: invalid key %s", key);
      return NULL;
   }

   unsigned char *myaddr = rig.eeprom_mmap + eeprom_layout[idx].offset;
   memcpy(packed_ip, myaddr, 4);
   sin->s_addr = *(uint32_t *)packed_ip;
#if	defined(NOISY_NETWORK)
   Log(LOG_DEBUG, "netcfg: %s => %s", key, inet_ntoa(*sin));
#endif
   return sin;
}

void show_pin_info(void) {
   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return;
   }

   char master_pin[PIN_LEN + 1];
   char reset_pin[PIN_LEN + 1];
   memset(master_pin, 0, PIN_LEN + 1);
   memset(reset_pin, 0, PIN_LEN + 1);
   snprintf(master_pin, PIN_LEN, "%s", eeprom_get_str("pin/master"));
   snprintf(reset_pin, PIN_LEN, "%s", eeprom_get_str("pin/reset"));
   Log(LOG_INFO, "*** Master PIN: %s, Factory Reset PIN: %s", master_pin, reset_pin);
}

