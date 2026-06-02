// SPDX-License-Identifier: MIT
/**
 * Microbenchmark
 *   FC. PROCESS = {create/delete files in 4KB at /test}
 *       - TEST: inode alloc/dealloc, block alloc/dealloc,
 *	        dentry insert/delete, block map insert/delete
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include "fxmark.h"
#include "util.h"
#include "rdtsc.h"

static void set_test_file(struct worker *worker,
                          uint64_t file_id, char *test_file)
{
    struct fx_opt *fx_opt = fx_opt_worker(worker);
    sprintf(test_file, "%s/u_sh_file_rm-%d-%" PRIu64 ".dat",
            fx_opt->root, worker->id, file_id);
}

static int create_test_file(struct worker *worker, uint64_t file_id)
{
    char path[PATH_MAX];
    int fd;

    set_test_file(worker, file_id, path);
    fd = open(path, O_CREAT | O_RDWR, S_IRWXU);
    if (fd == -1)
        return errno;
    close(fd);
    return 0;
}

static int pre_work(struct worker *worker)
{
    struct bench *bench =  worker->bench;
    uint64_t file_id, file_count;
    int i, rc = 0;

    if (worker->id != 0)
        return 0;

    /* a leader serializes pre-work across workers to avoid setup contention */
    file_count = fxmark_fixed_work_items_for_worker(
        bench,
        "FXMARK_FIXED_FILE_COUNT_TOTAL",
        FXMARK_FIXED_FILE_COUNT_TOTAL,
        "FXMARK_FIXED_FILE_COUNT_PER_WORKER");
    for (file_id = 0; file_id < file_count; ++file_id) {
        for (i = 0; i < bench->ncpu; ++i) {
            struct worker *w = &bench->workers[i];
            rc = create_test_file(w, w->private[0]);
            if (rc == ENOSPC) {
                rc = 0;
                goto out;
            }
            if (rc)
                goto err_out;
            ++w->private[0];
        }
    }
 out:
    return rc;
 err_out:
    bench->stop = 1;
    goto out;
}

static int pre_work_enospc(struct worker *worker)
{
    struct bench *bench =  worker->bench;
    int i, rc = 0;

    if (worker->id != 0)
        return 0;

    /* a leader serializes pre-work across workers to avoid setup contention */
    for (;;) {
        for (i = 0; i < bench->ncpu; ++i) {
            struct worker *w = &bench->workers[i];
            rc = create_test_file(w, w->private[0]);
            if (rc == ENOSPC) {
                rc = 0;
                goto out;
            }
            if (rc)
                goto err_out;
            ++w->private[0];
        }
    }
 err_out:
    bench->stop = 1;
 out:
    return rc;
}

static int main_work(struct worker *worker)
{
    struct bench *bench = worker->bench;
    uint64_t iter;
    int rc = 0;
    for (iter = 0; iter < worker->private[0] && !bench->stop; ++iter) {
        char file[PATH_MAX];
        set_test_file(worker, iter, file);
        if (unlink(file))
            goto err_out;
    }
 out:
    worker->works = (double)iter;
    return rc;
 err_out:
    bench->stop = 1;
    rc = errno;
    goto out;
}

struct bench_operations u_sh_file_rm_ops = {
    .pre_work  = pre_work,
    .main_work = main_work,
};

struct bench_operations u_sh_file_rm_enospc_ops = {
    .pre_work  = pre_work_enospc,
    .main_work = main_work,
};
