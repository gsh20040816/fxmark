// SPDX-License-Identifier: MIT
/**
 * Nanobenchmark: Read operation
 *   RD. PROCESS = {read entries of the shared directory}
 */	      
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include "fxmark.h"
#include "util.h"

#ifndef CXLFS_FXMARK_DIRECTORY_PRECREATE_FILES
#define CXLFS_FXMARK_DIRECTORY_PRECREATE_FILES 0
#endif
#ifndef CXLFS_ENABLE_FXMARK_PRECREATE_REPORT
#define CXLFS_ENABLE_FXMARK_PRECREATE_REPORT 0
#endif
#ifndef CXLFS_FXMARK_DIRECTORY_SCAN_PASSES
#define CXLFS_FXMARK_DIRECTORY_SCAN_PASSES 0
#endif

static int stop_pre_work;

static void set_test_root(struct worker *worker, char *test_root)
{
	struct fx_opt *fx_opt = fx_opt_worker(worker);
	sprintf(test_root, "%s", fx_opt->root);
}

static void set_test_file(struct worker *worker, 
			  uint64_t file_id, char *test_file)
{
	struct fx_opt *fx_opt = fx_opt_worker(worker);
	sprintf(test_file, "%s/n_shdir_rd-%d-%" PRIu64 ".dat",
		fx_opt->root, worker->id, file_id);
}

#if CXLFS_FXMARK_DIRECTORY_PRECREATE_FILES == 0
static void sighandler(int x)
{
	stop_pre_work = 1;
}
#endif

static int pre_work(struct worker *worker)
{
	struct bench *bench = worker->bench;
	char path[PATH_MAX];
	int fd, rc = 0;

#if CXLFS_FXMARK_DIRECTORY_SCAN_PASSES > 0
	/* Shared-directory entry counts and completion are single-worker checks. */
	if (bench->ncpu != 1 || bench->nbg != 0)
		return EINVAL;
#endif

	/* perform pre_work for bench->duration */
#if CXLFS_FXMARK_DIRECTORY_PRECREATE_FILES == 0
	if (signal(SIGALRM, sighandler) == SIG_ERR) {
		rc = errno;
		goto err_out;
	}
	alarm(bench->duration);
#endif

	/* create private directory */
	set_test_root(worker, path);
	rc = mkdir_p(path);
	if (rc) goto err_out;

	/* create files at the private directory */
	for (; CXLFS_FXMARK_DIRECTORY_PRECREATE_FILES
		? worker->private[0] < CXLFS_FXMARK_DIRECTORY_PRECREATE_FILES
		: !stop_pre_work; ++worker->private[0]) {
		set_test_file(worker, worker->private[0], path);
		if ((fd = open(path, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
			if (errno == ENOSPC && CXLFS_FXMARK_DIRECTORY_PRECREATE_FILES == 0)
				goto out;
			goto err_out;
		}
		close(fd);
	}
out:
#if CXLFS_ENABLE_FXMARK_PRECREATE_REPORT
	fprintf(stderr, "# precreate MRDM worker=%d files=%" PRIu64 "\n",
		worker->id, worker->private[0]);
#endif
	return rc; 
err_out:
	rc = errno;
	goto out;
}

static int main_work(struct worker *worker)
{
	struct bench *bench = worker->bench;
	char dir_path[PATH_MAX];
	DIR *dir;
	struct dirent entry;
	struct dirent *result;
	uint64_t iter = 0;
	uint64_t opens = 0, completed = 0;
	int rc = 0;

	set_test_root(worker, dir_path);
	while (!bench->stop && (!CXLFS_FXMARK_DIRECTORY_SCAN_PASSES
		|| completed < CXLFS_FXMARK_DIRECTORY_SCAN_PASSES)) {
		++opens;
		dir = opendir(dir_path);
		if (!dir) goto err_out;
		for (; !bench->stop;) {
			rc = readdir_r(dir, &entry, &result);
			if (rc) {
				closedir(dir);
				goto out;
			}
			if (!result) {
				++completed;
				break;
			}
			++iter;
		}
		closedir(dir);
	}
#if CXLFS_FXMARK_DIRECTORY_SCAN_PASSES > 0
	if (completed != CXLFS_FXMARK_DIRECTORY_SCAN_PASSES
		|| iter != (worker->private[0] + 2) * CXLFS_FXMARK_DIRECTORY_SCAN_PASSES)
		rc = ETIMEDOUT;
#endif
out:
#if CXLFS_ENABLE_FXMARK_PRECREATE_REPORT
	fprintf(stderr, "# scans MRDM worker=%d opens=%" PRIu64 " complete=%" PRIu64 " entries=%" PRIu64 "\n",
		worker->id, opens, completed, iter);
#endif
	bench->stop = 1;
	worker->works = (double)iter;
	return rc;
err_out:
	rc = errno;
	goto out;
}

struct bench_operations n_shdir_rd_ops = {
	.pre_work  = pre_work, 
	.main_work = main_work,
};
