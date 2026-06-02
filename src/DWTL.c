// SPDX-License-Identifier: MIT
/**
 * Microbenchmark
 *   FC. PROCESS = {create/delete files in 4KB at /test}
 *       - TEST: inode alloc/dealloc, block alloc/dealloc,
 *	        dentry insert/delete, block map insert/delete
 */
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdlib.h>
#include "fxmark.h"
#include "util.h"
#include "rdtsc.h"

static void set_test_file(struct worker *worker,
                          char *test_file)
{
    struct fx_opt *fx_opt = fx_opt_worker(worker);
    sprintf(test_file, "%s/u_file_tr-%d.dat",
            fx_opt->root, worker->id);
}

static int open_test_file(struct worker *worker, int flags)
{
    char path[PATH_MAX];

    set_test_file(worker, path);
    return open(path, flags | O_RDWR | O_LARGEFILE, S_IRWXU);
}

static int pre_work_impl(struct worker *worker, int until_enospc)
{
    struct bench *bench =  worker->bench;
    int fds[bench->ncpu];
    int fd=-1, i, rc = 0;
    char *page = NULL;
    uint64_t page_id, page_count;
    volatile uint64_t *pre_done = &bench->workers[0].private[2];

    for (i = 0; i < bench->ncpu; ++i)
      fds[i] = -1;

    if(posix_memalign((void **)&page, PAGE_SIZE, PAGE_SIZE))
      goto err_out;
    if (!page)
      goto err_out;

    if (worker->id == 0) {
      for (i = 0; i < bench->ncpu; ++i) {
        struct worker *w = &bench->workers[i];
        fds[i] = open_test_file(w, O_CREAT);
        if (fds[i] == -1) {
          rc = errno;
          goto err_out;
        }
        if (bench->directio && (fcntl(fds[i], F_SETFL, O_DIRECT)==-1)) {
          rc = errno;
          goto err_out;
        }
      }

      if (until_enospc) {
        for (;;) {
          for (i = 0; i < bench->ncpu; ++i) {
            struct worker *w = &bench->workers[i];
            rc = write(fds[i], page, PAGE_SIZE);
            if (rc != PAGE_SIZE)
              rc = errno == ENOSPC ? ENOSPC : errno;
            else
              rc = 0;
            if (rc == ENOSPC) {
              rc = 0;
              goto open_for_main;
            }
            if (rc)
              goto err_out;
            ++w->private[0];
          }
        }
      } else {
        page_count = fxmark_fixed_work_items_for_worker(
            bench,
            "FXMARK_FIXED_TRUNCATE_PAGES_TOTAL",
            FXMARK_FIXED_TRUNCATE_PAGE_TOTAL,
            "FXMARK_FIXED_TRUNCATE_PAGES_PER_WORKER");
        for (page_id = 0; page_id < page_count; ++page_id) {
          for (i = 0; i < bench->ncpu; ++i) {
            struct worker *w = &bench->workers[i];
            rc = write(fds[i], page, PAGE_SIZE);
            if (rc != PAGE_SIZE)
              rc = errno == ENOSPC ? ENOSPC : errno;
            else
              rc = 0;
            if (rc == ENOSPC) {
              rc = 0;
              goto open_for_main;
            }
            if (rc)
              goto err_out;
            ++w->private[0];
          }
        }
      }
open_for_main:
      *pre_done = 1;
    } else {
      while (!*pre_done && !bench->stop)
        usleep(1000);
      if (bench->stop)
        goto err_out;
    }

    fd = open_test_file(worker, 0);
    if (fd == -1) {
      rc = errno;
      goto err_out;
    }
    goto out;
err_out:
    if (fd != -1)
      close(fd);
    bench->stop = 1;
out:
    for (i = 0; i < bench->ncpu; ++i) {
      if (fds[i] != -1)
        close(fds[i]);
    }
    /*put fd to worker's private*/
    worker->private[1] = (uint64_t)fd;
    free(page);
    return rc;
}

static int pre_work(struct worker *worker)
{
    return pre_work_impl(worker, 0);
}

static int pre_work_enospc(struct worker *worker)
{
    return pre_work_impl(worker, 1);
}
#include <string.h>

static int main_work(struct worker *worker)
{
    struct bench *bench = worker->bench;
    uint64_t iter, initial_pages, completed = 0;
    int fd, rc = 0;
    char path[PATH_MAX];
    set_test_file(worker, path);

    /*get file */
    fd = (int)worker->private[1];

    initial_pages = worker->private[0];
    if (initial_pages == 0)
      goto out;

    for (iter = initial_pages - 1; iter > 0 && !bench->stop; --iter) {
      if (ftruncate(fd, iter * PAGE_SIZE) == -1) {
        rc = errno;
        goto err_out;
      }
      ++completed;
    }
 out:
    close(fd);
    worker->works = (double)completed;
    return rc;
 err_out:
    bench->stop = 1;
    rc = errno;
    goto out;
}

struct bench_operations u_file_tr_ops = {
    .pre_work  = pre_work,
    .main_work = main_work,
};

struct bench_operations u_file_tr_enospc_ops = {
    .pre_work  = pre_work_enospc,
    .main_work = main_work,
};
