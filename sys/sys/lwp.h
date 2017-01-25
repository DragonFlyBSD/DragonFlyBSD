#ifndef _SYS_LWP_H_
#define _SYS_LWP_H_

#include <sys/types.h>

/*
 * Parameters for the creation of a new lwp.
 */
struct lwp_params {
	void (*lwp_func)(void *); /* Function to start execution */
	void *lwp_arg;		/* Parameter to this function */
	void *lwp_stack;	/* Stack address to use */
	lwpid_t *lwp_tid1;	/* Address to copy out new tid */
	lwpid_t *lwp_tid2;	/* Same */
};

#if !defined(_KERNEL) || defined(_KERNEL_VIRTUAL)

#include <machine/cpumask.h>

__BEGIN_DECLS

struct rtprio;

int	lwp_create(struct lwp_params *);
int	lwp_create2(struct lwp_params *, const cpumask_t *);
#ifndef _LWP_GETTID_DECLARED
lwpid_t	lwp_gettid(void);
#define _LWP_GETTID_DECLARED
#endif
#ifndef _LWP_SETNAME_DECLARED
int	lwp_setname(lwpid_t, const char *);
#define _LWP_SETNAME_DECLARED
#endif
#ifndef _LWP_RTPRIO_DECLARED
int	lwp_rtprio(int, pid_t, lwpid_t, struct rtprio *);
#define _LWP_RTPRIO_DECLARED
#endif
int	lwp_setaffinity(pid_t, lwpid_t, const cpumask_t *);
int	lwp_getaffinity(pid_t, lwpid_t, cpumask_t *);
#ifndef _LWP_KILL_DECLARED
int	lwp_kill(pid_t, lwpid_t, int);
#define _LWP_KILL_DECLARED
#endif

__END_DECLS

#endif	/* !_KERNEL */

#endif	/* !_SYS_LWP_H_ */
