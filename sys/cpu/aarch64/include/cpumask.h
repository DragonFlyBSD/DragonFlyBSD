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

#define CPUMASK_ADDR(mask, cpu)	(&(mask).ary[((cpu) >> 6) & 3])

#define CPUMASK_ASSZERO(mask)		do { \
					(mask).ary[0] = 0; \
					(mask).ary[1] = 0; \
					(mask).ary[2] = 0; \
					(mask).ary[3] = 0; \
					} while (0)

#define CPUMASK_ASSALLONES(mask)	do { \
					(mask).ary[0] = (__uint64_t)-1; \
					(mask).ary[1] = (__uint64_t)-1; \
					(mask).ary[2] = (__uint64_t)-1; \
					(mask).ary[3] = (__uint64_t)-1; \
					} while (0)

#define CPUMASK_ASSBIT(mask, i)		do { \
					CPUMASK_ASSZERO(mask); \
					(mask).ary[((i) >> 6) & 3] = CPUMASK_SIMPLE((i) & 63); \
					} while (0)

#define CPUMASK_TESTNZERO(val)		((val).ary[0] != 0 || \
					 (val).ary[1] != 0 || \
					 (val).ary[2] != 0 || \
					 (val).ary[3] != 0)

#define CPUMASK_TESTZERO(val)		((val).ary[0] == 0 && \
					 (val).ary[1] == 0 && \
					 (val).ary[2] == 0 && \
					 (val).ary[3] == 0)

#define CPUMASK_ISUP(val)		((val).ary[0] == 1 && \
					 (val).ary[1] == 0 && \
					 (val).ary[2] == 0 && \
					 (val).ary[3] == 0)

#define CPUMASK_TESTBIT(val, i)		((val).ary[((i) >> 6) & 3] & \
					 CPUMASK_SIMPLE((i) & 63))

#define CPUMASK_ANDMASK(mask, val)		do { \
					(mask).ary[0] &= (val).ary[0]; \
					(mask).ary[1] &= (val).ary[1]; \
					(mask).ary[2] &= (val).ary[2]; \
					(mask).ary[3] &= (val).ary[3]; \
					} while (0)

#define ATOMIC_CPUMASK_ORBIT(mask, i)	CPUMASK_ORBIT(mask, i)

#define CPUMASK_ORBIT(mask, i)		((mask).ary[((i) >> 6) & 3] |= \
					 CPUMASK_SIMPLE((i) & 63))

#define CPUMASK_NANDBIT(mask, i)	((mask).ary[((i) >> 6) & 3] &= \
					 ~CPUMASK_SIMPLE((i) & 63))

#endif /* !_CPU_CPUMASK_H_ */
