/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#ifndef OSMM_H
#define OSMM_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Basic paging config                                                */
/* ------------------------------------------------------------------ */

#ifndef MM_PAGING
#define MM_PAGING
#endif

#ifndef PAGING_MAX_MMSWP
#define PAGING_MAX_MMSWP 4   /* max number of supported swapped space */
#endif

#ifndef PAGING_MAX_SYMTBL_SZ
#define PAGING_MAX_SYMTBL_SZ 30
#endif

/*
 * Address type:
 *  - We DO NOT define MM64 here to avoid redefinition warnings.
 *  - MM64 is defined in os-cfg.h or passed via compiler flags (-DMM64).
 */
#ifdef MM64
#define ADDR_TYPE uint64_t
/* On this platform uint64_t == unsigned long, so use %lu / %lx. */
#define FORMAT_ADDR  "%lu"
#define FORMATX_ADDR "%016lx"
#else
#define ADDR_TYPE uint32_t
#define FORMAT_ADDR  "%u"
#define FORMATX_ADDR "%08x"
#endif

typedef char    BYTE;
typedef ADDR_TYPE addr_t;

/* ------------------------------------------------------------------ */
/* Paging statistics                                                   */
/* ------------------------------------------------------------------ */

/*
 * Global paging statistics used by tests and for report.
 * These counters are updated in paging code (mm.c/mm64.c/libmem.c, etc.)
 */
struct paging_stats {
    unsigned long mem_access;   /* total page table lookups / translations */
    unsigned long page_faults;  /* total page faults */
    unsigned long swap_in;      /* number of swap-in operations */
    unsigned long swap_out;     /* number of swap-out operations */
    size_t        pt_bytes;     /* total bytes used by page tables */
};

/* Defined exactly once in src/os-mm.c */
extern struct paging_stats g_paging_stats;

/* Reset all counters – call this once at boot (os.c already does this). */
static inline void paging_stats_reset(void)
{
    g_paging_stats.mem_access  = 0;
    g_paging_stats.page_faults = 0;
    g_paging_stats.swap_in     = 0;
    g_paging_stats.swap_out    = 0;
    g_paging_stats.pt_bytes    = 0;
}

/* Print in a fixed format so run_paging_tests.sh can grep them. */
void paging_stats_print(void);

/* ------------------------------------------------------------------ */
/* VM / MM data structures (unchanged from your code)                 */
/* ------------------------------------------------------------------ */

struct pgn_t{
   addr_t pgn;
   struct pgn_t *pg_next; 
};

/*
 *  Memory region struct
 */
struct vm_rg_struct {
   addr_t rg_start;
   addr_t rg_end;

   struct vm_rg_struct *rg_next;
};

/*
 *  Memory area struct
 */
struct vm_area_struct {
   unsigned long vm_id;
   addr_t vm_start;
   addr_t vm_end;

   addr_t sbrk;
   /*
    * Derived field
    * unsigned long vm_limit = vm_end - vm_start
    */
   struct mm_struct *vm_mm;
   struct vm_rg_struct *vm_freerg_list;
   struct vm_area_struct *vm_next;
};

/* 
 * Memory management struct
 */
struct mm_struct {
#ifdef MM64
   addr_t *pgd;
   addr_t *p4d;
   addr_t *pud;
   addr_t *pmd;
   addr_t *pt;
#else
   uint32_t *pgd;
#endif

   struct vm_area_struct *mmap;

   /* Currently we support a fixed number of symbol */
   struct vm_rg_struct symrgtbl[PAGING_MAX_SYMTBL_SZ];

   /* list of free page (for FIFO replacement) */
   struct pgn_t *fifo_pgn;
};

/*
 * FRAME / MEM PHY struct
 */
struct framephy_struct { 
   addr_t fpn;
   struct framephy_struct *fp_next;

   /* Reserved for tracking allocated frames */
   struct mm_struct* owner;
};

struct memphy_struct {
   /* Basic field of data and size */
   BYTE *storage;
   int maxsz;
   
   /* Sequential device fields */ 
   int rdmflg;
   int cursor;

   /* Management structure */
   struct framephy_struct *free_fp_list;
   struct framephy_struct *used_fp_list;
};

#endif /* OSMM_H */
