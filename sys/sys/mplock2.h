/*
 * SYS/MPLOCK2.H
 *
 * Implement the MP lock.  Note that debug operations
 */
#ifndef _SYS_MPLOCK2_H_
#define _SYS_MPLOCK2_H_

#ifndef _MACHINE_ATOMIC_H_
#include <machine/atomic.h>
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>
#endif

#ifdef SMP

/*
 * NOTE: try_mplock()/lwkt_trytoken() return non-zero on success.
 */
#define get_mplock()		lwkt_gettoken(&mp_token)
#define try_mplock()		lwkt_trytoken(&mp_token)
#define rel_mplock()		lwkt_reltoken(&mp_token)
#define get_mplock_count(td)	lwkt_cnttoken(&mp_token, td)

void cpu_get_initial_mplock(void);
void handle_cpu_contention_mask(void);

extern cpumask_t cpu_contention_mask;

/*
 * A cpu wanted the MP lock but could not get it.  This function is also
 * called directly from the LWKT scheduler.
 *
 * Reentrant, may be called even if the cpu is already contending the MP
 * lock.
 */
static __inline
void
set_cpu_contention_mask(globaldata_t gd)
{
	atomic_set_cpumask(&cpu_contention_mask, gd->gd_cpumask);
}

/*
 * A cpu is no longer contending for the MP lock after previously contending
 * for it.
 *
 * Reentrant, may be called even if the cpu was not previously contending
 * the MP lock.
 */
static __inline
void
clr_cpu_contention_mask(globaldata_t gd)
{
	atomic_clear_cpumask(&cpu_contention_mask, gd->gd_cpumask);
}

#define MP_LOCK_HELD()		LWKT_TOKEN_HELD(&mp_token)
#define ASSERT_MP_LOCK_HELD()	ASSERT_LWKT_TOKEN_HELD(&mp_token)


#else

/*
 * UNI-PROCESSOR BUILD - Degenerate case macros
 */
#define	get_mplock()
#define	rel_mplock()
#define try_mplock()		1
#define owner_mplock()		0
#define MP_LOCK_HELD(gd)	1
#define ASSERT_MP_LOCK_HELD(td)

#endif

#endif
