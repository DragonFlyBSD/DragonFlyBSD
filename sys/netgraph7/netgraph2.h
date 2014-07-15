/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#ifndef _NETGRAPH_NETGRAPH2_H_
#define _NETGRAPH_NETGRAPH2_H_

/*
 * This header implements the netgraph kernel public inline functions.
 */

#ifndef _KERNEL
#error "kernel only header file"
#endif

#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#ifndef _NETGRAPH_NETGRAPH_H_
#include <netgraph7/netgraph.h>
#endif

extern struct lwkt_port *ng_msgport[MAXCPU];

/*
 * Return the message port for the netgraph message servicing
 * thread for a particular cpu.
 */
static __inline lwkt_port_t
ng_cpuport(int cpu)
{
	KKASSERT(cpu >= 0 && cpu < ncpus);
	return ng_msgport[cpu];
}

/*
 * Grab a reference on the item.
 */
static __inline void
ng_ref_item(item_p item)
{
	refcount_acquire(&item->refs);
}

/*
 * Release a reference on the item.
 *
 * When the last reference is released, call the item's callback function if
 * there is one and free the item.
 */
static __inline void
ng_unref_item(item_p item, int error)
{
	if (refcount_release(&item->refs)) {
		/* We have released the last reference */
		if (item->apply != NULL) {
			KKASSERT(item->apply->apply != NULL);
			(*item->apply->apply)(item->apply->context, error);
			ng_free_apply(item->apply);
			item->apply = NULL;
		}
		ng_free_item(item);
	}
}

#endif	/* _NETGRAPH_NETGRAPH2_H_ */
