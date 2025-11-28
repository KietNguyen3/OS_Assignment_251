/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#if defined(MM64)

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // physical frame number
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}


/*
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Extract page directories */
	*pgd = PAGING64_ADDR_PGD(addr);
	*p4d = PAGING64_ADDR_P4D(addr);
	*pud = PAGING64_ADDR_PUD(addr);
	*pmd = PAGING64_ADDR_PMD(addr);
	*pt = PAGING64_ADDR_PT(addr);

	/* TODO: implement the page direactories mapping */

	return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Shift the address to get page num and perform the mapping*/
	return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                         pgd,p4d,pud,pmd,pt);
}

/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  struct krnl_t *krnl = caller->krnl;
  
#ifdef MM64	
  // Get the address of the PTE
  addr_t pte_addr;
  if (get_pte_address(krnl->mm, krnl->mram, pgn, &pte_addr) != 0) {
    return -1;
  }
  
  // Read current PTE value
  addr_t pte_value = get_32bit_entry(pte_addr, krnl->mram);
  
#else
  addr_t pte_addr = (addr_t)&krnl->mm->pgd[pgn];
  addr_t pte_value = krnl->mm->pgd[pgn];
#endif
	
  // Modify the PTE
  SETBIT(pte_value, PAGING_PTE_PRESENT_MASK);
  SETBIT(pte_value, PAGING_PTE_SWAPPED_MASK);
  SETVAL(pte_value, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(pte_value, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  // Write back to memory
  for (int i = 0; i < 4; i++) {
    BYTE byte_val = (pte_value >> (i * 8)) & 0xFF;
    MEMPHY_write(krnl->mram, pte_addr + i, byte_val);
  }

  return 0;
}
/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  struct krnl_t *krnl = caller->krnl;

#ifdef MM64	
  addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
  get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

  // Level 1: PGD
  addr_t pgd_base = (addr_t)krnl->mm->pgd;
  addr_t pgd_entry = get_32bit_entry(pgd_base + pgd_idx * 4, krnl->mram);
  
  // Allocate P4D table if not present
  if (!(pgd_entry & PAGING_PTE_PRESENT_MASK)) {
    addr_t p4d_fpn;
    if (MEMPHY_get_freefp(krnl->mram, &p4d_fpn) != 0) return -1;
    
    pgd_entry = 0;
    SETBIT(pgd_entry, PAGING_PTE_PRESENT_MASK);
    SETVAL(pgd_entry, p4d_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    
    // Write PGD entry
    for (int i = 0; i < 4; i++) {
      BYTE byte_val = (pgd_entry >> (i * 8)) & 0xFF;
      MEMPHY_write(krnl->mram, pgd_base + pgd_idx * 4 + i, byte_val);
    }
  }
  
  addr_t p4d_base = (pgd_entry & 0x1FFF) * PAGING64_PAGESZ;

  // Level 2: P4D
  addr_t p4d_entry = get_32bit_entry(p4d_base + p4d_idx * 4, krnl->mram);
  
  // Allocate PUD table if not present
  if (!(p4d_entry & PAGING_PTE_PRESENT_MASK)) {
    addr_t pud_fpn;
    if (MEMPHY_get_freefp(krnl->mram, &pud_fpn) != 0) return -1;
    
    p4d_entry = 0;
    SETBIT(p4d_entry, PAGING_PTE_PRESENT_MASK);
    SETVAL(p4d_entry, pud_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    
    // Write P4D entry
    for (int i = 0; i < 4; i++) {
      BYTE byte_val = (p4d_entry >> (i * 8)) & 0xFF;
      MEMPHY_write(krnl->mram, p4d_base + p4d_idx * 4 + i, byte_val);
    }
  }
  
  addr_t pud_base = (p4d_entry & 0x1FFF) * PAGING64_PAGESZ;

  // Level 3: PUD
  addr_t pud_entry = get_32bit_entry(pud_base + pud_idx * 4, krnl->mram);
  
  // Allocate PMD table if not present
  if (!(pud_entry & PAGING_PTE_PRESENT_MASK)) {
    addr_t pmd_fpn;
    if (MEMPHY_get_freefp(krnl->mram, &pmd_fpn) != 0) return -1;
    
    pud_entry = 0;
    SETBIT(pud_entry, PAGING_PTE_PRESENT_MASK);
    SETVAL(pud_entry, pmd_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    
    // Write PUD entry
    for (int i = 0; i < 4; i++) {
      BYTE byte_val = (pud_entry >> (i * 8)) & 0xFF;
      MEMPHY_write(krnl->mram, pud_base + pud_idx * 4 + i, byte_val);
    }
  }
  
  addr_t pmd_base = (pud_entry & 0x1FFF) * PAGING64_PAGESZ;

  // Level 4: PMD
  addr_t pmd_entry = get_32bit_entry(pmd_base + pmd_idx * 4, krnl->mram);
  
  // Allocate PT table if not present
  if (!(pmd_entry & PAGING_PTE_PRESENT_MASK)) {
    addr_t pt_fpn;
    if (MEMPHY_get_freefp(krnl->mram, &pt_fpn) != 0) return -1;
    
    pmd_entry = 0;
    SETBIT(pmd_entry, PAGING_PTE_PRESENT_MASK);
    SETVAL(pmd_entry, pt_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    
    // Write PMD entry
    for (int i = 0; i < 4; i++) {
      BYTE byte_val = (pmd_entry >> (i * 8)) & 0xFF;
      MEMPHY_write(krnl->mram, pmd_base + pmd_idx * 4 + i, byte_val);
    }
  }
  
  addr_t pt_base = (pmd_entry & 0x1FFF) * PAGING64_PAGESZ;

  // Level 5: PT - Get PTE address
  addr_t pte_addr = pt_base + pt_idx * 4;
  
  // Read current PTE value (may be 0 if new)
  addr_t pte_value = get_32bit_entry(pte_addr, krnl->mram);
  
#else
  // 32-bit version - direct access
  addr_t pte_addr = (addr_t)&krnl->mm->pgd[pgn];
  addr_t pte_value = krnl->mm->pgd[pgn];
#endif

  // Modify the PTE value
  SETBIT(pte_value, PAGING_PTE_PRESENT_MASK);
  CLRBIT(pte_value, PAGING_PTE_SWAPPED_MASK);
  SETVAL(pte_value, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  // Write the modified PTE back to physical memory
  for (int i = 0; i < 4; i++) {
    BYTE byte_val = (pte_value >> (i * 8)) & 0xFF;
    MEMPHY_write(krnl->mram, pte_addr + i, byte_val);
  }

  return 0;
}

// int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
// {
//   printf("[DEBUG pte_set_fpn] ============ START ============\n");
//   printf("[DEBUG pte_set_fpn] pgn=%ld, fpn=%ld\n", pgn, fpn);
//   fflush(stdout);
  
//   struct krnl_t *krnl = caller->krnl;
//   printf("[DEBUG pte_set_fpn] krnl=%p, krnl->mm=%p, krnl->mram=%p\n", 
//          (void*)krnl, (void*)krnl->mm, (void*)krnl->mram);
//   fflush(stdout);

// #ifdef MM64	
//   addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
//   get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

//   printf("[DEBUG pte_set_fpn] Extracted indices:\n");
//   printf("  pgd_idx=%ld, p4d_idx=%ld, pud_idx=%ld, pmd_idx=%ld, pt_idx=%ld\n",
//          pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx);
//   fflush(stdout);

//   // Level 1: PGD
//   printf("[DEBUG pte_set_fpn] === Level 1: PGD ===\n");
//   fflush(stdout);
  
//   addr_t pgd_base = (addr_t)krnl->mm->pgd;
//   printf("[DEBUG pte_set_fpn] pgd_base = 0x%lx\n", pgd_base);
//   fflush(stdout);
  
//   printf("[DEBUG pte_set_fpn] Reading PGD entry at address 0x%lx\n", pgd_base + pgd_idx * 4);
//   fflush(stdout);
  
//   addr_t pgd_entry = get_32bit_entry(pgd_base + pgd_idx * 4, krnl->mram);
//   printf("[DEBUG pte_set_fpn] pgd_entry = 0x%x, present = %d\n", 
//          pgd_entry, !!(pgd_entry & PAGING_PTE_PRESENT_MASK));
//   fflush(stdout);
  
//   // Allocate P4D table if not present
//   if (!(pgd_entry & PAGING_PTE_PRESENT_MASK)) {
//     printf("[DEBUG pte_set_fpn] PGD entry not present, allocating P4D table\n");
//     fflush(stdout);
    
//     addr_t p4d_fpn;
//     if (MEMPHY_get_freefp(krnl->mram, &p4d_fpn) != 0) {
//       printf("[ERROR pte_set_fpn] Failed to allocate P4D frame\n");
//       fflush(stdout);
//       return -1;
//     }
    
//     printf("[DEBUG pte_set_fpn] Allocated P4D frame: fpn=%ld\n", p4d_fpn);
//     fflush(stdout);
    
//     pgd_entry = 0;
//     SETBIT(pgd_entry, PAGING_PTE_PRESENT_MASK);
//     SETVAL(pgd_entry, p4d_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    
//     printf("[DEBUG pte_set_fpn] New pgd_entry = 0x%x\n", pgd_entry);
//     fflush(stdout);
    
//     // Write PGD entry
//     printf("[DEBUG pte_set_fpn] Writing PGD entry back to address 0x%lx\n", pgd_base + pgd_idx * 4);
//     fflush(stdout);
    
//     for (int i = 0; i < 4; i++) {
//       BYTE byte_val = (pgd_entry >> (i * 8)) & 0xFF;
//       MEMPHY_write(krnl->mram, pgd_base + pgd_idx * 4 + i, byte_val);
//     }
    
//     printf("[DEBUG pte_set_fpn] PGD entry written successfully\n");
//     fflush(stdout);
//   }
  
//   addr_t p4d_base = (pgd_entry & 0x1FFF) * PAGING64_PAGESZ;
//   printf("[DEBUG pte_set_fpn] p4d_base = 0x%lx\n", p4d_base);
//   fflush(stdout);

//   // Level 2: P4D
//   printf("[DEBUG pte_set_fpn] === Level 2: P4D ===\n");
//   fflush(stdout);
  
//   printf("[DEBUG pte_set_fpn] Reading P4D entry at address 0x%lx\n", p4d_base + p4d_idx * 4);
//   fflush(stdout);
  
//   addr_t p4d_entry = get_32bit_entry(p4d_base + p4d_idx * 4, krnl->mram);
//   printf("[DEBUG pte_set_fpn] p4d_entry = 0x%x, present = %d\n", 
//          p4d_entry, !!(p4d_entry & PAGING_PTE_PRESENT_MASK));
//   fflush(stdout);
  
//   // Allocate PUD table if not present
//   if (!(p4d_entry & PAGING_PTE_PRESENT_MASK)) {
//     printf("[DEBUG pte_set_fpn] P4D entry not present, allocating PUD table\n");
//     fflush(stdout);
    
//     addr_t pud_fpn;
//     if (MEMPHY_get_freefp(krnl->mram, &pud_fpn) != 0) {
//       printf("[ERROR pte_set_fpn] Failed to allocate PUD frame\n");
//       fflush(stdout);
//       return -1;
//     }
    
//     printf("[DEBUG pte_set_fpn] Allocated PUD frame: fpn=%ld\n", pud_fpn);
//     fflush(stdout);
    
//     p4d_entry = 0;
//     SETBIT(p4d_entry, PAGING_PTE_PRESENT_MASK);
//     SETVAL(p4d_entry, pud_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    
//     printf("[DEBUG pte_set_fpn] New p4d_entry = 0x%x\n", p4d_entry);
//     fflush(stdout);
    
//     // Write P4D entry
//     printf("[DEBUG pte_set_fpn] Writing P4D entry back to address 0x%lx\n", p4d_base + p4d_idx * 4);
//     fflush(stdout);
    
//     for (int i = 0; i < 4; i++) {
//       BYTE byte_val = (p4d_entry >> (i * 8)) & 0xFF;
//       MEMPHY_write(krnl->mram, p4d_base + p4d_idx * 4 + i, byte_val);
//     }
    
//     printf("[DEBUG pte_set_fpn] P4D entry written successfully\n");
//     fflush(stdout);
//   }
  
//   addr_t pud_base = (p4d_entry & 0x1FFF) * PAGING64_PAGESZ;
//   printf("[DEBUG pte_set_fpn] pud_base = 0x%lx\n", pud_base);
//   fflush(stdout);

//   // Level 3: PUD
//   printf("[DEBUG pte_set_fpn] === Level 3: PUD ===\n");
//   fflush(stdout);
  
//   printf("[DEBUG pte_set_fpn] Reading PUD entry at address 0x%lx\n", pud_base + pud_idx * 4);
//   fflush(stdout);
  
//   addr_t pud_entry = get_32bit_entry(pud_base + pud_idx * 4, krnl->mram);
//   printf("[DEBUG pte_set_fpn] pud_entry = 0x%x, present = %d\n", 
//          pud_entry, !!(pud_entry & PAGING_PTE_PRESENT_MASK));
//   fflush(stdout);
  
//   // Allocate PMD table if not present
//   if (!(pud_entry & PAGING_PTE_PRESENT_MASK)) {
//     printf("[DEBUG pte_set_fpn] PUD entry not present, allocating PMD table\n");
//     fflush(stdout);
    
//     addr_t pmd_fpn;
//     if (MEMPHY_get_freefp(krnl->mram, &pmd_fpn) != 0) {
//       printf("[ERROR pte_set_fpn] Failed to allocate PMD frame\n");
//       fflush(stdout);
//       return -1;
//     }
    
//     printf("[DEBUG pte_set_fpn] Allocated PMD frame: fpn=%ld\n", pmd_fpn);
//     fflush(stdout);
    
//     pud_entry = 0;
//     SETBIT(pud_entry, PAGING_PTE_PRESENT_MASK);
//     SETVAL(pud_entry, pmd_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    
//     printf("[DEBUG pte_set_fpn] New pud_entry = 0x%x\n", pud_entry);
//     fflush(stdout);
    
//     // Write PUD entry
//     printf("[DEBUG pte_set_fpn] Writing PUD entry back to address 0x%lx\n", pud_base + pud_idx * 4);
//     fflush(stdout);
    
//     for (int i = 0; i < 4; i++) {
//       BYTE byte_val = (pud_entry >> (i * 8)) & 0xFF;
//       MEMPHY_write(krnl->mram, pud_base + pud_idx * 4 + i, byte_val);
//     }
    
//     printf("[DEBUG pte_set_fpn] PUD entry written successfully\n");
//     fflush(stdout);
//   }
  
//   addr_t pmd_base = (pud_entry & 0x1FFF) * PAGING64_PAGESZ;
//   printf("[DEBUG pte_set_fpn] pmd_base = 0x%lx\n", pmd_base);
//   fflush(stdout);

//   // Level 4: PMD
//   printf("[DEBUG pte_set_fpn] === Level 4: PMD ===\n");
//   fflush(stdout);
  
//   printf("[DEBUG pte_set_fpn] Reading PMD entry at address 0x%lx\n", pmd_base + pmd_idx * 4);
//   fflush(stdout);
  
//   addr_t pmd_entry = get_32bit_entry(pmd_base + pmd_idx * 4, krnl->mram);
//   printf("[DEBUG pte_set_fpn] pmd_entry = 0x%x, present = %d\n", 
//          pmd_entry, !!(pmd_entry & PAGING_PTE_PRESENT_MASK));
//   fflush(stdout);
  
//   // Allocate PT table if not present
//   if (!(pmd_entry & PAGING_PTE_PRESENT_MASK)) {
//     printf("[DEBUG pte_set_fpn] PMD entry not present, allocating PT table\n");
//     fflush(stdout);
    
//     addr_t pt_fpn;
//     if (MEMPHY_get_freefp(krnl->mram, &pt_fpn) != 0) {
//       printf("[ERROR pte_set_fpn] Failed to allocate PT frame\n");
//       fflush(stdout);
//       return -1;
//     }
    
//     printf("[DEBUG pte_set_fpn] Allocated PT frame: fpn=%ld\n", pt_fpn);
//     fflush(stdout);
    
//     pmd_entry = 0;
//     SETBIT(pmd_entry, PAGING_PTE_PRESENT_MASK);
//     SETVAL(pmd_entry, pt_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    
//     printf("[DEBUG pte_set_fpn] New pmd_entry = 0x%x\n", pmd_entry);
//     fflush(stdout);
    
//     // Write PMD entry
//     printf("[DEBUG pte_set_fpn] Writing PMD entry back to address 0x%lx\n", pmd_base + pmd_idx * 4);
//     fflush(stdout);
    
//     for (int i = 0; i < 4; i++) {
//       BYTE byte_val = (pmd_entry >> (i * 8)) & 0xFF;
//       MEMPHY_write(krnl->mram, pmd_base + pmd_idx * 4 + i, byte_val);
//     }
    
//     printf("[DEBUG pte_set_fpn] PMD entry written successfully\n");
//     fflush(stdout);
//   }
  
//   addr_t pt_base = (pmd_entry & 0x1FFF) * PAGING64_PAGESZ;
//   printf("[DEBUG pte_set_fpn] pt_base = 0x%lx\n", pt_base);
//   fflush(stdout);

//   // Level 5: PT - Get PTE address
//   printf("[DEBUG pte_set_fpn] === Level 5: PT (Final) ===\n");
//   fflush(stdout);
  
//   addr_t pte_addr = pt_base + pt_idx * 4;
//   printf("[DEBUG pte_set_fpn] pte_addr = 0x%lx\n", pte_addr);
//   fflush(stdout);
  
//   // Read current PTE value (may be 0 if new)
//   printf("[DEBUG pte_set_fpn] Reading current PTE value\n");
//   fflush(stdout);
  
//   addr_t pte_value = get_32bit_entry(pte_addr, krnl->mram);
//   printf("[DEBUG pte_set_fpn] Current pte_value = 0x%x\n", pte_value);
//   fflush(stdout);
  
// #else
//   // 32-bit version - direct access
//   addr_t pte_addr = (addr_t)&krnl->mm->pgd[pgn];
//   addr_t pte_value = krnl->mm->pgd[pgn];
// #endif

//   // Modify the PTE value
//   printf("[DEBUG pte_set_fpn] Modifying PTE: setting FPN=%ld\n", fpn);
//   fflush(stdout);
  
//   SETBIT(pte_value, PAGING_PTE_PRESENT_MASK);
//   CLRBIT(pte_value, PAGING_PTE_SWAPPED_MASK);
//   SETVAL(pte_value, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

//   printf("[DEBUG pte_set_fpn] New pte_value = 0x%x\n", pte_value);
//   fflush(stdout);

//   // Write the modified PTE back to physical memory
//   printf("[DEBUG pte_set_fpn] Writing PTE back to address 0x%lx\n", pte_addr);
//   fflush(stdout);
  
//   for (int i = 0; i < 4; i++) {
//     BYTE byte_val = (pte_value >> (i * 8)) & 0xFF;
//     MEMPHY_write(krnl->mram, pte_addr + i, byte_val);
//   }

//   printf("[DEBUG pte_set_fpn] ============ COMPLETE ============\n\n");
//   fflush(stdout);
  
//   return 0;
// }

/* Get PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  struct krnl_t *krnl = caller->krnl;  // Uncomment this!
  
  uint32_t pte = 0;
  
#ifdef MM64
  addr_t pgd_idx = 0;
  addr_t p4d_idx = 0;
  addr_t pud_idx = 0;
  addr_t pmd_idx = 0;
  addr_t pt_idx = 0;
	
  /* Get indices from page number */
  get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);
  
  /* Use helper function to get PTE address */
  addr_t pte_addr;
  if (get_pte_address(krnl->mm, krnl->mram, pgn, &pte_addr) != 0) {
    return 0;  // Return 0 if page table doesn't exist
  }
  
  /* Read and return the PTE value */
  pte = get_32bit_entry(pte_addr, krnl->mram);
  
#else
  // 32-bit version - direct access
  pte = krnl->mm->pgd[pgn];
#endif
	
  return pte;
}

/* Set PTE page table entry
 * @caller  : caller
 * @pgn     : page number
 * @pte_val : page table entry value to set
 **/
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
  struct krnl_t *krnl = caller->krnl;
  
#ifdef MM64
  /* Get the address of the PTE */
  addr_t pte_addr;
  if (get_pte_address(krnl->mm, krnl->mram, pgn, &pte_addr) != 0) {
    return -1;  // Page table doesn't exist
  }
  
  /* Write the PTE value to physical memory */
  for (int i = 0; i < 4; i++) {
    BYTE byte_val = (pte_val >> (i * 8)) & 0xFF;
    MEMPHY_write(krnl->mram, pte_addr + i, byte_val);
  }
  
#else
  // 32-bit version - direct access
  krnl->mm->pgd[pgn] = pte_val;
#endif
	
  return 0;
}

/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum){
  // struct krnl_t *krnl = caller->krnl;
  
  addr_t start_pgn = addr >> PAGING64_ADDR_PT_SHIFT;
  
  for (int pgit = 0; pgit < pgnum; pgit++) {
    addr_t pgn = start_pgn + pgit;
    
    /* Use pte_set_fpn with FPN=0 for dummy allocation */
    /* This will allocate page tables on-demand */
    if (pte_set_fpn(caller, pgn, 0) != 0) {
      return -1;
    }
  }

  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum,                         // num of mapping page
                    struct framephy_struct *frames,    // list of the mapped frames
                    struct vm_rg_struct *ret_rg)       // return mapped region
{
  struct krnl_t *krnl = caller->krnl;
  struct framephy_struct *fpit = frames;
  int pgit = 0;
  addr_t pgn;

  /* Update the return region with mapped range */
  ret_rg->rg_start = addr;
  ret_rg->rg_end = addr + pgnum * PAGING64_PAGESZ;
  
  /* Calculate starting page number */
  addr_t start_pgn = addr >> PAGING64_ADDR_PT_SHIFT;

  /* Map each frame to corresponding page */
  for (pgit = 0; pgit < pgnum && fpit != NULL; pgit++, fpit = fpit->fp_next) {
    pgn = start_pgn + pgit;
    
    /* Set the PTE with the frame's FPN */
    if (pte_set_fpn(caller, pgn, fpit->fpn) != 0) {
      // If mapping fails, return how many pages were successfully mapped
      ret_rg->rg_end = addr + pgit * PAGING64_PAGESZ;
      return pgit;
    }

#ifdef MM_PAGING
    /* Tracking for page replacement (if needed) */
    enlist_pgn_node(&krnl->mm->fifo_pgn, pgn);
#endif
  }

  /* Return number of pages actually mapped */
  return pgit;
}

// addr_t vmap_page_range(struct pcb_t *caller, addr_t addr, int pgnum,
//                        struct framephy_struct *frames, struct vm_rg_struct *ret_rg)
// {
//   printf("[DEBUG vmap_page_range] START: addr=0x%lx, pgnum=%d\n", addr, pgnum);
//   fflush(stdout);
  
//   struct krnl_t *krnl = caller->krnl;
//   struct framephy_struct *fpit = frames;
//   int pgit = 0;
//   addr_t pgn;

//   ret_rg->rg_start = addr;
//   ret_rg->rg_end = addr + pgnum * PAGING64_PAGESZ;
  
//   addr_t start_pgn = addr >> PAGING64_ADDR_PT_SHIFT;
//   printf("[DEBUG vmap_page_range] start_pgn=%ld\n", start_pgn);
//   fflush(stdout);

//   for (pgit = 0; pgit < pgnum && fpit != NULL; pgit++, fpit = fpit->fp_next) {
//     pgn = start_pgn + pgit;
    
//     printf("[DEBUG vmap_page_range] Mapping page %d: pgn=%ld, fpn=%ld\n", pgit, pgn, fpit->fpn);
//     fflush(stdout);
    
//     if (pte_set_fpn(caller, pgn, fpit->fpn) != 0) {
//       printf("[ERROR vmap_page_range] pte_set_fpn failed\n");
//       fflush(stdout);
//       ret_rg->rg_end = addr + pgit * PAGING64_PAGESZ;
//       return pgit;
//     }

// #ifdef MM_PAGING
//     enlist_pgn_node(&krnl->mm->fifo_pgn, pgn);
// #endif
//   }

//   printf("[DEBUG vmap_page_range] COMPLETE: mapped %d pages\n", pgit);
//   fflush(stdout);
  
//   return pgit;
// }

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  struct krnl_t *krnl = caller->krnl;
  addr_t fpn;
  int pgit;
  struct framephy_struct *newfp_str = NULL;
  struct framephy_struct *prev_fp = NULL;

  /* Initialize the frame list head to NULL */
  *frm_lst = NULL;

  /* Allocate frames one by one */
  for (pgit = 0; pgit < req_pgnum; pgit++){
    /* Try to get a free frame from physical memory */
    if (MEMPHY_get_freefp(krnl->mram, &fpn) == 0){
      /* Create new frame node */
      newfp_str = (struct framephy_struct *)malloc(sizeof(struct framephy_struct));
      if (newfp_str == NULL) {
        // Memory allocation failed
        return pgit;  // Return number of frames allocated so far
      }
      
      newfp_str->fpn = fpn;
      newfp_str->fp_next = NULL;

      /* Build the linked list */
      if (*frm_lst == NULL) {
        /* First frame - set as head */
        *frm_lst = newfp_str;
      } else {
        /* Link to previous frame */
        prev_fp->fp_next = newfp_str;
      }
      
      /* Update previous pointer for next iteration */
      prev_fp = newfp_str;
    }
    else
    {
      /* Failed to get free frame - out of memory */
      // Return number of frames successfully allocated
      return pgit;
    }
  }

  /* Successfully allocated all requested frames */
  return req_pgnum;
}

// addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
// {
//   printf("[DEBUG alloc_pages_range] START: req_pgnum=%d\n", req_pgnum);
//   fflush(stdout);
  
//   struct krnl_t *krnl = caller->krnl;
//   addr_t fpn;
//   int pgit;
//   struct framephy_struct *newfp_str = NULL;
//   struct framephy_struct *prev_fp = NULL;

//   *frm_lst = NULL;

//   for (pgit = 0; pgit < req_pgnum; pgit++){
//     if (MEMPHY_get_freefp(krnl->mram, &fpn) == 0){
//       printf("[DEBUG alloc_pages_range] Allocated frame %d: fpn=%ld\n", pgit, fpn);
//       fflush(stdout);
      
//       newfp_str = (struct framephy_struct *)malloc(sizeof(struct framephy_struct));
//       if (newfp_str == NULL) {
//         printf("[ERROR alloc_pages_range] malloc failed\n");
//         fflush(stdout);
//         return pgit;
//       }
      
//       newfp_str->fpn = fpn;
//       newfp_str->fp_next = NULL;

//       if (*frm_lst == NULL) {
//         *frm_lst = newfp_str;
//       } else {
//         prev_fp->fp_next = newfp_str;
//       }
      
//       prev_fp = newfp_str;
//     }
//     else
//     {
//       printf("[ERROR alloc_pages_range] MEMPHY_get_freefp failed at iteration %d\n", pgit);
//       fflush(stdout);
//       return pgit;
//     }
//   }

