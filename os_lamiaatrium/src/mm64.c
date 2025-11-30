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
 * 64-bit, 5-level paging
 */

#include "mm64.h"
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "os-mm.h"   /* paging stats: g_paging_stats */
/* mm64.c (or a dedicated mm_stats.c), near the top, outside any function */

#include "os-mm.h"
#include <stdio.h>

/* Single definition of the global stats variable */

#if defined(MM64)

/* ------------------------------------------------------------------ */
/* Debug                                                              */
/* ------------------------------------------------------------------ */

#ifdef MMDBG
#define MMLOG(fmt, ...) \
    do { printf("[MM64] " fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define MMLOG(fmt, ...) do {} while (0)
#endif

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

/* Get MEMRAM pointer safely from kernel */
static inline struct memphy_struct *mm_get_mram(struct krnl_t *krnl)
{
    if (!krnl) return NULL;
    return krnl->mram;
}

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,       // present
             addr_t fpn,    // physical frame number
             int drt,       // dirty
             int swp,       // swapped
             int swptyp,    // swap type
             addr_t swpoff) // swap offset
{
    if (pre != 0) {
        if (swp == 0) { /* page in RAM */
            if (fpn == 0)
                return -1;  /* invalid setting */

            SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
            CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
            if (drt)
                SETBIT(*pte, PAGING_PTE_DIRTY_MASK);
            else
                CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

            SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
        } else {        /* page swapped */
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
 * get_pd_from_address - Parse address to 5 page directory levels
 */
int get_pd_from_address(addr_t addr,
                        addr_t *pgd,
                        addr_t *p4d,
                        addr_t *pud,
                        addr_t *pmd,
                        addr_t *pt)
{
    *pgd = PAGING64_ADDR_PGD(addr);
    *p4d = PAGING64_ADDR_P4D(addr);
    *pud = PAGING64_ADDR_PUD(addr);
    *pmd = PAGING64_ADDR_PMD(addr);
    *pt  = PAGING64_ADDR_PT(addr);
    return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory levels
 */
int get_pd_from_pagenum(addr_t pgn,
                        addr_t *pgd,
                        addr_t *p4d,
                        addr_t *pud,
                        addr_t *pmd,
                        addr_t *pt)
{
    return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                               pgd, p4d, pud, pmd, pt);
}

/* ------------------------------------------------------------------ */
/* PTE swap / FPN helpers                                             */
/* ------------------------------------------------------------------ */

int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
    struct krnl_t *krnl = caller->krnl;
    struct memphy_struct *mram = mm_get_mram(krnl);
    if (!mram) {
        MMLOG("pte_set_swap: mram == NULL (PID=%u)", caller ? caller->pid : 0);
        return -1;
    }

    addr_t pte_addr;
    addr_t pte_value;

#ifdef MM64
    if (get_pte_address(krnl->mm, mram, pgn, &pte_addr) != 0)
        return -1;

    pte_value = get_32bit_entry(pte_addr, mram);
#else
    pte_addr  = (addr_t)&krnl->mm->pgd[pgn];
    pte_value = krnl->mm->pgd[pgn];
#endif

    SETBIT(pte_value, PAGING_PTE_PRESENT_MASK);
    SETBIT(pte_value, PAGING_PTE_SWAPPED_MASK);
    CLRBIT(pte_value, PAGING_PTE_DIRTY_MASK);
    SETVAL(pte_value, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
    SETVAL(pte_value, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

#ifdef MM64
    for (int i = 0; i < 4; i++) {
        BYTE byte_val = (pte_value >> (i * 8)) & 0xFF;
        MEMPHY_write(mram, pte_addr + i, byte_val);
    }
#else
    krnl->mm->pgd[pgn] = pte_value;
#endif

    return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
    struct krnl_t *krnl = caller->krnl;
    struct memphy_struct *mram = mm_get_mram(krnl);
    if (!mram) {
        MMLOG("pte_set_fpn: mram == NULL (PID=%u)", caller ? caller->pid : 0);
        return -1;
    }
    if (!krnl || !krnl->mm) {
        MMLOG("pte_set_fpn: krnl/mm NULL (caller=%p)", (void*)caller);
        return -1;
    }

#ifdef MM64
    addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
    get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

    /* Level 1: PGD */
    addr_t pgd_base  = (addr_t)krnl->mm->pgd;
    addr_t pgd_entry = get_32bit_entry(pgd_base + pgd_idx * 4, mram);

    /* Allocate P4D if not present */
    if (!(pgd_entry & PAGING_PTE_PRESENT_MASK)) {
        addr_t p4d_fpn;
        if (MEMPHY_get_freefp(mram, &p4d_fpn) != 0) {
            MMLOG("pte_set_fpn: no frame for P4D");
            return -1;
        }

        /* count PT memory: one 4KB page for P4D */
        g_paging_stats.pt_bytes += PAGING64_PAGESZ;

        pgd_entry = 0;
        SETBIT(pgd_entry, PAGING_PTE_PRESENT_MASK);
        SETVAL(pgd_entry, p4d_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

        for (int i = 0; i < 4; i++) {
            BYTE byte_val = (pgd_entry >> (i * 8)) & 0xFF;
            MEMPHY_write(mram, pgd_base + pgd_idx * 4 + i, byte_val);
        }
    }

    addr_t p4d_base = (pgd_entry & 0x1FFF) * PAGING64_PAGESZ;

    /* Level 2: P4D */
    addr_t p4d_entry = get_32bit_entry(p4d_base + p4d_idx * 4, mram);
    if (!(p4d_entry & PAGING_PTE_PRESENT_MASK)) {
        addr_t pud_fpn;
        if (MEMPHY_get_freefp(mram, &pud_fpn) != 0) {
            MMLOG("pte_set_fpn: no frame for PUD");
            return -1;
        }

        /* count PT memory: one 4KB page for PUD */
        g_paging_stats.pt_bytes += PAGING64_PAGESZ;

        p4d_entry = 0;
        SETBIT(p4d_entry, PAGING_PTE_PRESENT_MASK);
        SETVAL(p4d_entry, pud_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

        for (int i = 0; i < 4; i++) {
            BYTE byte_val = (p4d_entry >> (i * 8)) & 0xFF;
            MEMPHY_write(mram, p4d_base + p4d_idx * 4 + i, byte_val);
        }
    }

    addr_t pud_base = (p4d_entry & 0x1FFF) * PAGING64_PAGESZ;

    /* Level 3: PUD */
    addr_t pud_entry = get_32bit_entry(pud_base + pud_idx * 4, mram);
    if (!(pud_entry & PAGING_PTE_PRESENT_MASK)) {
        addr_t pmd_fpn;
        if (MEMPHY_get_freefp(mram, &pmd_fpn) != 0) {
            MMLOG("pte_set_fpn: no frame for PMD");
            return -1;
        }

        /* count PT memory: one 4KB page for PMD */
        g_paging_stats.pt_bytes += PAGING64_PAGESZ;

        pud_entry = 0;
        SETBIT(pud_entry, PAGING_PTE_PRESENT_MASK);
        SETVAL(pud_entry, pmd_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

        for (int i = 0; i < 4; i++) {
            BYTE byte_val = (pud_entry >> (i * 8)) & 0xFF;
            MEMPHY_write(mram, pud_base + pud_idx * 4 + i, byte_val);
        }
    }

    addr_t pmd_base = (pud_entry & 0x1FFF) * PAGING64_PAGESZ;

    /* Level 4: PMD */
    addr_t pmd_entry = get_32bit_entry(pmd_base + pmd_idx * 4, mram);
    if (!(pmd_entry & PAGING_PTE_PRESENT_MASK)) {
        addr_t pt_fpn;
        if (MEMPHY_get_freefp(mram, &pt_fpn) != 0) {
            MMLOG("pte_set_fpn: no frame for PT");
            return -1;
        }

        /* count PT memory: one 4KB page for PT (leaf) */
        g_paging_stats.pt_bytes += PAGING64_PAGESZ;

        pmd_entry = 0;
        SETBIT(pmd_entry, PAGING_PTE_PRESENT_MASK);
        SETVAL(pmd_entry, pt_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

        for (int i = 0; i < 4; i++) {
            BYTE byte_val = (pmd_entry >> (i * 8)) & 0xFF;
            MEMPHY_write(mram, pmd_base + pmd_idx * 4 + i, byte_val);
        }
    }

    addr_t pt_base = (pmd_entry & 0x1FFF) * PAGING64_PAGESZ;

    /* Level 5: PT (leaf) */
    addr_t pte_addr  = pt_base + pt_idx * 4;
    addr_t pte_value = get_32bit_entry(pte_addr, mram);
#else
    addr_t pte_addr  = (addr_t)&krnl->mm->pgd[pgn];
    addr_t pte_value = krnl->mm->pgd[pgn];
#endif

    SETBIT(pte_value, PAGING_PTE_PRESENT_MASK);
    CLRBIT(pte_value, PAGING_PTE_SWAPPED_MASK);
    SETVAL(pte_value, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

#ifdef MM64
    for (int i = 0; i < 4; i++) {
        BYTE byte_val = (pte_value >> (i * 8)) & 0xFF;
        MEMPHY_write(mram, pte_addr + i, byte_val);
    }
#else
    krnl->mm->pgd[pgn] = pte_value;
#endif

    return 0;
}

/*
 * pte_get_entry - read PTE
 */
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
    struct krnl_t *krnl = caller->krnl;
    struct memphy_struct *mram = mm_get_mram(krnl);
    if (!mram) {
        MMLOG("pte_get_entry: mram == NULL (PID=%u)", caller ? caller->pid : 0);
        return 0;
    }

    uint32_t pte = 0;

#ifdef MM64
    addr_t pte_addr;
    if (get_pte_address(krnl->mm, mram, pgn, &pte_addr) != 0)
        return 0;
    pte = (uint32_t)get_32bit_entry(pte_addr, mram);
#else
    pte = krnl->mm->pgd[pgn];
#endif

    /* Count this as a page-table access */
    g_paging_stats.mem_access++;

    return pte;
}

/*
 * pte_set_entry - write raw PTE value
 */
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
    struct krnl_t *krnl = caller->krnl;
    struct memphy_struct *mram = mm_get_mram(krnl);
    if (!mram) {
        MMLOG("pte_set_entry: mram == NULL (PID=%u)", caller ? caller->pid : 0);
        return -1;
    }

#ifdef MM64
    addr_t pte_addr;
    if (get_pte_address(krnl->mm, mram, pgn, &pte_addr) != 0)
        return -1;

    for (int i = 0; i < 4; i++) {
        BYTE byte_val = (pte_val >> (i * 8)) & 0xFF;
        MEMPHY_write(mram, pte_addr + i, byte_val);
    }
#else
    krnl->mm->pgd[pgn] = pte_val;
#endif

    return 0;
}

/* ------------------------------------------------------------------ */
/* vmap helpers                                                       */
/* ------------------------------------------------------------------ */

int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum)
{
    addr_t start_pgn = addr >> PAGING64_ADDR_PT_SHIFT;

    for (int pgit = 0; pgit < pgnum; pgit++) {
        addr_t pgn = start_pgn + pgit;
        uint32_t pte_val = 0;

        SETBIT(pte_val, PAGING_PTE_PRESENT_MASK);
        if (pte_set_entry(caller, pgn, pte_val) != 0)
            return -1;
    }
    return 0;
}

addr_t vmap_page_range(struct pcb_t *caller,
                       addr_t addr,
                       int pgnum,
                       struct framephy_struct *frames,
                       struct vm_rg_struct *ret_rg)
{
    struct krnl_t *krnl = caller->krnl;
    struct framephy_struct *fpit = frames;
    int pgit = 0;
    addr_t start_pgn = addr >> PAGING64_ADDR_PT_SHIFT;

    ret_rg->rg_start = addr;
    ret_rg->rg_end   = addr + pgnum * PAGING64_PAGESZ;

    for (pgit = 0; pgit < pgnum && fpit != NULL; ++pgit, fpit = fpit->fp_next) {
        addr_t pgn = start_pgn + pgit;

        if (pte_set_fpn(caller, pgn, fpit->fpn) != 0) {
            ret_rg->rg_end = addr + pgit * PAGING64_PAGESZ;
            return pgit;
        }

#ifdef MM_PAGING
        enlist_pgn_node(&krnl->mm->fifo_pgn, pgn);
#endif
    }

    return pgit;
}

/* ------------------------------------------------------------------ */
/* Frame allocation / vm_map_ram                                      */
/* ------------------------------------------------------------------ */

addr_t alloc_pages_range(struct pcb_t *caller,
                         int req_pgnum,
                         struct framephy_struct **frm_lst)
{
    struct krnl_t *krnl = caller->krnl;
    struct memphy_struct *mram = mm_get_mram(krnl);
    addr_t fpn;
    int pgit;
    struct framephy_struct *head = NULL;
    struct framephy_struct *tail = NULL;

    *frm_lst = NULL;

    if (!mram) {
        MMLOG("alloc_pages_range: mram == NULL (PID=%u)", caller ? caller->pid : 0);
        return 0;
    }

    MMLOG("alloc_pages_range: PID=%u req_pgnum=%d",
          caller ? caller->pid : 0, req_pgnum);

    for (pgit = 0; pgit < req_pgnum; ++pgit) {
        if (MEMPHY_get_freefp(mram, &fpn) != 0) {
            MMLOG("alloc_pages_range: out of frames after %d pages", pgit);
            break;
        }

        struct framephy_struct *node =
            (struct framephy_struct*)malloc(sizeof(struct framephy_struct));
        if (!node) {
            perror("malloc framephy_struct");
            break;
        }

        node->fpn = fpn;
        node->fp_next = NULL;

        if (!head)
            head = node;
        else
            tail->fp_next = node;
        tail = node;

        MMLOG("alloc_pages_range: PID=%u got fpn=%llu (%d/%d)",
              caller ? caller->pid : 0,
              (unsigned long long)fpn, pgit + 1, req_pgnum);
    }

    *frm_lst = head;
    return pgit;
}

void free_frame_list(struct pcb_t *caller, struct framephy_struct *frm_lst)
{
    struct krnl_t *krnl = caller->krnl;
    struct memphy_struct *mram = mm_get_mram(krnl);
    struct framephy_struct *curr = frm_lst;

    if (!mram)
        return;

    while (curr != NULL) {
        struct framephy_struct *next = curr->fp_next;
        MEMPHY_put_freefp(mram, curr->fpn);
        free(curr);
        curr = next;
    }
}

addr_t vm_map_ram(struct pcb_t *caller,
                  addr_t astart,
                  addr_t aend,
                  addr_t mapstart,
                  int incpgnum,
                  struct vm_rg_struct *ret_rg)
{
    struct framephy_struct *frm_lst = NULL;
    int ret_alloc = (int)alloc_pages_range(caller, incpgnum, &frm_lst);

    MMLOG("vm_map_ram: PID=%u astart=" FORMAT_ADDR " aend=" FORMAT_ADDR
          " mapstart=" FORMAT_ADDR " incpgnum=%d ret_alloc=%d",
          caller ? caller->pid : 0,
          astart, aend, mapstart, incpgnum, ret_alloc);

    if (ret_alloc == 0)
        return (addr_t)-1;

    if (ret_alloc < incpgnum) {
        free_frame_list(caller, frm_lst);
        return (addr_t)-1;
    }

    int mapped = (int)vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);
    if (mapped < incpgnum) {
        free_frame_list(caller, frm_lst);
        return (addr_t)-1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Swap copy                                                           */
/* ------------------------------------------------------------------ */

int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
    for (int cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++) {
        addr_t addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
        addr_t addrdst = dstfpn * PAGING_PAGESZ + cellidx;

        BYTE data;
        MEMPHY_read(mpsrc, addrsrc, &data);
        MEMPHY_write(mpdst, addrdst, data);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* init_mm                                                             */
/* ------------------------------------------------------------------ */

int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
    struct krnl_t *krnl = caller->krnl;
    struct memphy_struct *mram = mm_get_mram(krnl);
    struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

    if (!mram) {
        MMLOG("init_mm: mram == NULL (PID=%u)", caller ? caller->pid : 0);
        free(vma0);
        return -1;
    }
    if (!vma0)
        return -1;

#ifdef MM64
    addr_t pgd_fpn;
    if (MEMPHY_get_freefp(mram, &pgd_fpn) != 0) {
        free(vma0);
        return -1;
    }

    /* one PGD page per process */
    g_paging_stats.pt_bytes += PAGING64_PAGESZ;

    mm->pgd = (addr_t *)(pgd_fpn * PAGING64_PAGESZ);

    addr_t pgd_base = (addr_t)mm->pgd;
    for (int i = 0; i < PAGING64_PAGESZ; i++) {
        MEMPHY_write(mram, pgd_base + i, 0);
    }

    mm->p4d = NULL;
    mm->pud = NULL;
    mm->pmd = NULL;
    mm->pt  = NULL;
#else
    addr_t pgd_fpn32;
    if (MEMPHY_get_freefp(mram, &pgd_fpn32) != 0) {
        free(vma0);
        return -1;
    }

    /* 32-bit single-level, count that page as well */
    g_paging_stats.pt_bytes += PAGING_PAGESZ;

    mm->pgd = (uint32_t *)(pgd_fpn32 * PAGING_PAGESZ);

    addr_t pgd_base32 = (addr_t)mm->pgd;
    for (int i = 0; i < PAGING_PAGESZ; i++) {
        MEMPHY_write(mram, pgd_base32 + i, 0);
    }
#endif

    mm->fifo_pgn = NULL;
    memset(mm->symrgtbl, 0, sizeof(struct vm_rg_struct) * PAGING_MAX_SYMTBL_SZ);

    vma0->vm_id    = 0;
    vma0->vm_start = 0;
    vma0->vm_end   = vma0->vm_start;
    vma0->sbrk     = vma0->vm_start;

    struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
    enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

    vma0->vm_next = NULL;
    vma0->vm_mm   = mm;

    mm->mmap = vma0;

    MMLOG("init_mm: PID=%u mm=%p pgd_base=" FORMAT_ADDR,
          caller ? caller->pid : 0, (void*)mm, (addr_t)mm->pgd);

    return 0;
}

/* ------------------------------------------------------------------ */
/* VM region + debug helpers                                          */
/* ------------------------------------------------------------------ */

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
    struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));
    rgnode->rg_start = rg_start;
    rgnode->rg_end   = rg_end;
    rgnode->rg_next  = NULL;
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
    if (fp == NULL) { printf("NULL list\n"); return -1; }
    printf("\n");
    while (fp != NULL) {
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
    while (rg != NULL) {
        printf("rg[" FORMAT_ADDR "->" FORMAT_ADDR "]\n",
               rg->rg_start, rg->rg_end);
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
    while (vma != NULL) {
        printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n",
               vma->vm_start, vma->vm_end);
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
    while (ip != NULL) {
        printf("va[" FORMAT_ADDR "]\n", ip->pgn);
        ip = ip->pg_next;
    }
    printf("\n");
    return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
    (void)caller;
    (void)start;
    (void)end;
    /* Optional: implement a detailed dump if needed */
    return 0;
}

/* ------------------------------------------------------------------ */
/* 32-bit PTE entry helpers over MEMPHY                               */
/* ------------------------------------------------------------------ */

addr_t get_32bit_entry(addr_t base_address, struct memphy_struct *mp)
{
    addr_t entry = 0;
    for (int i = 0; i < 4; i++) {
        BYTE byte_val;
        if (MEMPHY_read(mp, base_address + i, &byte_val) != 0)
            return (addr_t)-1;
        entry |= ((addr_t)byte_val) << (i * 8);
    }
    return entry;
}

int translate_address(struct mm_struct *mm,
                      struct memphy_struct *mp,
                      addr_t vaddr,
                      addr_t *paddr)
{
    /* one logical "page-table lookup" */
    g_paging_stats.mem_access++;

    addr_t pgd, p4d, pud, pmd, pt;
    get_pd_from_address(vaddr, &pgd, &p4d, &pud, &pmd, &pt);

    addr_t pgd_base  = (addr_t)mm->pgd;
    addr_t pgd_entry = get_32bit_entry(pgd_base + pgd * 4, mp);
    if (!(pgd_entry & PAGING_PTE_PRESENT_MASK)) return -1;
    addr_t p4d_base = (pgd_entry & 0x1FFF) * PAGING64_PAGESZ;

    addr_t p4d_entry = get_32bit_entry(p4d_base + p4d * 4, mp);
    if (!(p4d_entry & PAGING_PTE_PRESENT_MASK)) return -1;
    addr_t pud_base = (p4d_entry & 0x1FFF) * PAGING64_PAGESZ;

    addr_t pud_entry = get_32bit_entry(pud_base + pud * 4, mp);
    if (!(pud_entry & PAGING_PTE_PRESENT_MASK)) return -1;
    addr_t pmd_base = (pud_entry & 0x1FFF) * PAGING64_PAGESZ;

    addr_t pmd_entry = get_32bit_entry(pmd_base + pmd * 4, mp);
    if (!(pmd_entry & PAGING_PTE_PRESENT_MASK)) return -1;
    addr_t pt_base = (pmd_entry & 0x1FFF) * PAGING64_PAGESZ;

    addr_t pt_entry = get_32bit_entry(pt_base + pt * 4, mp);
    if (!(pt_entry & PAGING_PTE_PRESENT_MASK)) return -1;

    addr_t fpn       = pt_entry & 0x1FFF;
    addr_t page_base = fpn * PAGING64_PAGESZ;
    addr_t offset    = vaddr & 0xFFF;

    *paddr = page_base + offset;
    return 0;
}

int get_pte_address(struct mm_struct *mm,
                    struct memphy_struct *mp,
                    addr_t pgn,
                    addr_t *pte_addr)
{
    /* one logical page-table access */
    g_paging_stats.mem_access++;

    addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
    get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

    addr_t pgd_base  = (addr_t)mm->pgd;
    addr_t pgd_entry = get_32bit_entry(pgd_base + pgd_idx * 4, mp);
    if (!(pgd_entry & PAGING_PTE_PRESENT_MASK)) return -1;
    addr_t p4d_base = (pgd_entry & 0x1FFF) * PAGING64_PAGESZ;

    addr_t p4d_entry = get_32bit_entry(p4d_base + p4d_idx * 4, mp);
    if (!(p4d_entry & PAGING_PTE_PRESENT_MASK)) return -1;
    addr_t pud_base = (p4d_entry & 0x1FFF) * PAGING64_PAGESZ;

    addr_t pud_entry = get_32bit_entry(pud_base + pud_idx * 4, mp);
    if (!(pud_entry & PAGING_PTE_PRESENT_MASK)) return -1;
    addr_t pmd_base = (pud_entry & 0x1FFF) * PAGING64_PAGESZ;

    addr_t pmd_entry = get_32bit_entry(pmd_base + pmd_idx * 4, mp);
    if (!(pmd_entry & PAGING_PTE_PRESENT_MASK)) return -1;
    addr_t pt_base = (pmd_entry & 0x1FFF) * PAGING64_PAGESZ;

    *pte_addr = pt_base + pt_idx * 4;
    return 0;
}

#endif /* defined(MM64) */
