/*
 * hq_worker_main.c — entry point for the dedicated hq_worker helper
 * process.
 *
 * This binary is never invoked directly by a user: it is exec()'d by
 * hq_adapter.c's hq_exec_worker_or_exit() right after fork(), as a
 * fresh process image, to run exactly one HQ conversion job and then
 * exit. See hq_adapter.c's file header comment for the full rationale
 * (fork()-in-multithreaded-process malloc-arena-lock hazard).
 *
 * argv layout (all set by hq_exec_worker_or_exit(), never by a human):
 *   argv[0] = path to this binary
 *   argv[1] = status_write_fd (decimal)
 *   argv[2] = control_read_fd (decimal)
 *   argv[3] = input_path
 *   argv[4] = work_dir
 *   argv[5] = codec
 *   argv[6] = preset
 */

#include <stdlib.h>

#include "hq_adapter.h"

int main(int argc, char **argv)
{
    if (argc != 7)
        return 1;

    int status_write_fd = atoi(argv[1]);
    int control_read_fd = atoi(argv[2]);

    /* Never returns. */
    hq_worker_main_entry(status_write_fd, control_read_fd,
                          argv[3], argv[4], argv[5], argv[6]);

    return 0; /* unreachable */
}
