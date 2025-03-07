/*
**  CRC.H - header file for SNIPPETS CRC and checksum functions
*/
// From https://github.com/panzi/CRC-and-checksum-functions

#ifndef CRC__H
#define CRC__H

#include <stdlib.h>           /* For size_t                           */
#include <stdint.h>           /* For uint8_t, uint16_t, uint32_t      */
#include <stdbool.h>          /* For bool, true, false                */

/*
**  File: CRC_32.C
*/

#define UPDC32(octet,crc) (crc_32_tab[((crc)\
     ^ ((uint8_t)octet)) & 0xff] ^ ((crc) >> 8))

uint32_t updateCRC32(unsigned char ch, uint32_t crc);
bool crc32file(char *name, uint32_t *crc, long *charcnt);
uint32_t crc32buf(char *buf, size_t len);

#endif /* CRC__H */
