/*
 * Placeholder cpumask definitions for arm64.
 */

#ifndef _CPU_CPUMASK_H_
#define	_CPU_CPUMASK_H_

#include <sys/types.h>

typedef struct {
	__uint64_t	ary[4];
} __cpumask_t;

typedef __cpumask_t cpumask_t;

#define CPUMASK_ELEMENTS	4

#define CPUMASK_INITIALIZER_ALLONES	{ .ary = { (__uint64_t)-1, \
					  (__uint64_t)-1, \
					  (__uint64_t)-1, \
					  (__uint64_t)-1 } }
#define CPUMASK_INITIALIZER_ONLYONE	{ .ary = { 1, 0, 0, 0 } }

#define CPUMASK_SIMPLE(cpu)	((__uint64_t)1 << (cpu))

#endif /* !_CPU_CPUMASK_H_ */
