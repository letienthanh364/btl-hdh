/*
 * Copyright (C) 2024 pdnguyen of the HCMC University of Technology
 */
/*
 * Source Code License Grant: Authors hereby grants to Licensee
 * a personal to use and modify the Licensed Source Code for
 * the sole purpose of studying during attending the course CO2018.
 */
/*
 * CPU TLB
 * TLB module cpu/cpu-tlb.c
 */

#include "mm.h"
#ifdef CPU_TLB
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t tlb_lock = PTHREAD_MUTEX_INITIALIZER;

int handle_page_fault(struct pcb_t *proc, uint32_t virtual_address)
{
  uint32_t page_index = PAGING_PGN(virtual_address);
  uint32_t *pte = &proc->mm->pgd[page_index];

  if (*pte & PAGING_PTE_PRESENT_MASK)
  {
    // Page is present but not in TLB
    tlb_cache_write(proc->tlb, proc->pid, page_index, PAGING_FPN(*pte));
  }
  else
  {
    // Page is not present, allocate memory
    struct framephy_struct *frame;
    if (alloc_pages_range(proc, 1, &frame) != 0 || frame == NULL)
    {
      printf("Failed to allocate memory frame.\n");

      return -1;
    }

    // Update page table
    init_pte(pte, 1, frame->fpn, 1, 0, 0, 0);
    // Update TLB
    tlb_cache_write(proc->tlb, proc->pid, page_index, frame->fpn);
    free(frame);
  }

  return 0;
}

int tlb_change_all_page_tables_of(struct pcb_t *proc, struct memphy_struct *mp)
{
  /* TODO update all page table directory info
   *      in flush or wipe TLB (if needed)
   */
  return 0;
}

int tlb_flush_tlb_of(struct pcb_t *proc, struct memphy_struct *mp)
{
  /* TODO flush tlb cached*/
  return 0;
}

/* tlballoc - CPU TLB-based allocate a region memory
 *  @proc:  Process executing the instruction
 *  @size: allocated size
 *  @reg_index: memory region ID (used to identify variable in symbole table)
 */
int tlballoc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
  pthread_mutex_lock(&tlb_lock);
  if (proc == NULL || proc->mm == NULL)
  {
    pthread_mutex_unlock(&tlb_lock);
    return -1; // Invalid process or memory management structure
  }

  int addr;
  // Allocate virtual memory region
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val != 0)
  {
    pthread_mutex_unlock(&tlb_lock);
    return val; // Memory allocation failed, propagate the error
  }

  // No need to manually update TLB here as vmap_page_range already updates TLB for each mapped page

  TLBMEMPHY_dump(proc->tlb); // Optionally dump the TLB to verify its state after allocation

  pthread_mutex_unlock(&tlb_lock);
  return 0; // Success
}

/*  pgfree - CPU TLB-based free a region memory
 *  @proc: Process executing the instruction
 *  @size: allocated size
 *  @reg_index: memory region ID (used to identify variable in symbole table)
 */
int tlbfree_data(struct pcb_t *proc, uint32_t reg_index)
{
  pthread_mutex_lock(&tlb_lock);

  if (proc == NULL || proc->mm == NULL || reg_index >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&tlb_lock);
    return -1; // Invalid input check
  }

  // Get region details
  struct vm_rg_struct *region = &proc->mm->symrgtbl[reg_index];
  if (region->rg_start == 0 && region->rg_end == 0)
  {
    pthread_mutex_unlock(&tlb_lock);
    return -1; // Check for uninitialized or already freed region
  }

  // Invalidate TLB entries for all pages in the region before freeing the region
  uint32_t start_page = PAGING_PGN(region->rg_start);
  uint32_t end_page = PAGING_PGN((region->rg_end - 1)); // inclusive end
  BYTE dummy_frame_number = 0x00;                       // Use a dummy frame number to denote an invalid entry

  for (uint32_t page_number = start_page; page_number <= end_page; ++page_number)
  {
    // Invalidate the entry
    tlb_cache_write(proc->tlb, proc->pid, page_number, dummy_frame_number);
  }

  // Free the memory region after TLB has been updated
  int val = __free(proc, 0, reg_index);

  pthread_mutex_unlock(&tlb_lock);
  return val;
}

/* tlbread - CPU TLB-based read a region memory
 * @proc: Process executing the instruction
 * @source: index of source register
 * @offset: source address = [source] + [offset]
 * @destination: destination storage
 */
