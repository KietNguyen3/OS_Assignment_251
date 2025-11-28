/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "os-mm.h"
#include "syscall.h"
#include "libmem.h"
#include "queue.h"
#include <stdlib.h>

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

static struct pcb_t *find_pcb_by_pid(struct krnl_t *krnl, uint32_t pid)
{
    int i;
   
/* chạy qua danh sách đang chạy */
    if (krnl->running_list != NULL) {
        struct queue_t *rq = krnl->running_list;
        for (i = 0; i < rq->size; i++) {
            if (rq->proc[i] && rq->proc[i]->pid == pid) {
                return rq->proc[i];
            }
        }
    }

/* nếu chưa thấy thì thử trong ready_queue */
    if (krnl->ready_queue != NULL) {
        struct queue_t *rq = krnl->ready_queue;
        for (i = 0; i < rq->size; i++) {
            if (rq->proc[i] && rq->proc[i]->pid == pid) {
                return rq->proc[i];
            }
        }
    }

    return NULL;
}

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs* regs)
{
    int memop = regs->a1;
    BYTE value;
    struct pcb_t *caller = find_pcb_by_pid(krnl, pid);
    if (caller == NULL) {
        printf("__sys_memmap: khong tim thay PCB cho pid = %u\n", pid);
        return -1;
    }

	
    switch (memop) {
    case SYSMEM_MAP_OP:
        vmap_pgd_memset(caller, regs->a2, regs->a3);
        break;

    case SYSMEM_INC_OP:
        inc_vma_limit(caller, regs->a2, regs->a3);
        break;

    case SYSMEM_SWP_OP:
        __mm_swap_page(caller, regs->a2, regs->a3);
        break;

    case SYSMEM_IO_READ:
        MEMPHY_read(krnl->mram, regs->a2, &value);
        regs->a3 = value;
        break;

    case SYSMEM_IO_WRITE:
        MEMPHY_write(krnl->mram, regs->a2, regs->a3);
        break;

    default:
        printf("__sys_memmap: memop la la: %d\n", memop);
        break;
    }

    return 0;
}
