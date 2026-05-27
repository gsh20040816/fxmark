/* SPDX-License-Identifier: MIT */
#ifndef __UTIL_H__
#define __UTIL_H__

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "bench.h"

int mkdir_p(const char *path);

#define FXMARK_FIXED_FILE_COUNT_TOTAL 983040ULL
#define FXMARK_FIXED_TRUNCATE_PAGE_TOTAL 1048576ULL

inline static uint64_t fxmark_u64_env_or(const char *name, uint64_t fallback)
{
	const char *value = getenv(name);
	char *end = NULL;
	unsigned long long parsed;

	if (!value || !*value)
		return fallback;

	errno = 0;
	parsed = strtoull(value, &end, 10);
	if (errno || end == value || *end != '\0' || parsed == 0)
		return fallback;

	return (uint64_t)parsed;
}

inline static uint64_t fxmark_fixed_work_items_for_worker(
	struct bench *bench,
	const char *specific_total_env,
	uint64_t default_total,
	const char *specific_per_worker_env)
{
	uint64_t total;
	uint64_t per_worker;
	uint64_t ncpu;

	per_worker = fxmark_u64_env_or(specific_per_worker_env, 0);
	if (per_worker)
		return per_worker;

	per_worker = fxmark_u64_env_or("FXMARK_FIXED_WORK_ITEMS_PER_WORKER", 0);
	if (per_worker)
		return per_worker;

	total = fxmark_u64_env_or("FXMARK_FIXED_WORK_ITEMS_TOTAL", default_total);
	total = fxmark_u64_env_or(specific_total_env, total);
	ncpu = bench && bench->ncpu > 0 ? (uint64_t)bench->ncpu : 1;
	per_worker = total / ncpu;
	return per_worker ? per_worker : 1;
}

inline static unsigned int pseudo_random(unsigned int x_n)
{
	/* 
	 * NOTE: linear congruential generator 
	 *   http://en.wikipedia.org/wiki/Linear_congruential_generator 
	 */
	return 1103515245 * x_n + 12345;
}

#endif /* __UTIL_H__ */
