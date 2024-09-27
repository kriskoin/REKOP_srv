//******************************************************************
//
//      Misc. CRC tools
// 
//
//******************************************************************

#include <stdio.h>
#include "pplib.h"

// This code is based on ISO 3309 and ITU-T V.42

/* Table of CRCs of all 8-bit messages. */
static unsigned long crc_table[256];

/* Flag: has the table been computed? Initially false. */
static int crc_table_computed = 0;

/* Make the table for a fast CRC. */
static void make_crc_table(void)
{
  unsigned long c;
  int n, k;

  for (n = 0; n < 256; n++) {
    c = (unsigned long) n;
    for (k = 0; k < 8; k++) {
      if (c & 1) {
        c = 0xedb88320L ^ (c >> 1);
      } else {
        c = c >> 1;
      }
    }
    crc_table[n] = c;
  }
  crc_table_computed = 1;
}

/*
   Update a running crc with the bytes buf[0..len-1] and return
 the updated crc. The crc should be initialized to zero. Pre- and
 post-conditioning (one's complement) is performed within this
 function so it shouldn't be done by the caller. Usage example:

   unsigned long crc = 0L;

   while (read_buffer(buffer, length) != EOF) {
     crc = update_crc(crc, buffer, length);
   }
   if (crc != original_crc) error();
*/
static unsigned long update_crc(unsigned long crc,
                unsigned char *buf, int len)
{
  unsigned long c = crc ^ 0xffffffffL;
  int n;

  if (!crc_table_computed)
    make_crc_table();
  for (n = 0; n < len; n++) {
    c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
  }
  return c ^ 0xffffffffL;
}

#if 0	//19990621MB no longer needed
/****************************************************************/
/*  Mon October 31/94 - MB										*/
/*																*/
/*	Calculate the CRC on a range of memory						*/
/*																*/
/****************************************************************/

WORD16 CalcCRC16(void *vptr, int len)
{
	WORD16 crc;
	byte *p;

		crc = CRC16INITVALUE;
		p = (byte *)vptr;
		while (len-- > 0) {
			crc = UpdateCRC16(*p, crc);
			p++;
		}
		return crc;
}
#endif

//****************************************************************
// 
//
//	Calculate a 32-bit CRC.
//
WORD32 CalcCRC32(void *ptr, int len)
{
	return update_crc(0L, (unsigned char *)ptr, len);
}


//*********************************************************
// https://github.com/kriskoin//
// Calculate crc for a file
//
WORD32 CalcCRC32forFile(char *fname)
{
	WORD32 crc = 0;
	FILE *fd = fopen(fname, "rb");
	if (fd) {
		#define BUFFER_SIZE	2048
		size_t bytes_read;
		do {
			byte buffer[BUFFER_SIZE];
			bytes_read = fread(buffer, 1, BUFFER_SIZE, fd);
			if (bytes_read) {
				crc = update_crc(crc, buffer, bytes_read);
			}
		} while(bytes_read);
		fclose(fd);
	} else {
		Error(ERR_ERROR, "%s(%d) Cannot open file '%s' to calculate CRC.", _FL, fname);
	}
	return crc;
}



//************* Check Sum tools ***********
//  Modified by Allen Ko 9/25/2001
//  To change the email validation code from CRC to checksum
//  

/*-------------------------------------------------------------------------------
 * DESCRIPTION:
 *     This method is used to calculate the check_sum of the input string
 *
 * INPUTS:
 *     vptr  -  string used to calculate
 *     len   -  the length of the string
 *
 * RETURN VALUE:
 *     WORD32
 *
 * ADDED by Allen Ko 9/25/2001
 * 
 */ 
 
WORD32 CalcChecksum(char *vptr, int len)

{
    WORD32   check_sum;
    int i;

    check_sum = 0;
    
    for (i = 0; i < len ; i++)
    {
        check_sum = check_sum + (WORD32)(*vptr) ;
        vptr++;
    }

    // only the least significant bytes will be used
    check_sum &= 0xFFFF;

	return ((WORD32)check_sum);
}

