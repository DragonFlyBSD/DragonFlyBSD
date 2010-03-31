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

#include <sys/mplock2.h>

static void lwbuf_init(void *);
SYSINIT(sock_lwb, SI_BOOT2_MACHDEP, SI_ORDER_ANY, lwbuf_init, NULL);

static struct objcache *lwbuf_cache;

MALLOC_DEFINE(M_LWBUF, "lwbuf", "Lightweight buffers");
struct objcache_malloc_args lwbuf_malloc_args = { sizeof(struct lwbuf), M_LWBUF };

/* Number of pages of KVA to allocate at boot per cpu (1MB) */
static int lwbuf_reserve_pages = 256;

SYSCTL_INT(_kern_ipc, OID_AUTO, lwbuf_reserve, CTLFLAG_RD,
           &lwbuf_reserve_pages, 0,
           "Per-CPU count of pre-allocated lightweight buffers");

static int
do_lwbuf_count(SYSCTL_HANDLER_ARGS)
{
    int count[SMP_MAXCPU];
    struct mdglobaldata *gd;
    int i;

    for (i = 0; i < ncpus; i++) {
        gd = &CPU_prvspace[i].mdglobaldata;
        count[i] = gd->gd_lwbuf_count;
    }

    return (sysctl_handle_opaque(oidp, count, sizeof(int) * ncpus, req));
}
SYSCTL_PROC(_kern_ipc, OID_AUTO, lwbuf_count, CTLTYPE_INT|CTLFLAG_RD,
            0, 0, do_lwbuf_count, "I",
            "Per-CPU count of free lightweight buffers");

static boolean_t
lwbuf_cache_ctor(void *obj, void *pdata, int ocflags)
{
    struct lwbuf *lwb = (struct lwbuf *)obj;

    get_mplock();
    lwb->kva = kmem_alloc_nofault(&kernel_map, PAGE_SIZE, PAGE_SIZE);
    rel_mplock();
    if (lwb->kva == 0)
        return (FALSE);
    lwb->gd = mdcpu;
    lwb->ephemeral = TRUE;

    return (TRUE);
}

static void
lwbuf_cache_dtor(void *obj, void *pdata)
{
    struct lwbuf *lwb = (struct lwbuf *)obj;

    get_mplock();
    kmem_free(&kernel_map, lwb->kva, PAGE_SIZE);
    rel_mplock();
}

static void
lwbuf_init(void *arg)
{
    struct mdglobaldata *gd = mdcpu;
    struct lwbuf *lwb;
    vm_offset_t k;
    int i, j;

    for (i = 0; i < ncpus; ++i) {
	gd = &CPU_prvspace[i].mdglobaldata;
        SLIST_INIT(&gd->gd_lwbuf_fpages);
        get_mplock();
        k = kmem_alloc_nofault(&kernel_map, PAGE_SIZE * lwbuf_reserve_pages,
                               PAGE_SIZE);
        rel_mplock();
        if (k == 0) {
            gd->gd_lwbuf_count = 0;
            continue;
        }
        for (j = 0; j < lwbuf_reserve_pages; ++j) {
            lwb = kmalloc(sizeof(*lwb), M_LWBUF, M_WAITOK | M_ZERO);
            lwb->kva = k + (j * PAGE_SIZE);
            lwb->gd = gd;
            lwb->ephemeral = FALSE;
            SLIST_INSERT_HEAD(&gd->gd_lwbuf_fpages, lwb, next);
        }
        gd->gd_lwbuf_count = lwbuf_reserve_pages;
    }

    lwbuf_cache = objcache_create("lwbuf", 0, 0,
        lwbuf_cache_ctor, lwbuf_cache_dtor, NULL,
        objcache_malloc_alloc, objcache_malloc_free,
        &lwbuf_malloc_args);
}

struct lwbuf *
lwbuf_alloc(vm_page_t m)
{
    struct mdglobaldata *gd = mdcpu;
    struct lwbuf *lwb;

    crit_enter_gd(&gd->mi);
    while ((lwb = SLIST_FIRST(&gd->gd_lwbuf_fpages)) == NULL) {
        if ((lwb = objcache_get(lwbuf_cache, M_WAITOK)) == NULL)
            tsleep(&gd->gd_lwbuf_fpages, 0, "lwbuf", 0);
    }
    --gd->gd_lwbuf_count;
    SLIST_REMOVE_HEAD(&gd->gd_lwbuf_fpages, next);
    lwb->m = m;
    crit_exit_gd(&gd->mi);

    pmap_kenter_quick(lwb->kva, m->phys_addr);
    lwb->cpumask |= gd->mi.gd_cpumask;

    return (lwb);
}

#ifdef SMP
static
void
lwbuf_free_remote(void *arg)
{
    lwbuf_free(arg);
}
#endif

void
lwbuf_free(struct lwbuf *lwb)
{
    struct mdglobaldata *gd = mdcpu;

    lwb->m = NULL;
    lwb->cpumask = 0;

    if (gd == lwb->gd) {
        if (lwb->ephemeral == FALSE) {
            crit_enter_gd(&gd->mi);
            SLIST_INSERT_HEAD(&gd->gd_lwbuf_fpages, lwb, next);
            if (gd->gd_lwbuf_count++ == 0)
                wakeup_one(&gd->gd_lwbuf_fpages);
            crit_exit_gd(&gd->mi);
        } else {
            objcache_put(lwbuf_cache, lwb);
        }
    }
#ifdef SMP
    else {
	lwkt_send_ipiq_passive(&lwb->gd->mi, lwbuf_free_remote, lwb);
    }
#endif
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
