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
#include <sys/sysctl.h>
#include <sys/param.h>
#include <sys/serialize.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <cpu/lwbuf.h>
#include <machine/globaldata.h>
#include <machine/atomic.h>
#include <machine/param.h>
#include <sys/thread.h>

static void lwbuf_init(void *);
SYSINIT(sock_lwb, SI_BOOT2_MACHDEP, SI_ORDER_ANY, lwbuf_init, NULL);

static struct objcache *lwbuf_cache;

MALLOC_DEFINE(M_LWBUF, "lwbuf", "Lightweight buffers");
struct objcache_malloc_args lwbuf_malloc_args =
	    { sizeof(struct lwbuf), M_LWBUF };

/* Number of pages of KVA to allocate at boot per cpu (1MB) */
static int lwbuf_reserve_pages = 256;
static int lwbuf_count;
static int lwbuf_kva_bytes;

SYSCTL_INT(_kern_ipc, OID_AUTO, lwbuf_reserve, CTLFLAG_RD,
           &lwbuf_reserve_pages, 0,
           "Number of pre-allocated lightweight buffers");
SYSCTL_INT(_kern_ipc, OID_AUTO, lwbuf_count, CTLFLAG_RD,
	   &lwbuf_count, 0,
	   "Currently allocated lightweight buffers");
SYSCTL_INT(_kern_ipc, OID_AUTO, lwbuf_kva_bytes, CTLFLAG_RD,
	   &lwbuf_kva_bytes, 0,
	   "Currently used KVA for lightweight buffers");

static boolean_t
lwbuf_cache_ctor(void *obj, void *pdata, int ocflags)
{
    struct lwbuf *lwb = (struct lwbuf *)obj;

    lwb->m = NULL;
    lwb->cpumask = 0;
    lwb->kva = kmem_alloc_nofault(&kernel_map, PAGE_SIZE, PAGE_SIZE);
    if (lwb->kva == 0)
        return (FALSE);
    atomic_add_int(&lwbuf_kva_bytes, PAGE_SIZE);

    return (TRUE);
}

/*
 * Destructor for lwb.  Note that we must remove any pmap entries
 * created with pmap_kenter() to prevent them from being misinterpreted
 * as managed pages which would cause kernel_pmap.pm_stats.resident_count
 * to get out of whack.
 */
static void
lwbuf_cache_dtor(void *obj, void *pdata)
{
    struct lwbuf *lwb = (struct lwbuf *)obj;

    KKASSERT(lwb->kva != 0);
    pmap_kremove_quick(lwb->kva);
    kmem_free(&kernel_map, lwb->kva, PAGE_SIZE);
    lwb->kva = 0;
    atomic_add_int(&lwbuf_kva_bytes, -PAGE_SIZE);
}

static void
lwbuf_init(void *arg)
{
    lwbuf_cache = objcache_create("lwbuf", 0, 0,
        lwbuf_cache_ctor, lwbuf_cache_dtor, NULL,
        objcache_malloc_alloc, objcache_malloc_free,
        &lwbuf_malloc_args);
}

struct lwbuf *
lwbuf_alloc(vm_page_t m, struct lwbuf *lwb_dummy __unused)
{
    struct mdglobaldata *gd = mdcpu;
    struct lwbuf *lwb;

    lwb = objcache_get(lwbuf_cache, M_WAITOK);
    KKASSERT(lwb->m == NULL);
    lwb->m = m;
    lwb->cpumask = gd->mi.gd_cpumask;
    pmap_kenter_quick(lwb->kva, m->phys_addr);
    atomic_add_int(&lwbuf_count, 1);

    return (lwb);
}

void
lwbuf_free(struct lwbuf *lwb)
{
    KKASSERT(lwb->m != NULL);
    lwb->m = NULL;
    lwb->cpumask = 0;
    objcache_put(lwbuf_cache, lwb);
    atomic_add_int(&lwbuf_count, -1);
}

void
lwbuf_set_global(struct lwbuf *lwb)
{
    if (lwb->cpumask != (cpumask_t)-1) {
	pmap_kenter_sync(lwb->kva);
	lwb->cpumask = (cpumask_t)-1;
    }
}

static vm_offset_t
_lwbuf_kva(struct lwbuf *lwb, struct mdglobaldata *gd)
{
    pmap_kenter_sync_quick(lwb->kva);

    ATOMIC_CPUMASK_ORBIT(lwb->cpumask, gd->mi.gd_cpuid);

    return (lwb->kva);
}

__inline vm_offset_t
lwbuf_kva(struct lwbuf *lwb)
{
    struct mdglobaldata *gd = mdcpu;

    if (CPUMASK_TESTBIT(lwb->cpumask, gd->mi.gd_cpuid))
        return (lwb->kva);

    return (_lwbuf_kva(lwb, gd));
}
