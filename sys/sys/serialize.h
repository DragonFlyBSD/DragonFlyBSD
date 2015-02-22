/*
 * Provides a fast serialization facility that will serialize across blocking
 * conditions.  This facility is very similar to a lock but much faster for
 * the common case.  It utilizes the atomic_intr_*() functions to acquire
 * and release the serializer and token functions to block.
 *
 * This API is designed to be used whenever low level serialization is
 * required.  Unlike tokens this serialization is not safe from deadlocks
 * nor is it recursive, and care must be taken when using it. 
 */

#ifndef _SYS_SERIALIZE_H_
#define _SYS_SERIALIZE_H_

#include <machine/stdint.h>

struct thread;

struct lwkt_serialize {
    __atomic_intr_t	interlock;
    struct thread	*last_td;
};

#define LWKT_SERIALIZE_INITIALIZER      { 0, NULL }

#define IS_SERIALIZED(ss)		((ss)->last_td == curthread)
#define ASSERT_SERIALIZED(ss)		KKASSERT(IS_SERIALIZED((ss)))
#define ASSERT_NOT_SERIALIZED(ss)	KKASSERT(!IS_SERIALIZED((ss)))

typedef struct lwkt_serialize *lwkt_serialize_t;

void lwkt_serialize_init(lwkt_serialize_t);
void lwkt_serialize_enter(lwkt_serialize_t);
void lwkt_serialize_adaptive_enter(lwkt_serialize_t);
int lwkt_serialize_try(lwkt_serialize_t);
void lwkt_serialize_exit(lwkt_serialize_t);
void lwkt_serialize_handler_disable(lwkt_serialize_t);
void lwkt_serialize_handler_enable(lwkt_serialize_t);
void lwkt_serialize_handler_call(lwkt_serialize_t, void (*)(void *, void *), void *, void *);
int lwkt_serialize_handler_try(lwkt_serialize_t, void (*)(void *, void *), void *, void *);

#endif	/* !_SYS_SERIALIZE_H_ */
