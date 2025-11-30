/*
 * Simple OS - main control / bootstrap
 */

#include "cpu.h"
#include "timer.h"
#include "sched.h"
#include "loader.h"
#include "mm.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* --------------------------------------------------------------------- */
/* Debug macros                                                          */
/* --------------------------------------------------------------------- */

#ifdef OSDBG
#define OSLOG(fmt, ...) \
    do { printf("[OS] " fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define OSLOG(fmt, ...) do {} while (0)
#endif

static int time_slot;
static int num_cpus;
static int done = 0;
struct krnl_t os;

#ifdef MM_PAGING
#include "os-mm.h"   /* stats: g_paging_stats, paging_stats_reset/print */

static int memramsz;
static int memswpsz[PAGING_MAX_MMSWP];

struct mmpaging_ld_args {
    /* A dispatched argument struct to compact many-fields passing to loader */
    int vmemsz;
    struct memphy_struct *mram;
    struct memphy_struct **mswp;
    struct memphy_struct *active_mswp;
    int active_mswp_id;
    struct timer_id_t  *timer_id;
};
#endif

struct ld_args {
    char ** path;
    unsigned long * start_time;
#ifdef MLQ_SCHED
    unsigned long * prio;
#endif
} ld_processes;

int num_processes;

struct cpu_args {
    struct timer_id_t * timer_id;
    int id;
};

/* --------------------------------------------------------------------- */
/* CPU routine                                                           */
/* --------------------------------------------------------------------- */

static void * cpu_routine(void * args)
{
    struct timer_id_t * timer_id = ((struct cpu_args*)args)->timer_id;
    int id = ((struct cpu_args*)args)->id;

    int time_left = 0;
    struct pcb_t * proc = NULL;

    OSLOG("CPU %d thread started", id);

    while (1) {
        /* Check the status of current process */
        if (proc == NULL) {
            /* No process is running, then we load new process from
             * ready queue */
            proc = get_proc();
            if (proc == NULL) {
                OSLOG("CPU %d: no process in ready queue at time %lu",
                      id, current_time());
                next_slot(timer_id);
                continue; /* First load failed. skip dummy load */
            }
        } else if (proc->pc == proc->code->size) {
            /* The process has finished its job */
            printf("\tCPU %d: Processed %2d has finished\n",
                   id ,proc->pid);
            OSLOG("CPU %d: freeing PCB PID=%d", id, proc->pid);
            free(proc);
            proc = get_proc();
            time_left = 0;
        } else if (time_left == 0) {
            /* The process has done its job in current time slot */
            printf("\tCPU %d: Put process %2d to run queue\n",
                   id, proc->pid);
            OSLOG("CPU %d: time slice over for PID=%d, requeue",
                  id, proc->pid);
            put_proc(proc);
            proc = get_proc();
        }

        /* Recheck process status after loading new process */
        if (proc == NULL && done) {
            /* No process to run, exit */
            printf("\tCPU %d stopped\n", id);
            OSLOG("CPU %d: done and no process left, exiting thread", id);
            break;
        } else if (proc == NULL) {
            /* There may be new processes to run in
             * next time slots, just skip current slot */
            OSLOG("CPU %d: idle slot at time %lu", id, current_time());
            next_slot(timer_id);
            continue;
        } else if (time_left == 0) {
            printf("\tCPU %d: Dispatched process %2d\n",
                   id, proc->pid);
            OSLOG("CPU %d: dispatched PID=%d new time slice=%d",
                  id, proc->pid, time_slot);
            time_left = time_slot;
        }

        /* Run current process */
        run(proc);
        time_left--;
        next_slot(timer_id);
    }

    detach_event(timer_id);
    pthread_exit(NULL);
}

/* --------------------------------------------------------------------- */
/* Loader routine                                                        */
/* --------------------------------------------------------------------- */

static void * ld_routine(void * args)
{
#ifdef MM_PAGING
    struct memphy_struct*  mram        = ((struct mmpaging_ld_args *)args)->mram;
    struct memphy_struct** mswp        = ((struct mmpaging_ld_args *)args)->mswp;
    struct memphy_struct*  active_mswp = ((struct mmpaging_ld_args *)args)->active_mswp;
    struct timer_id_t *    timer_id    = ((struct mmpaging_ld_args *)args)->timer_id;
#else
    struct timer_id_t * timer_id = (struct timer_id_t*)args;
#endif

    int i = 0;
    printf("ld_routine\n");
    OSLOG("Loader thread started, num_processes=%d", num_processes);

    while (i < num_processes) {
        struct pcb_t * proc = load(ld_processes.path[i]);
        struct krnl_t * krnl = proc->krnl = &os;

#ifdef MLQ_SCHED
        proc->prio = ld_processes.prio[i];
#endif

        OSLOG("Loader: loaded image %s as PID=%d, default prio=%u",
              ld_processes.path[i], proc->pid, proc->priority);

        /* Wait until start_time for this process */
        while (current_time() < ld_processes.start_time[i]) {
            OSLOG("Loader: waiting to start PID=%d at time %lu (current=%lu)",
                  proc->pid,
                  ld_processes.start_time[i],
                  current_time());
            next_slot(timer_id);
        }

#ifdef MM_PAGING
        /* IMPORTANT: hook kernel memory pointers BEFORE init_mm() */
        krnl->mram           = mram;
        krnl->mswp           = mswp;
        krnl->active_mswp    = active_mswp;
        krnl->active_mswp_id = 0;

        OSLOG("Loader: kernel mem hooks set: mram=%p mswp=%p active_mswp=%p",
              (void*)krnl->mram, (void*)krnl->mswp, (void*)krnl->active_mswp);

        krnl->mm = malloc(sizeof(struct mm_struct));
        if (!krnl->mm) {
            perror("malloc mm_struct");
            exit(1);
        }

        OSLOG("Loader: calling init_mm(mm=%p, PID=%d)",
              (void*)krnl->mm, proc->pid);

        if (init_mm(krnl->mm, proc) != 0) {
            fprintf(stderr, "[OS] init_mm failed for PID=%d\n", proc->pid);
            exit(1);
        }

        /* make sure global os.mm is valid for mm-vm.c helpers */
        os.mm = krnl->mm;

        OSLOG("Loader: init_mm done for PID=%d, mm=%p mram=%p mswp[0]=%p",
              proc->pid,
              (void*)krnl->mm,
              (void*)krnl->mram,
              (void*)(krnl->mswp ? krnl->mswp[0] : NULL));
#endif

#ifdef MLQ_SCHED
        printf("\tLoaded a process at %s, PID: %d PRIO: %lu\n",
               ld_processes.path[i], proc->pid, ld_processes.prio[i]);
#else
        printf("\tLoaded a process at %s, PID: %d\n",
               ld_processes.path[i], proc->pid);
#endif

        add_proc(proc);
        OSLOG("Loader: added PID=%d to ready queue", proc->pid);

        free(ld_processes.path[i]);
        i++;
        next_slot(timer_id);
    }

    free(ld_processes.path);
    free(ld_processes.start_time);
#ifdef MLQ_SCHED
    free(ld_processes.prio);
#endif

    done = 1;
    OSLOG("Loader: all processes loaded, done=1");
    detach_event(timer_id);
    pthread_exit(NULL);
}

/* --------------------------------------------------------------------- */
/* Config reader                                                         */
/* --------------------------------------------------------------------- */

static void read_config(const char * path)
{
    FILE * file;
    if ((file = fopen(path, "r")) == NULL) {
        printf("Cannot find configure file at %s\n", path);
        exit(1);
    }

    /* header: time_slice, num_cpus, num_processes */
    fscanf(file, "%d %d %d\n", &time_slot, &num_cpus, &num_processes);
    printf("[CONF] time_slice=%d cpus=%d procs=%d\n",
           time_slot, num_cpus, num_processes);

    ld_processes.path = (char**)malloc(sizeof(char*) * num_processes);
    ld_processes.start_time =
        (unsigned long*)malloc(sizeof(unsigned long) * num_processes);

#ifdef MM_PAGING
    int sit;

    /* --- Auto-detect whether there is a RAM/SWAP size line --- */
    long pos = ftell(file);
    char line[256];

    if (!fgets(line, sizeof(line), file)) {
        /* No more lines → fall back to defaults */
        memramsz    = 0x10000000;
        memswpsz[0] = 0x01000000;
        for (sit = 1; sit < PAGING_MAX_MMSWP; sit++)
            memswpsz[sit] = 0;

        printf("[CONF] MM_FIXED_MEMSZ=AUTO RAM=%#x SWP0=%#x\n",
               memramsz, memswpsz[0]);
    } else {
        /* Check if this line looks like pure numbers (RAM/SWAP) or contains letters (proc name) */
        int has_alpha = 0;
        for (int k = 0; line[k]; k++) {
            if ((line[k] >= 'A' && line[k] <= 'Z') ||
                (line[k] >= 'a' && line[k] <= 'z') ||
                line[k] == '/') {
                has_alpha = 1;
                break;
            }
        }

        if (!has_alpha) {
            /* Treat as RAM/SWAP line */
            int n = sscanf(line, "%d %d %d %d %d",
                           &memramsz,
                           &memswpsz[0],
                           &memswpsz[1],
                           &memswpsz[2],
                           &memswpsz[3]);
            if (n < 2) {
                fprintf(stderr, "[CONF] Invalid RAM/SWAP line: '%s'\n", line);
                exit(1);
            }
            for (sit = n - 1; sit < PAGING_MAX_MMSWP; sit++)
                memswpsz[sit] = 0;

            printf("[CONF] MM_FIXED_MEMSZ=FILE RAM=%#x", memramsz);
            for (sit = 0; sit < PAGING_MAX_MMSWP; sit++)
                printf(" SWP%d=%#x", sit, memswpsz[sit]);
            printf("\n");
        } else {
            /* This line is actually the FIRST PROCESS LINE.
             * → revert and use default RAM/SWAP sizes (legacy configs).
             */
            fseek(file, pos, SEEK_SET);

            memramsz    = 0x10000000;
            memswpsz[0] = 0x01000000;
            for (sit = 1; sit < PAGING_MAX_MMSWP; sit++)
                memswpsz[sit] = 0;

            printf("[CONF] MM_FIXED_MEMSZ=DEFAULT RAM=%#x SWP0=%#x\n",
                   memramsz, memswpsz[0]);
        }
    }
#endif /* MM_PAGING */

#ifdef MLQ_SCHED
    ld_processes.prio =
        (unsigned long*)malloc(sizeof(unsigned long) * num_processes);
#endif

    /* --- Process lines --- */
    int i;
    for (i = 0; i < num_processes; i++) {
        ld_processes.path[i] = (char*)malloc(sizeof(char) * 100);
        ld_processes.path[i][0] = '\0';
        strcat(ld_processes.path[i], "input/proc/");

        char proc[100];
#ifdef MLQ_SCHED
        fscanf(file, "%lu %s %lu\n",
               &ld_processes.start_time[i],
               proc,
               &ld_processes.prio[i]);
#else
        fscanf(file, "%lu %s\n",
               &ld_processes.start_time[i],
               proc);
#endif
        strcat(ld_processes.path[i], proc);

#ifdef MLQ_SCHED
        printf("[CONF] proc[%d]: start=%lu path=%s prio=%lu\n",
               i,
               ld_processes.start_time[i],
               ld_processes.path[i],
               ld_processes.prio[i]);
#else
        printf("[CONF] proc[%d]: start=%lu path=%s\n",
               i,
               ld_processes.start_time[i],
               ld_processes.path[i]);
#endif
    }

    fclose(file);
}


/* --------------------------------------------------------------------- */
/* main                                                                  */
/* --------------------------------------------------------------------- */

int main(int argc, char * argv[])
{
    /* Read config */
    if (argc != 2) {
        printf("Usage: os [path to configure file]\n");
        return 1;
    }

    char path[256];
    path[0] = '\0';
    strcat(path, "input/");
    strcat(path, argv[1]);

    read_config(path);

#ifdef MM_PAGING
    /* Reset paging statistics at the beginning of each run */
    paging_stats_reset();
#endif

    pthread_t * cpu = (pthread_t*)malloc(num_cpus * sizeof(pthread_t));
    struct cpu_args * args =
        (struct cpu_args*)malloc(sizeof(struct cpu_args) * num_cpus);
    pthread_t ld;

    /* Init timer */
    int i;
    for (i = 0; i < num_cpus; i++) {
        args[i].timer_id = attach_event();
        args[i].id = i;
    }
    struct timer_id_t * ld_event = attach_event();

    printf("[BOOT] starting timer...\n");
    start_timer();

#ifdef MM_PAGING
    /* Init all MEMPHY: 1 MEMRAM and up to PAGING_MAX_MMSWP MEMSWP */
    int rdmflag = 1; /* random access device */

    struct memphy_struct *mram =
        (struct memphy_struct*)malloc(sizeof(struct memphy_struct));
    struct memphy_struct **mswp =
        (struct memphy_struct**)malloc(sizeof(struct memphy_struct*) * PAGING_MAX_MMSWP);

    /* Create MEM RAM */
    init_memphy(mram, memramsz, rdmflag);
    printf("[BOOT] init MEMRAM size=%#x\n", memramsz);

    /* Create all MEM SWAP */
    int sit;
    for (sit = 0; sit < PAGING_MAX_MMSWP; sit++) {
        if (memswpsz[sit] > 0) {
            mswp[sit] =
                (struct memphy_struct*)malloc(sizeof(struct memphy_struct));
            init_memphy(mswp[sit], memswpsz[sit], rdmflag);
            printf("[BOOT] init MEMSWP[%d] size=%#x\n",
                   sit, memswpsz[sit]);
        } else {
            mswp[sit] = NULL;
            printf("[BOOT] MEMSWP[%d] disabled (size=0)\n", sit);
        }
    }

    /* Make sure global kernel knows about MEMPHY (optional but nice) */
    os.mram           = mram;
    os.mswp           = mswp;
    os.active_mswp    = mswp[0];
    os.active_mswp_id = 0;

    /* In Paging mode, pass system mem to loader via args */
    struct mmpaging_ld_args *mm_ld_args =
        (struct mmpaging_ld_args*)malloc(sizeof(struct mmpaging_ld_args));

    mm_ld_args->timer_id       = ld_event;
    mm_ld_args->mram           = mram;
    mm_ld_args->mswp           = mswp;
    mm_ld_args->active_mswp    = mswp[0];
    mm_ld_args->active_mswp_id = 0;

    OSLOG("main: MM_PAGING enabled, mram=%p mswp=%p active_mswp=%p",
          (void*)mram, (void*)mswp, (void*)mm_ld_args->active_mswp);
#endif

    /* Init scheduler */
    init_scheduler();
    OSLOG("main: scheduler initialized");

    /* Run CPU and loader */
#ifdef MM_PAGING
    pthread_create(&ld, NULL, ld_routine, (void*)mm_ld_args);
#else
    pthread_create(&ld, NULL, ld_routine, (void*)ld_event);
#endif

    for (i = 0; i < num_cpus; i++) {
        pthread_create(&cpu[i], NULL,
                       cpu_routine, (void*)&args[i]);
        OSLOG("main: CPU thread %d created", i);
    }

    /* Wait for CPU and loader finishing */
    for (i = 0; i < num_cpus; i++) {
        pthread_join(cpu[i], NULL);
    }
    pthread_join(ld, NULL);

    /* Stop timer */
    stop_timer();
    printf("[BOOT] timer stopped (now=%lu)\n", current_time());

    OSLOG("main: all threads joined, exiting");


    /* Print paging statistics in the format expected by run_paging_tests.sh */
    paging_stats_print();


    return 0;
}
