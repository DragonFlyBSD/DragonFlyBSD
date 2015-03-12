/*
 * Copyright (c) 1998 David Greenman.  All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sfbuf.h>
#include <sys/refcount.h>
#include <sys/objcache.h>

#include <cpu/lwbuf.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>

static void sf_buf_init(void *arg);
SYSINIT(sock_sf, SI_BOOT2_MACHDEP, SI_ORDER_ANY, sf_buf_init, NULL);

LIST_HEAD(sf_buf_list, sf_buf);

static struct objcache *sf_buf_cache;

MALLOC_DEFINE(M_SFBUF, "sfbuf", "Sendfile buffer structures");
struct objcache_malloc_args sf_buf_malloc_args = { sizeof(struct sf_buf), M_SFBUF };


static boolean_t
sf_buf_cache_ctor(void *obj, void *pdata, int ocflags)
{
	struct sf_buf *sf = (struct sf_buf *)obj;

	sf->lwbuf = NULL;
	refcount_init(&sf->ref, 0);

	return (TRUE);
}

/*
 * Init objcache of sf_bufs (sendfile(2) or "super-fast" if you prefer. :-))
 */
static void
sf_buf_init(void *arg)
{
	sf_buf_cache = objcache_create("sf_buf", 0, 0,
		sf_buf_cache_ctor, NULL, NULL,
		objcache_malloc_alloc, objcache_malloc_free,
		&sf_buf_malloc_args);
}

/*
 * Acquire an sf_buf reference for a vm_page.
 */
struct sf_buf *
sf_buf_alloc(struct vm_page *m)
{
	struct sf_buf *sf;

	if ((sf = objcache_get(sf_buf_cache, M_WAITOK)) == NULL)
		goto done;

	if ((sf->lwbuf = lwbuf_alloc(m, &sf->lwbuf_cache)) == NULL) {
		objcache_put(sf_buf_cache, sf);
		sf = NULL;
		goto done;
	}

	/*
	 * Force invalidation of the TLB entry on all CPU's
	 */
	lwbuf_set_global(sf->lwbuf);

	refcount_init(&sf->ref, 1);

done:
	return (sf);
}

void
sf_buf_ref(void *arg)
{
	struct sf_buf *sf = arg;

	refcount_acquire(&sf->ref);
}

/*
 * Detach mapped page and release resources back to the system.
 *
 * Returns non-zero (TRUE) if this was the last release of the sfbuf.
 *
 * (matching the same API that refcount_release() uses to reduce confusion)
 */
int
sf_buf_free(void *arg)
{
	struct sf_buf *sf = arg;

	if (refcount_release(&sf->ref)) {
		lwbuf_free(sf->lwbuf);
		objcache_put(sf_buf_cache, sf);
		return (1);
	}
	return (0);
}
