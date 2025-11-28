#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: put a new process to queue [q] */
        if (q == NULL || proc == NULL)
                return;

        if (q->size >= MAX_QUEUE_SIZE)
                return;
        
        q->proc[q->size] = proc;
        q->size++;
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */
        if (empty(q))
                return NULL;
        
        int best = 0;           // index của phần tử có highest priority(lowest priority value)
        
        for (int i = 1; i < q->size; i++)
        {
                struct pcb_t *cur = q->proc[i];
                struct pcb_t *best_proc = q->proc[best];

                /* Lấy priority hoặc prior tương ứng:
                - Nếu có MLQ_SCHED: dùng prio
                - Nếu không: dùng priority
                */

#ifdef MLQ_SCHED
                uint32_t cur_prio = cur->prio;
                uint32_t best_prio = best_proc->prio;
#else   
                uint32_t cur_prio = cur->priority;
                uint32_t best_prio = best_proc->priority;
#endif

                // the lower value -> the higher priority
                if (cur_prio < best_prio)
                        best = i;
                
        }

        // Save pcb
        struct pcb_t *ret = q->proc[best];

        // Dồn phần tử
        for (int i = best; i < q->size - 1; i++)
        {
                q->proc[i] = q->proc[i + 1];
        }

        // Giảm sau khi lấy ra
        q->size--;

	return ret;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: remove a specific item from queue
         * */
        if (empty(q) || proc  == NULL)
                return NULL;
        
        // finding the pcb
        for (int i = 0; i < q->size; i++)
        {
                if (q->proc[i] == proc)         // PCB cần tìm
                {
                        struct pcb_t *ret = q->proc[i];
                        for (int j = i; j < q->size - 1; j++ )
                        {
                                q->proc[j] = q->proc[j + 1];
                        }
                        q->size--;
                        return ret;
                }
        }

        return NULL;       
}