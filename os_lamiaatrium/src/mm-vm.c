/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "os-mm.h"      /* <<< add this */

#ifdef MMDBG
#define MMLOG(fmt, ...) \
    do { printf("[MM-VM] " fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define MMLOG(fmt, ...) do {} while (0)
#endif

/* global kernel object (set up in os.c) */
extern struct krnl_t os;

/* __swap_cp_page is implemented in mm64.c / mm.c */
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn);

/* --------------------------------------------------------- */
/* get_vma_by_num - get vm area by numID                     */
/* --------------------------------------------------------- */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
    if (!mm || !mm->mmap)
        return NULL;

    struct vm_area_struct *pvma = mm->mmap;
    int vmait = pvma->vm_id;

    while (vmait < vmaid) {
        pvma = pvma->vm_next;
        if (pvma == NULL)
            return NULL;
        vmait = pvma->vm_id;
    }

    return pvma;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn, addr_t swpfpn)
{
    (void)caller;  /* we only need global os.mram / os.active_mswp */

    if (!os.mram || !os.active_mswp) {
        MMLOG("__mm_swap_page: mram or active_mswp is NULL");
        return -1;
    }

    MMLOG("__mm_swap_page: vicfpn=%llu swpfpn=%llu",
          (unsigned long long)vicfpn,
          (unsigned long long)swpfpn);

    /* RAM -> SWAP (victim out) */
    int rc = __swap_cp_page(os.mram, vicfpn, os.active_mswp, swpfpn);
    if (rc == 0) {
        /* Count successful swap-out */
        g_paging_stats.swap_out++;
    }
    return rc;
}

/* --------------------------------------------------------- */
/* get_vm_area_node_at_brk - new region at current sbrk      */
/* --------------------------------------------------------- */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller,
                                             int vmaid,
                                             addr_t size,
                                             addr_t alignedsz)
{
    (void)caller;    /* only vmaid + os.mm used here */
    (void)alignedsz; /* kept for interface compatibility */

    struct mm_struct *mm = os.mm;
    if (!mm) {
        MMLOG("get_vm_area_node_at_brk: os.mm == NULL");
        return NULL;
    }

    struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);
    if (!cur_vma) {
        MMLOG("get_vm_area_node_at_brk: vma %d not found", vmaid);
        return NULL;
    }

    struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
    if (!newrg) {
        perror("malloc vm_rg_struct");
        return NULL;
    }

    /* allocate at current sbrk and grow upward */
    newrg->rg_start = cur_vma->sbrk;
    newrg->rg_end   = newrg->rg_start + size;
    newrg->rg_next  = NULL;

    MMLOG("get_vm_area_node_at_brk: vmaid=%d start=%llu end=%llu",
          vmaid,
          (unsigned long long)newrg->rg_start,
          (unsigned long long)newrg->rg_end);

    return newrg;
}

/* --------------------------------------------------------- */
/* validate_overlap_vm_area - check planned area vs others   */
/* --------------------------------------------------------- */
int validate_overlap_vm_area(struct pcb_t *caller,
                             int vmaid,
                             addr_t vmastart,
                             addr_t vmaend)
{
    (void)caller; /* we validate against all VMAs in os.mm */
    (void)vmaid;  /* no per-vma restriction */

    if (vmastart >= vmaend) {
        MMLOG("validate_overlap_vm_area: invalid range [%llu, %llu)",
              (unsigned long long)vmastart,
              (unsigned long long)vmaend);
        return -1;
    }

    struct mm_struct *mm = os.mm;
    if (!mm) {
        MMLOG("validate_overlap_vm_area: os.mm == NULL");
        return -1;
    }

    struct vm_area_struct *vma = mm->mmap;
    while (vma) {
        if (OVERLAP(vmastart, vmaend, vma->vm_start, vma->vm_end)) {
            MMLOG("validate_overlap_vm_area: overlap with vm_id=%d "
                  "[%llu, %llu)",
                  vma->vm_id,
                  (unsigned long long)vma->vm_start,
                  (unsigned long long)vma->vm_end);
            return -1;
        }
        vma = vma->vm_next;
    }

    return 0;
}

/* --------------------------------------------------------- */
/* inc_vma_limit - grow vma by inc_sz bytes                  */
/* --------------------------------------------------------- */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
    if (inc_sz == 0)
        return 0;

    struct mm_struct *mm = os.mm;
    if (!mm) {
        MMLOG("inc_vma_limit: os.mm == NULL (PID=%u)",
              caller ? caller->pid : 0);
        return -1;
    }

    struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);
    if (!cur_vma) {
        MMLOG("inc_vma_limit: vma %d not found", vmaid);
        return -1;
    }

    /* align request to page size */
    addr_t aligned = PAGING_PAGE_ALIGNSZ(inc_sz);
    int incnumpage = (int)(aligned / PAGING_PAGESZ);
    if (incnumpage <= 0)
        return 0;

    MMLOG("inc_vma_limit: pid=%u vmaid=%d inc_sz=%llu aligned=%llu pages=%d",
          caller ? caller->pid : 0,
          vmaid,
          (unsigned long long)inc_sz,
          (unsigned long long)aligned,
          incnumpage);

    /* get region at current sbrk, with aligned size */
    struct vm_rg_struct *area =
        get_vm_area_node_at_brk(caller, vmaid, aligned, aligned);
    if (!area) {
        MMLOG("inc_vma_limit: get_vm_area_node_at_brk failed");
        return -1;
    }

    /* sanity / overlap check */
    if (validate_overlap_vm_area(caller, vmaid,
                                 area->rg_start, area->rg_end) < 0) {
        MMLOG("inc_vma_limit: overlap detected, aborting");
        free(area);
        return -1;
    }

    addr_t old_end = cur_vma->sbrk;

    /* map virtual range to physical frames */
    if (vm_map_ram(caller,
                   area->rg_start,
                   area->rg_end,
                   old_end,
                   incnumpage,
                   area) < 0) {
        MMLOG("inc_vma_limit: vm_map_ram failed");
        free(area);
        return -1;
    }

    /* update vma break and end */
    cur_vma->sbrk = area->rg_end;
    if (cur_vma->sbrk > cur_vma->vm_end)
        cur_vma->vm_end = cur_vma->sbrk;

    MMLOG("inc_vma_limit: new sbrk=%llu vm_end=%llu",
          (unsigned long long)cur_vma->sbrk,
          (unsigned long long)cur_vma->vm_end);

    /* NOTE: if you keep a free-region list per VMA, you can enlist area
       there; if not needed, area can be kept only as a mapping descriptor. */

    return 0;
}
