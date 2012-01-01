/*
 * SYS/MPLOCK2.H
 *
 * Implement the MP lock.  Note that debug operations
 */
#ifndef _SYS_MPLOCK2_H_
#define _SYS_MPLOCK2_H_

#include <machine/atomic.h>
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

#define MP_LOCK_HELD()		LWKT_TOKEN_HELD_EXCL(&mp_token)
#define ASSERT_MP_LOCK_HELD()	ASSERT_LWKT_TOKEN_HELD_EXCL(&mp_token)

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