int tlbread(struct pcb_t *proc, uint32_t source, uint32_t offset, uint32_t *destination)
{
  pthread_mutex_lock(&tlb_lock);
  if (!proc || !proc->tlb)
  {
    printf("Argurments are invalid\n");

    pthread_mutex_unlock(&tlb_lock);
    return -1;
  }
  BYTE data;
  BYTE frmnum = -1; // Initialize to an invalid frame number

  // Calculate the virtual address using the destination register and offset
  uint32_t virtual_address = proc->regs[*destination] + offset;
  uint32_t page_number = PAGING_PGN(virtual_address);

  // Try to retrieve the frame number from the TLB
  if (tlb_cache_read(proc->tlb, proc->pid, page_number, &frmnum) != 0)
  {
    printf("TLB miss at region=%d offset=%d\n", source, offset);
    // TLB miss, handle the page fault
    if (handle_page_fault(proc, virtual_address) != 0)
    {
      pthread_mutex_unlock(&tlb_lock);
      return -1; // Handle error if page fault handling fails
    }

    printf("Handled page fault.\n");
    // After handling the page fault, attempt to read the frame number again
    if (tlb_cache_read(proc->tlb, proc->pid, page_number, (BYTE *)&frmnum) != 0)
    {
      printf("CANNOT READ\n");

      pthread_mutex_unlock(&tlb_lock);
      return -1; // Still failing after handling page fault
    }
  }

#ifdef IODUMP
  if (frmnum >= 0)
    printf("TLB hit at read region=%d offset=%d\n",
           source, offset);
  else
    printf("TLB miss at read region=%d offset=%d\n",
           source, offset);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
  // TLBMEMPHY_dump(proc->tlb);
#endif
  MEMPHY_dump(proc->mram);
#endif

  int val = -1; // Initalize an invalid value
  // If the frame number is valid, perform the memory read
  if (frmnum >= 0)
    val = __read(proc, 0, source, offset, &data);
  if (val != 0)
  {
    pthread_mutex_unlock(&tlb_lock);
    return val; // Error reading memory
  }

  // Store the read data to the destination register
  *destination = (uint32_t)data;

  pthread_mutex_unlock(&tlb_lock);
  return val;
}

/* tlbwrite - CPU TLB-based write a region memory
 * @proc: Process executing the instruction
 * @data: data to be wrttien into memory
 * @destination: index of destination register
 * @offset: destination address = [destination] + [offset]
 */
int tlbwrite(struct pcb_t *proc, BYTE data, uint32_t destination, uint32_t offset)
{
  pthread_mutex_lock(&tlb_lock);

  if (!proc || !proc->tlb)
  {
    printf("Argurments are invalid\n");

    pthread_mutex_unlock(&tlb_lock);
    return -1;
  }
  int val = -1;
  BYTE frmnum = -1; // Initialize frame number as invalid

  // Calculate the virtual address using the destination register and offset
  uint32_t virtual_address = proc->regs[destination] + offset;
  uint32_t page_number = PAGING_PGN(virtual_address);

  // Try to retrieve the frame number from the TLB
  if (tlb_cache_read(proc->tlb, proc->pid, page_number, &frmnum) != 0)
  {
    printf("TLB miss at write region=%d offset=%d value=%d\n",
           destination, offset, data);
    // TLB miss, handle the page fault
    if (handle_page_fault(proc, virtual_address) != 0)
    {
      pthread_mutex_unlock(&tlb_lock);
      return -1; // Handle error if page fault handling fails
    }

    printf("Handled page fault.\n");
    // After handling the page fault, attempt to read the frame number again
    if (tlb_cache_read(proc->tlb, proc->pid, page_number, (BYTE *)&frmnum) != 0)
    {
      printf("CANNOT READ\n");

      pthread_mutex_unlock(&tlb_lock);
      return -1; // Still failing after handling page fault
    }
  }

#ifdef IODUMP
  if (frmnum >= 0)
    printf("TLB hit at write region=%d offset=%d value=%d\n",
           destination, offset, data);
  else
    printf("TLB miss at write region=%d offset=%d value=%d\n",
           destination, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
  // TLBMEMPHY_dump(proc->tlb);
#endif
  MEMPHY_dump(proc->mram);
#endif

  // If the frame number is valid, perform the memory write
  if (frmnum >= 0)
    val = __write(proc, 0, destination, offset, data);

  if (val == 0)
  {
    // Only update TLB on successful write
    tlb_cache_write(proc->tlb, proc->pid, page_number, frmnum);
  }

  pthread_mutex_unlock(&tlb_lock);
  return val;
}

#endif