//   printf("[DEBUG alloc_pages_range] COMPLETE: allocated %d frames\n", req_pgnum);
//   fflush(stdout);
  
//   return req_pgnum;
// }

//---Helper function---//
void free_frame_list(struct pcb_t *caller, struct framephy_struct *frm_lst)
{
  struct framephy_struct *curr = frm_lst;
  while (curr != NULL) {
    struct framephy_struct *next = curr->fp_next;
    MEMPHY_put_freefp(caller->krnl->mram, curr->fpn);
    free(curr);
    curr = next;
  }
}
/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, 
                  addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  int ret_alloc = 0;

  /* Allocate physical frames */
  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);

  /* Check allocation result */
  if (ret_alloc < 0 && ret_alloc != -3000) {
    return -1;  // Allocation error
  }

  /* Out of memory */
  if (ret_alloc == -3000) {
    return -1;
  }

  /* Check if we got enough frames */
  if (ret_alloc < incpgnum) {
    /* Partial allocation - not enough frames available
     * Could implement swapping here, but for simplicity we fail */
    free_frame_list(caller, frm_lst);
    return -1;
  }

  /* Map the allocated frames to virtual address range */
  int mapped = vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  /* Check if all pages were mapped successfully */
  if (mapped < incpgnum) {
    /* Some pages failed to map - this shouldn't happen if allocation succeeded */
    free_frame_list(caller, frm_lst);
    return -1;
  }

  return 0;
}

// addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, 
//                   addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
// {
//   printf("[DEBUG vm_map_ram] START: astart=0x%lx, aend=0x%lx, mapstart=0x%lx, incpgnum=%d\n",
//          astart, aend, mapstart, incpgnum);
//   fflush(stdout);
  
//   struct framephy_struct *frm_lst = NULL;
//   int ret_alloc = 0;

//   ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);
  
//   printf("[DEBUG vm_map_ram] alloc_pages_range returned %d\n", ret_alloc);
//   fflush(stdout);

//   if (ret_alloc < 0 && ret_alloc != -3000) {
//     printf("[ERROR vm_map_ram] alloc_pages_range failed\n");
//     fflush(stdout);
//     return -1;
//   }

//   if (ret_alloc == -3000) {
//     printf("[ERROR vm_map_ram] Out of memory\n");
//     fflush(stdout);
//     return -1;
//   }

//   if (ret_alloc < incpgnum) {
//     printf("[ERROR vm_map_ram] Partial allocation: got %d, needed %d\n", ret_alloc, incpgnum);
//     fflush(stdout);
//     free_frame_list(caller, frm_lst);
//     return -1;
//   }

//   int mapped = vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);
  
//   printf("[DEBUG vm_map_ram] vmap_page_range returned %d\n", mapped);
//   fflush(stdout);

