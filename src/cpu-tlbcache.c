/*
 * Copyright (C) 2024 pdnguyen of the HCMC University of Technology
 */
/*
 * Source Code License Grant: Authors hereby grants to Licensee
 * a personal to use and modify the Licensed Source Code for
 * the sole purpose of studying during attending the course CO2018.
 */
/*
 * Memory physical based TLB Cache
 * TLB cache module tlb/tlbcache.c
 *
 * TLB cache is physically memory phy
 * supports random access
 * and runs at high speed
 */

#include "mm.h"
#ifdef CPU_TLB
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

static pthread_mutex_t tlb_lock = PTHREAD_MUTEX_INITIALIZER;

#define init_tlbcache(mp, sz, ...) init_memphy(mp, sz, (1, ##__VA_ARGS__))

// BEGIN Student support code
// Helper functions to encode and decode integers to/from bytes
void encode_int(BYTE *dest, uint32_t value)
{
   memcpy(dest, &value, sizeof(uint32_t));
}

uint32_t decode_int(const BYTE *src)
{
   uint32_t value;
   memcpy(&value, src, sizeof(uint32_t));
   return value;
}

// Helper function to calculate the index for a given set based on the page number
int calculate_set_index(int pgnum)
{
   return pgnum % NUM_SETS; // Set index based on page number
}

// END Student support code

/*
 *  tlb_cache_read read TLB cache device
 *  @mp: memphy struct
 *  @pid: process id
 *  @pgnum: page number
 *  @value: obtained value
 */
// Implement the TLB cache read function using a set-associative approach
int tlb_cache_read(struct memphy_struct *mp, int pid, int pgnum, BYTE *value)
{
   pthread_mutex_lock(&tlb_lock);

   if (mp == NULL || value == NULL || mp->storage == NULL)
   {
      pthread_mutex_unlock(&tlb_lock);
      return -1; // Error: Invalid input
   }

   int set_index = calculate_set_index(pgnum); // Find the set index for the pgnum
   int set_base_addr = set_index * ENTRIES_PER_SET * ENTRY_SIZE;

   // Search within the set for the matching TLB entry
   BYTE entry[ENTRY_SIZE];
   for (int i = 0; i < ENTRIES_PER_SET; ++i)
   {
      int entry_addr = set_base_addr + i * ENTRY_SIZE;

      TLBMEMPHY_read(mp, entry_addr, entry, ENTRY_SIZE);
      uint32_t entry_pid = decode_int(entry + PID_OFFSET);
      uint32_t entry_vpn = decode_int(entry + VPN_OFFSET);
      uint32_t entry_pfn = decode_int(entry + PFN_OFFSET);
      BYTE valid = entry[VALID_OFFSET];

      if (valid && entry_pid == pid && entry_vpn == pgnum)
      {
         *value = entry_pfn; // Found the matching frame number

         pthread_mutex_unlock(&tlb_lock);
         return 0; // TLB hit
      }
   }

   pthread_mutex_unlock(&tlb_lock);
   return -1; // TLB miss, entry not found
}

/*
 *  tlb_cache_write write TLB cache device
 *  @mp: memphy struct
 *  @pid: process id
 *  @pgnum: page number
 *  @value: obtained value
 */
int tlb_cache_write(struct memphy_struct *mp, int pid, int pgnum, BYTE value)
{
   pthread_mutex_lock(&tlb_lock);

   if (mp == NULL || mp->storage == NULL)
   {
      pthread_mutex_unlock(&tlb_lock);
      return -1; // Error: Invalid input
   }

   int set_index = calculate_set_index(pgnum);
   int set_base_addr = set_index * ENTRIES_PER_SET * ENTRY_SIZE;
   int free_entry_addr = -1; // To keep track of the first free slot

   BYTE entry[ENTRY_SIZE];
   for (int i = 0; i < ENTRIES_PER_SET; ++i)
   {
      int entry_addr = set_base_addr + i * ENTRY_SIZE;
      TLBMEMPHY_read(mp, entry_addr, entry, ENTRY_SIZE);
      uint32_t entry_pid = decode_int(entry + PID_OFFSET);
      uint32_t entry_vpn = decode_int(entry + VPN_OFFSET);
      BYTE valid = entry[VALID_OFFSET];

      if (valid && entry_pid == pid && entry_vpn == pgnum)
      {
         // Matching entry found, update it
         encode_int(entry + PFN_OFFSET, value);
         entry[VALID_OFFSET] = 1;
         TLBMEMPHY_write(mp, entry_addr, entry, ENTRY_SIZE);

         pthread_mutex_unlock(&tlb_lock);
         return 0;
      }
      else if (!valid && free_entry_addr == -1)
      {
         // Remember the first free slot
         free_entry_addr = entry_addr;
      }
   }

   // If no matching entry was found, use the first free slot if available
   if (free_entry_addr != -1)
   {
      encode_int(entry + PID_OFFSET, pid);
      encode_int(entry + VPN_OFFSET, pgnum);
      encode_int(entry + PFN_OFFSET, value);
      entry[VALID_OFFSET] = 1;
      TLBMEMPHY_write(mp, free_entry_addr, entry, ENTRY_SIZE);

      pthread_mutex_unlock(&tlb_lock);
      return 0;
   }

   pthread_mutex_unlock(&tlb_lock);
   return -1; // No space to insert new entry
}

/*
 *  TLBMEMPHY_read natively supports MEMPHY device interfaces
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int TLBMEMPHY_read(struct memphy_struct *mp, int addr, BYTE *value, uint32_t size)
{
   if (mp == NULL || mp->storage == NULL)
      return -1;

   // Ensure the address and size are valid
   if (addr + size > mp->maxsz)
      return -1;

   memcpy(value, &mp->storage[addr], size);

   return 0;
}

/*
 *  TLBMEMPHY_write natively supports MEMPHY device interfaces
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int TLBMEMPHY_write(struct memphy_struct *mp, int addr, const BYTE *data, uint32_t size)
{
   if (mp == NULL || mp->storage == NULL)
      return -1;

   // Ensure the address and size are valid
   if (addr + size > mp->maxsz)
      return -1;

   memcpy(&mp->storage[addr], data, size);

   return 0;
}

/*
 *  TLBMEMPHY_format natively supports MEMPHY device interfaces
 *  @mp: memphy struct
 */

int TLBMEMPHY_dump(struct memphy_struct *mp)
{
   if (mp == NULL || mp->storage == NULL)
   {
      return -1; // Ensure the memory structure and its storage are valid
   }

   printf("\t\tDump of meaningful memory contents:\n\t\t");
   int has_printed = 0; // To handle formatting
   for (int i = 0; i < mp->maxsz; i++)
   {
      if (mp->storage[i] != 0)
      {                                   // Assume non-zero bytes are used
         printf("%08X ", mp->storage[i]); // Print each byte in hexadecimal
         has_printed = 1;
      }
      if ((i + 1) % 16 == 0 && has_printed)
      {
         printf("\n\t\t");
         has_printed = 0; // Reset after printing a line
      }
   }
   if (has_printed)
      printf("\n");

   return 0; // Success
}

/*
 *  Init TLBMEMPHY struct
 */
int init_tlbmemphy(struct memphy_struct *mp, int max_size)
{
   mp->storage = (BYTE *)malloc(max_size * sizeof(BYTE));
   mp->maxsz = max_size;

   mp->rdmflg = 1;

   return 0;
}

#endif
