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

static void lwbuf_init(void *);
SYSINIT(sock_lwb, SI_BOOT2_MACHDEP, SI_ORDER_ANY, lwbuf_init, NULL);

/* Number of pages of KVA to allocate at boot per cpu (1MB) */
#define	LWBUF_BOOT_PAGES	256
/* Number to allocate incrementally (128KB) */
#define LWBUF_ALLOC_PAGES	32

static struct objcache *lwbuf_cache;

MALLOC_DEFINE(M_LWBUF, "lwbuf", "Lightweight buffers");
struct objcache_malloc_args lwbuf_malloc_args = { sizeof(struct lwbuf), M_LWBUF };


static boolean_t
lwbuf_cache_ctor(void *obj, void *pdata, int ocflags)
{
    struct lwbuf *lwb = (struct lwbuf *)obj;

    lwb->m = NULL;
    lwb->kva = 0;
    lwb->cpumask = 0;

    return (TRUE);
}

static boolean_t
lwbuf_initpages(struct lwbuf_free_kvp_list *fkvpl, int pages)
{
    struct lwbuf_free_kvp *free_kvp;
    vm_offset_t k;
    int i;

    k = kmem_alloc_nofault(&kernel_map, PAGE_SIZE * pages, PAGE_SIZE);
    if (k == 0)
        return (FALSE);

    for (i = 0; i < pages; ++i) {
        free_kvp = (struct lwbuf_free_kvp *)
            kmalloc(sizeof(*free_kvp), M_LWBUF, M_WAITOK | M_ZERO);

        free_kvp->kva = k + (i * PAGE_SIZE);
        SLIST_INSERT_HEAD(fkvpl, free_kvp, next);
    }

    return (TRUE);
}

static void
lwbuf_init(void *arg)
{
    struct mdglobaldata *gd = mdcpu;
    int i;

    lwbuf_cache = objcache_create("lwbuf", 0, 0,
	lwbuf_cache_ctor, NULL, NULL,
	objcache_malloc_alloc, objcache_malloc_free,
	&lwbuf_malloc_args);

    /* Should probably be in cpu_gdinit */
    for (i = 0; i < SMP_MAXCPU; ++i) {
        SLIST_INIT(&gd->gd_lwbuf_fpages);
        lwbuf_initpages(&gd->gd_lwbuf_fpages, LWBUF_BOOT_PAGES);
    }
}

struct lwbuf *
lwbuf_alloc(vm_page_t m)
{
    struct mdglobaldata *gd = mdcpu;
    struct lwbuf_free_kvp *free_kvp;
    struct lwbuf *lwb;

    if ((lwb = objcache_get(lwbuf_cache, M_WAITOK)) == NULL)
        return (NULL);

    lwb->m = m;

    crit_enter_gd(&gd->mi);
check_slist:
    if (!SLIST_EMPTY(&gd->gd_lwbuf_fpages)) {
        free_kvp = SLIST_FIRST(&gd->gd_lwbuf_fpages);
        SLIST_REMOVE_HEAD(&gd->gd_lwbuf_fpages, next);

        lwb->kva = free_kvp->kva;

        kfree(free_kvp, M_LWBUF);
    } else {
        if (lwbuf_initpages(&gd->gd_lwbuf_fpages,
                            LWBUF_ALLOC_PAGES) == FALSE)
            tsleep(&gd->gd_lwbuf_fpages, 0, "lwbuf", 0);

        goto check_slist;
    }
    crit_exit_gd(&gd->mi);

    pmap_kenter_quick(lwb->kva, lwb->m->phys_addr);
    lwb->cpumask |= gd->mi.gd_cpumask;

    return (lwb);
}

void
lwbuf_free(struct lwbuf *lwb)
{
    struct mdglobaldata *gd = mdcpu;
    struct lwbuf_free_kvp *free_kvp;

    free_kvp = (struct lwbuf_free_kvp *)
        kmalloc(sizeof(*free_kvp), M_LWBUF, M_WAITOK);
    free_kvp->kva = lwb->kva;
    crit_enter_gd(&gd->mi);
    SLIST_INSERT_HEAD(&gd->gd_lwbuf_fpages, free_kvp, next);
    crit_exit_gd(&gd->mi);
    wakeup_one(&gd->gd_lwbuf_fpages);

    lwb->m = NULL;
    lwb->kva = 0;
    lwb->cpumask = 0;

    objcache_put(lwbuf_cache, lwb);
}

void
lwbuf_set_global(struct lwbuf *lwb)
{
    pmap_kenter_sync(lwb->kva);
    lwb->cpumask = (cpumask_t)-1;
}

static vm_offset_t
_lwbuf_kva(struct lwbuf *lwb, struct mdglobaldata *gd)
{
    cpumask_t old, new;

    pmap_kenter_sync_quick(lwb->kva);

    do {
        old = lwb->cpumask;
        new = old | gd->mi.gd_cpumask;
    } while (atomic_cmpset_int(&lwb->cpumask, old, new) == 0);

    return (lwb->kva);
}

__inline vm_offset_t
lwbuf_kva(struct lwbuf *lwb)
{
    struct mdglobaldata *gd = mdcpu;

    if (lwb->cpumask & gd->mi.gd_cpumask)
        return (lwb->kva);

    return (_lwbuf_kva(lwb, gd));
}