//   if (mapped < incpgnum) {
//     printf("[ERROR vm_map_ram] Partial mapping: mapped %d, needed %d\n", mapped, incpgnum);
//     fflush(stdout);
//     free_frame_list(caller, frm_lst);
//     return -1;
//   }

//   printf("[DEBUG vm_map_ram] COMPLETE\n");
//   fflush(stdout);
  
//   return 0;
// }

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct krnl_t *krnl = caller->krnl;
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

#ifdef MM64
  /* Initialize page table directory for 64-bit */
  addr_t pgd_fpn;
  
  // Allocate physical frame for PGD
  if (MEMPHY_get_freefp(krnl->mram, &pgd_fpn) != 0) {
    free(vma0);  // Clean up allocated vma0
    return -1;  // Out of memory
  }
  mm->pgd = (addr_t *)(pgd_fpn * PAGING64_PAGESZ);
  
  // Zero out the PGD frame (clear any garbage data)
  addr_t pgd_base = (addr_t)mm->pgd;
  for (int i = 0; i < PAGING64_PAGESZ; i++) {
    MEMPHY_write(krnl->mram, pgd_base + i, 0);
  }
  
  // Lazy allocation for other levels
  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt = NULL;
  
#else
  /* 32-bit version */
  addr_t pgd_fpn;
  if (MEMPHY_get_freefp(krnl->mram, &pgd_fpn) != 0) {
    free(vma0);
    return -1;
  }
  mm->pgd = (uint32_t *)(pgd_fpn * PAGING_PAGESZ);
  
  // Zero out the PGD
  addr_t pgd_base = (addr_t)mm->pgd;
  for (int i = 0; i < PAGING_PAGESZ; i++) {
    MEMPHY_write(krnl->mram, pgd_base + i, 0);
  }
