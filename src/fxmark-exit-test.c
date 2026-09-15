// SPDX-License-Identifier: MIT
#include "bench.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static int exit_mode;

static void fail_after_work(void)
{
	if (exit_mode == 1)
		_Exit(7);
	raise(SIGTERM);
}

static int completed_work(struct worker *worker)
{
	if (worker->id != 0 && exit_mode != 0)
		assert(atexit(fail_after_work) == 0);
	worker->works = 1;
	return 0;
}

int main(void)
{
	for (exit_mode = 0; exit_mode != 3; ++exit_mode) {
		struct bench *bench = alloc_bench(2, 0);
		assert(bench != NULL);
		bench->duration = 1;
		bench->ops.main_work = completed_work;
		run_bench(bench);
		assert(bench->workers[0].ret == 0);
		assert(bench->workers[1].works == 1);
		assert(bench->workers[1].ret == (exit_mode ? EIO : 0));
		alarm(0);
		munmap(bench, sizeof(*bench) + 2 * sizeof(struct worker));
	}
	puts("Fxmark worker exit tests passed");
	return 0;
}
