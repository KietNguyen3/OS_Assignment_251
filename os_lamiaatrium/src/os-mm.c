#include <stdio.h>
#include "os-mm.h"

/* Global stats object (zero-initialized by loader) */
struct paging_stats g_paging_stats;

/*
 * Print stats in the exact format run_paging_tests.sh expects:
 *   [STATS] mem_access = <val>
 *   [STATS] page_faults = <val>
 *   [STATS] swap_in = <val>
 *   [STATS] swap_out = <val>
 *   [STATS] pt_bytes = <val>
 */
void paging_stats_print(void)
{
    printf("[STATS] mem_access = %lu\n",   g_paging_stats.mem_access);
    printf("[STATS] page_faults = %lu\n",  g_paging_stats.page_faults);
    printf("[STATS] swap_in = %lu\n",      g_paging_stats.swap_in);
    printf("[STATS] swap_out = %lu\n",     g_paging_stats.swap_out);
    printf("[STATS] pt_bytes = %llu\n",
           (unsigned long long)g_paging_stats.pt_bytes);
}