#endif

  /* Initialize FIFO page list for page replacement */
  mm->fifo_pgn = NULL;

  /* Initialize symbol region table */
  memset(mm->symrgtbl, 0, sizeof(struct vm_rg_struct) * PAGING_MAX_SYMTBL_SZ);

  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  /* Update VMA0 next - initially NULL (only one VMA) */
  vma0->vm_next = NULL;

  /* Point vma owner backward */
  vma0->vm_mm = mm; 

  /* Update mmap - point to the first VMA */
  mm->mmap = vma0;

  return 0;
}

// int init_mm(struct mm_struct *mm, struct pcb_t *caller)
// {
//   printf("[DEBUG 1] init_mm: START, mm=%p, caller=%p\n", (void*)mm, (void*)caller);
//   fflush(stdout);
  
//   if (caller == NULL) {
//     printf("[ERROR] caller is NULL\n");
//     exit(1);
//   }
  
//   printf("[DEBUG 2] init_mm: Getting krnl from caller\n");
//   fflush(stdout);
  
//   struct krnl_t *krnl = caller->krnl;
//   printf("[DEBUG 3] init_mm: krnl = %p\n", (void*)krnl);
//   fflush(stdout);
  
//   if (krnl == NULL) {
//     printf("[ERROR] krnl is NULL\n");
//     exit(1);
//   }
  
