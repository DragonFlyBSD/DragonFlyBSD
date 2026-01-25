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

#define BSRCPUMASK(val)		((val).ary[3] ? 192 + bsrq((val).ary[3]) : \
				((val).ary[2] ? 128 + bsrq((val).ary[2]) : \
				((val).ary[1] ? 64 + bsrq((val).ary[1]) : \
						bsrq((val).ary[0]))))

#define BSFCPUMASK(val)		((val).ary[0] ? (int)__builtin_ctzll((val).ary[0]) : \
				((val).ary[1] ? 64 + (int)__builtin_ctzll((val).ary[1]) : \
				((val).ary[2] ? 128 + (int)__builtin_ctzll((val).ary[2]) : \
				192 + (int)__builtin_ctzll((val).ary[3]))))

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

#define CPUMASK_ASSBMASK(mask, i) \
	do { \
		if ((i) < 64) { \
			(mask).ary[0] = CPUMASK_SIMPLE(i) - 1; \
			(mask).ary[1] = 0; \
			(mask).ary[2] = 0; \
			(mask).ary[3] = 0; \
		} else if ((i) < 128) { \
			(mask).ary[0] = (__uint64_t)-1; \
			(mask).ary[1] = CPUMASK_SIMPLE((i) - 64) - 1; \
			(mask).ary[2] = 0; \
			(mask).ary[3] = 0; \
		} else if ((i) < 192) { \
			(mask).ary[0] = (__uint64_t)-1; \
			(mask).ary[1] = (__uint64_t)-1; \
			(mask).ary[2] = CPUMASK_SIMPLE((i) - 128) - 1; \
			(mask).ary[3] = 0; \
		} else { \
			(mask).ary[0] = (__uint64_t)-1; \
			(mask).ary[1] = (__uint64_t)-1; \
			(mask).ary[2] = (__uint64_t)-1; \
			(mask).ary[3] = CPUMASK_SIMPLE((i) - 192) - 1; \
		} \
	} while (0)

#define CPUMASK_TESTNZERO(val)		((val).ary[0] != 0 || \
					 (val).ary[1] != 0 || \
					 (val).ary[2] != 0 || \
					 (val).ary[3] != 0)

#define CPUMASK_CMPMASKNEQ(val1, val2)	((val1).ary[0] != (val2).ary[0] || \
					 (val1).ary[1] != (val2).ary[1] || \
					 (val1).ary[2] != (val2).ary[2] || \
					 (val1).ary[3] != (val2).ary[3])

#define CPUMASK_CMPMASKEQ(val1, val2)	((val1).ary[0] == (val2).ary[0] && \
					 (val1).ary[1] == (val2).ary[1] && \
					 (val1).ary[2] == (val2).ary[2] && \
					 (val1).ary[3] == (val2).ary[3])

#define CPUMASK_LOWMASK(val)		((val).ary[0])

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

#define CPUMASK_TESTMASK(val1, val2)	(((val1).ary[0] & (val2).ary[0]) || \
					 ((val1).ary[1] & (val2).ary[1]) || \
					 ((val1).ary[2] & (val2).ary[2]) || \
					 ((val1).ary[3] & (val2).ary[3]))

#define CPUMASK_ANDMASK(mask, val)		do { \
					(mask).ary[0] &= (val).ary[0]; \
					(mask).ary[1] &= (val).ary[1]; \
					(mask).ary[2] &= (val).ary[2]; \
					(mask).ary[3] &= (val).ary[3]; \
					} while (0)

#define ATOMIC_CPUMASK_ORBIT(mask, i)	CPUMASK_ORBIT(mask, i)
#define ATOMIC_CPUMASK_NANDBIT(mask, i)	CPUMASK_NANDBIT(mask, i)
#define ATOMIC_CPUMASK_ORMASK(mask, val)	CPUMASK_ORMASK(mask, val)
#define ATOMIC_CPUMASK_NANDMASK(mask, val)	CPUMASK_NANDMASK(mask, val)

#define CPUMASK_ORBIT(mask, i)		((mask).ary[((i) >> 6) & 3] |= \
					 CPUMASK_SIMPLE((i) & 63))

#define CPUMASK_NANDBIT(mask, i)	((mask).ary[((i) >> 6) & 3] &= \
					 ~CPUMASK_SIMPLE((i) & 63))

#define CPUMASK_ASSNBMASK(mask, i)			\
	do {							\
		if ((i) < 64) {					\
			(mask).ary[0] = ~(CPUMASK_SIMPLE(i) - 1); \
			(mask).ary[1] = (__uint64_t)-1; \
			(mask).ary[2] = (__uint64_t)-1; \
			(mask).ary[3] = (__uint64_t)-1; \
		} else if ((i) < 128) {			\
			(mask).ary[0] = 0; \
			(mask).ary[1] = ~(CPUMASK_SIMPLE((i) - 64) - 1); \
			(mask).ary[2] = (__uint64_t)-1; \
			(mask).ary[3] = (__uint64_t)-1; \
		} else if ((i) < 192) {			\
			(mask).ary[0] = 0; \
			(mask).ary[1] = 0; \
			(mask).ary[2] = ~(CPUMASK_SIMPLE((i) - 128) - 1); \
			(mask).ary[3] = (__uint64_t)-1; \
		} else {					\
			(mask).ary[0] = 0; \
			(mask).ary[1] = 0; \
			(mask).ary[2] = 0; \
			(mask).ary[3] = ~(CPUMASK_SIMPLE((i) - 192) - 1); \
		}						\
	} while (0)

#define CPUMASK_NANDMASK(mask1, mask2)			\
	do {							\
		(mask1).ary[0] &= ~(mask2).ary[0];	\
		(mask1).ary[1] &= ~(mask2).ary[1];	\
		(mask1).ary[2] &= ~(mask2).ary[2];	\
		(mask1).ary[3] &= ~(mask2).ary[3];	\
	} while (0)

#define CPUMASK_ORMASK(mask, val)			\
	do {							\
		(mask).ary[0] |= (val).ary[0];		\
		(mask).ary[1] |= (val).ary[1];		\
		(mask).ary[2] |= (val).ary[2];		\
		(mask).ary[3] |= (val).ary[3];		\
	} while (0)

#define CPUMASK_INVMASK(mask)		do { \
					(mask).ary[0] ^= -1L; \
					(mask).ary[1] ^= -1L; \
					(mask).ary[2] ^= -1L; \
					(mask).ary[3] ^= -1L; \
					} while (0)

#endif /* !_CPU_CPUMASK_H_ */
