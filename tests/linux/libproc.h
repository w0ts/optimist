/* SPDX-License-Identifier: GPL-3.0-only */
/* macOS's <libproc.h> as the host tests use it (instruction counts of a loop: proc_pid_rusage), for a Linux host
 * (tests/run_tests.sh puts tests/linux on the include path off macOS). No count there: the call fails, and every test
 * then prints no cost line (its counts are information, never a check). */
#ifndef OPTIMIST_TESTS_LIBPROC_H
#define OPTIMIST_TESTS_LIBPROC_H
#include <stdint.h>
#include <sys/types.h>

#define RUSAGE_INFO_V4 4
typedef void *rusage_info_t;
struct rusage_info_v4 {
    uint64_t ri_instructions;
};
static inline int proc_pid_rusage(int pid, int flavor, rusage_info_t *buffer)
{
    (void)pid, (void)flavor, (void)buffer;
    return -1;
}
#endif
