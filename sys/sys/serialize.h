/*
 * Provides a fast serialization facility that will serialize across blocking
 * conditions.  This facility is very similar to a lock but much faster for
 * the common case.  It utilizes the atomic_intr_*() functions to acquire
 * and release the serializer and token functions to block.
 *
 * This API is designed to be used whenever low level serialization is
 * required.  Unlike tokens this serialization is not safe from deadlocks
 * nor is it recursive, and care must be taken when using it. 
 *
 * $DragonFly: src/sys/sys/serialize.h,v 1.9 2008/05/14 11:59:24 sephe Exp $
 */

#ifndef _SYS_SERIALIZE_H_
#define _SYS_SERIALIZE_H_

#ifndef _KERNEL
#error "kernel only header file"
#endif

#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif

#ifndef _SYS_SYSTM_H_
#include <sys/systm.h>
#endif

#ifndef _MACHINE_STDINT_H_
#include <machine/stdint.h>
#endif

struct thread;

struct lwkt_serialize {
    __atomic_intr_t	interlock;
    struct thread	*last_td;
    unsigned int	sleep_cnt;
    unsigned int	tryfail_cnt;
    unsigned int	enter_cnt;
    unsigned int	try_cnt;
};

#ifdef INVARIANTS
/*
 * Note that last_td is only maintained when INVARIANTS is turned on,
 * so this check is only useful as part of a [K]KASSERT.
 */
#define IS_SERIALIZED(ss)		((ss)->last_td == curthread)
#endif

#define ASSERT_SERIALIZED(ss)		KKASSERT(IS_SERIALIZED((ss)))
#define ASSERT_NOT_SERIALIZED(ss)	KKASSERT(!IS_SERIALIZED((ss)))

typedef struct lwkt_serialize *lwkt_serialize_t;

void lwkt_serialize_init(lwkt_serialize_t);
void lwkt_serialize_enter(lwkt_serialize_t);
#ifdef SMP
void lwkt_serialize_adaptive_enter(lwkt_serialize_t);
#endif
int lwkt_serialize_try(lwkt_serialize_t);
void lwkt_serialize_exit(lwkt_serialize_t);
void lwkt_serialize_handler_disable(lwkt_serialize_t);
void lwkt_serialize_handler_enable(lwkt_serialize_t);
void lwkt_serialize_handler_call(lwkt_serialize_t, void (*)(void *, void *), void *, void *);
int lwkt_serialize_handler_try(lwkt_serialize_t, void (*)(void *, void *), void *, void *);

static __inline void
lwkt_serialize_array_enter(lwkt_serialize_t *_arr, int _arrcnt, int _s)
{
	KASSERT(_s < _arrcnt, ("nothing to be serialized\n"));
	while (_s < _arrcnt)
		lwkt_serialize_enter(_arr[_s++]);
}

static __inline int
lwkt_serialize_array_try(lwkt_serialize_t *_arr, int _arrcnt, int _s)
{
	int _i;

	KASSERT(_s < _arrcnt, ("nothing to be serialized\n"));
	for (_i = _s; _i < _arrcnt; ++_i) {
		if (!lwkt_serialize_try(_arr[_i])) {
			while (--_i >= _s)
				lwkt_serialize_exit(_arr[_i]);
			return 0;
		}
	}
	return 1;
}

static __inline void
lwkt_serialize_array_exit(lwkt_serialize_t *_arr, int _arrcnt, int _s)
{
	KASSERT(_arrcnt > _s, ("nothing to be deserialized\n"));
	while (--_arrcnt >= _s)
		lwkt_serialize_exit(_arr[_arrcnt]);
}

#endif	/* !_SYS_SERIALIZE_H_ */