//   printf("[DEBUG 4] init_mm: krnl->mram = %p\n", (void*)krnl->mram);
//   fflush(stdout);
  
//   if (krnl->mram == NULL) {
//     printf("[ERROR] krnl->mram is NULL\n");
//     exit(1);
//   }
  
//   printf("[DEBUG 5] init_mm: Allocating vma0\n");
//   fflush(stdout);
  
//   struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
//   printf("[DEBUG 6] init_mm: vma0 = %p\n", (void*)vma0);
//   fflush(stdout);

// #ifdef MM64
//   printf("[DEBUG 7] init_mm: MM64 branch, allocating PGD\n");
//   fflush(stdout);
  
//   addr_t pgd_fpn;
  
//   printf("[DEBUG 8] init_mm: Calling MEMPHY_get_freefp\n");
//   fflush(stdout);
  
//   if (MEMPHY_get_freefp(krnl->mram, &pgd_fpn) != 0) {
//     printf("[ERROR] MEMPHY_get_freefp failed\n");
//     free(vma0);
//     return -1;
//   }
  
//   printf("[DEBUG 9] init_mm: Got pgd_fpn = %llu\n", pgd_fpn);
//   fflush(stdout);
  
//   mm->pgd = (addr_t *)(pgd_fpn * PAGING64_PAGESZ);
//   printf("[DEBUG 10] init_mm: mm->pgd = %p\n", (void*)mm->pgd);
//   fflush(stdout);
  
//   // COMMENT OUT the zeroing loop for now:
//   /*
//   addr_t pgd_base = (addr_t)mm->pgd;
//   for (int i = 0; i < PAGING64_PAGESZ; i++) {
//     MEMPHY_write(krnl->mram, pgd_base + i, 0);
//   }
//   */
//   printf("[DEBUG 11] init_mm: Skipped zeroing (commented out)\n");
//   fflush(stdout);
  
//   mm->p4d = NULL;
//   mm->pud = NULL;
//   mm->pmd = NULL;
//   mm->pt = NULL;
  
// #else
//   // 32-bit version
//   // ...
// #endif

//   printf("[DEBUG 12] init_mm: Initializing FIFO\n");
//   fflush(stdout);
  
//   mm->fifo_pgn = NULL;

//   printf("[DEBUG 13] init_mm: Calling memset for symrgtbl\n");
//   fflush(stdout);
  
//   memset(mm->symrgtbl, 0, sizeof(struct vm_rg_struct) * PAGING_MAX_SYMTBL_SZ);

//   printf("[DEBUG 14] init_mm: Setting up vma0\n");
//   fflush(stdout);
  
//   vma0->vm_id = 0;
//   vma0->vm_start = 0;
//   vma0->vm_end = vma0->vm_start;
//   vma0->sbrk = vma0->vm_start;
  
//   printf("[DEBUG 15] init_mm: Calling init_vm_rg\n");
//   fflush(stdout);
  
//   struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  
//   printf("[DEBUG 16] init_mm: Calling enlist_vm_rg_node\n");
//   fflush(stdout);
  
//   enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

//   vma0->vm_next = NULL;
//   vma0->vm_mm = mm; 
//   mm->mmap = vma0;

//   printf("[DEBUG 17] init_mm: COMPLETE\n");
//   fflush(stdout);
  
//   return 0;
// }

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("\n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  struct krnl_t *krnl = caller->krnl;
  addr_t pgd, p4d, pud, pmd, pt;

  get_pd_from_address(start, &pgd, &p4d, &pud, &pmd, &pt);

  // Walk and read each level
  addr_t pgd_entry = get_32bit_entry((addr_t)krnl->mm->pgd + pgd * 4, krnl->mram);
  addr_t p4d_entry = get_32bit_entry(((pgd_entry & 0x1FFF) * PAGING64_PAGESZ) + p4d * 4, krnl->mram);
  addr_t pud_entry = get_32bit_entry(((p4d_entry & 0x1FFF) * PAGING64_PAGESZ) + pud * 4, krnl->mram);
  addr_t pmd_entry = get_32bit_entry(((pud_entry & 0x1FFF) * PAGING64_PAGESZ) + pmd * 4, krnl->mram);

  printf("print_pgtbl:  PDG=%lx%x P4g=%lx%x PUD=%lx%x PMD=%lx%x\n",
         pgd, (unsigned int)pgd_entry, p4d, (unsigned int)p4d_entry,
         pud, (unsigned int)pud_entry, pmd, (unsigned int)pmd_entry);

  return 0;
}

