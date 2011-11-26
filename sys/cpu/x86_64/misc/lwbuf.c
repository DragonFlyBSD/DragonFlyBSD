/*
 * Copyright (c) 2010 by The DragonFly Project and Samuel J. Greear.
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Samuel J. Greear <sjg@thesjg.com>
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

#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/objcache.h>
#include <sys/systm.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <cpu/lwbuf.h>
#include <machine/param.h>

#if 0
/*
 * NO LONGER USED - See inlines
 */

static void lwbuf_init(void *);
SYSINIT(sock_lwb, SI_BOOT2_MACHDEP, SI_ORDER_ANY, lwbuf_init, NULL);

static struct objcache *lwbuf_cache;

MALLOC_DEFINE(M_LWBUF, "lwbuf", "Lightweight buffers");
struct objcache_malloc_args lwbuf_malloc_args = { sizeof(struct lwbuf), M_LWBUF };


static boolean_t
lwbuf_cache_ctor(void *obj, void *pdata, int ocflags)
{
    struct lwbuf *lwb = (struct lwbuf *)obj;

    lwb->m = NULL;
    lwb->kva = 0;

    return (TRUE);
}

static void
lwbuf_init(void *arg)
{
    lwbuf_cache = objcache_create("lwbuf", 0, 0,
	lwbuf_cache_ctor, NULL, NULL,
	objcache_malloc_alloc, objcache_malloc_free,
	&lwbuf_malloc_args);
}

#endif

#if 0
/*
 * NO LONGER USED - See inlines
 */

struct lwbuf *
lwbuf_alloc(vm_page_t m, struct lwbuf *lwb_cache)
{
    struct lwbuf *lwb = lwb_cache;

    lwb->m = m;
    lwb->kva = PHYS_TO_DMAP(VM_PAGE_TO_PHYS(lwb->m));

    return (lwb);
}

void
lwbuf_free(struct lwbuf *lwb)
{
    lwb->m = NULL;	/* safety */
}

#endif
