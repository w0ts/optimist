/* host stub for fm1-base plat.h: PLAT_STATS_APP builds call plat_stats_enable (core/dbgstats.h) and the stack read-outs (system/stackmark.h) */
#include <stdint.h>
#define PLAT_STK_MAIN 0
#define PLAT_STK_IRQ 1
#define PLAT_STK_C1 2
static void plat_stats_enable(int on) { (void)on; }
static int32_t plat_stack_least_ever(int which) { (void)which; return -1; }
