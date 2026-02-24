/*
 * Copyright (c) 2005-2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
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

#ifdef _KERNEL
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
#endif	/* _KERNEL */

#endif	/* !_SYS_SERIALIZE_H_ */