addr_t get_32bit_entry(addr_t base_address, struct memphy_struct* mp){
  addr_t entry = 0;
  for(int i = 0; i < 4; i++){
    BYTE byte_val;
    if((MEMPHY_read(mp, base_address + i, &byte_val)) != 0) return -1;
    entry |= ((addr_t)byte_val) << (i*8);
  }
  return entry;
}
// addr_t get_32bit_entry(addr_t base_address, struct memphy_struct* mp){
//   printf("[DEBUG get_32bit_entry] base_address = 0x%lx, mp = %p\n", base_address, (void*)mp);
//   fflush(stdout);
  
//   addr_t entry = 0;
//   for(int i = 0; i < 4; i++){
//     BYTE byte_val;
//     if((MEMPHY_read(mp, base_address + i, &byte_val)) != 0) {
//       printf("[ERROR get_32bit_entry] MEMPHY_read failed at address 0x%lx\n", base_address + i);
//       fflush(stdout);
//       return -1;
//     }
//     entry |= ((addr_t)byte_val) << (i*8);
//   }
//   printf("[DEBUG get_32bit_entry] Read entry = 0x%lx\n", entry);
//   fflush(stdout);
//   return entry;
// }

int translate_address(struct mm_struct* mm, struct memphy_struct* mp, addr_t vaddr, addr_t* paddr){
  addr_t pgd, p4d, pud, pmd, pt;
  get_pd_from_address(vaddr, &pgd, &p4d, &pud, &pmd, &pt);

  // Level 1: PGD
  addr_t pgd_base = (addr_t)mm->pgd;
  addr_t pgd_entry = get_32bit_entry(pgd_base + pgd * 4, mp);
  if(!(pgd_entry & PAGING_PTE_PRESENT_MASK)) return -1;

  addr_t p4d_base = (pgd_entry & 0x1FFF) * PAGING64_PAGESZ;  // Convert FPN to address

  // Level 2: P4D
  addr_t p4d_entry = get_32bit_entry(p4d_base + p4d * 4, mp);
  if(!(p4d_entry & PAGING_PTE_PRESENT_MASK)) return -1;

  addr_t pud_base = (p4d_entry & 0x1FFF) * PAGING64_PAGESZ;  // Convert FPN to address

  // Level 3: PUD
  addr_t pud_entry = get_32bit_entry(pud_base + pud * 4, mp);
  if(!(pud_entry & PAGING_PTE_PRESENT_MASK)) return -1;

  addr_t pmd_base = (pud_entry & 0x1FFF) * PAGING64_PAGESZ;  // Convert FPN to address

  // Level 4: PMD
  addr_t pmd_entry = get_32bit_entry(pmd_base + pmd * 4, mp);
  if(!(pmd_entry & PAGING_PTE_PRESENT_MASK)) return -1;

  addr_t pt_base = (pmd_entry & 0x1FFF) * PAGING64_PAGESZ;  // Convert FPN to address

  // Level 5: PT (final page table)
  addr_t pt_entry = get_32bit_entry(pt_base + pt * 4, mp);
  if(!(pt_entry & PAGING_PTE_PRESENT_MASK)) return -1;

  addr_t fpn = pt_entry & 0x1FFF;  // Extract FPN (bits 0-12)
  addr_t page_base = fpn * PAGING64_PAGESZ;  // Convert to physical address

  // Add offset
  addr_t offset = vaddr & 0xFFF;
  *paddr = page_base + offset;

  return 0;
}

int get_pte_address(struct mm_struct* mm, struct memphy_struct* mp, addr_t pgn, addr_t* pte_addr){
  addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
  get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

  // Level 1: PGD
  addr_t pgd_base = (addr_t)mm->pgd;
  addr_t pgd_entry = get_32bit_entry(pgd_base + pgd_idx * 4, mp);
  if (!(pgd_entry & PAGING_PTE_PRESENT_MASK)) return -1;
  
  addr_t p4d_base = (pgd_entry & 0x1FFF) * PAGING64_PAGESZ;

  // Level 2: P4D
  addr_t p4d_entry = get_32bit_entry(p4d_base + p4d_idx * 4, mp);
  if (!(p4d_entry & PAGING_PTE_PRESENT_MASK)) return -1;
  
  addr_t pud_base = (p4d_entry & 0x1FFF) * PAGING64_PAGESZ;

  // Level 3: PUD
  addr_t pud_entry = get_32bit_entry(pud_base + pud_idx * 4, mp);
  if (!(pud_entry & PAGING_PTE_PRESENT_MASK)) return -1;
  
  addr_t pmd_base = (pud_entry & 0x1FFF) * PAGING64_PAGESZ;

  // Level 4: PMD
  addr_t pmd_entry = get_32bit_entry(pmd_base + pmd_idx * 4, mp);
  if (!(pmd_entry & PAGING_PTE_PRESENT_MASK)) return -1;
  
  addr_t pt_base = (pmd_entry & 0x1FFF) * PAGING64_PAGESZ;

  // Level 5: PT - Return the ADDRESS of the PTE (not the content!)
  *pte_addr = pt_base + pt_idx * 4;
  
  return 0;
}

#endif  //def MM64