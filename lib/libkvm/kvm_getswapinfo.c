/*
 * Copyright (c) 1999 The DragonFly Project.  All rights reserved.
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
 * 
 * $FreeBSD: src/lib/libkvm/kvm_getswapinfo.c,v 1.10.2.4 2003/01/12 09:23:13 dillon Exp $
 * $DragonFly: src/lib/libkvm/kvm_getswapinfo.c,v 1.5 2006/03/18 17:15:35 dillon Exp $
 */

#define	_KERNEL_STRUCTURES

#include <sys/param.h>
#include <sys/time.h>
#include <sys/ucred.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/blist.h>
#include <sys/sysctl.h>
#include <vm/vm_param.h>

#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "kvm_private.h"

static struct nlist kvm_swap_nl[] = {
	{ "_swapblist" },	/* new radix swap list		*/
	{ "_swdevt" },		/* list of swap devices and sizes */
	{ "_nswdev" },		/* number of swap devices */
	{ "_dmmax" },		/* maximum size of a swap block */
	{ "" }
};

#define NL_SWAPBLIST	0
#define NL_SWDEVT	1
#define NL_NSWDEV	2
#define NL_DMMAX	3

static int kvm_swap_nl_cached = 0;
static int nswdev;
static int unswdev;
static int dmmax;

static int nlist_init(kvm_t *kd);
static void dump_blist(kvm_t *kd);
static int kvm_getswapinfo_sysctl(kvm_t *kd, struct kvm_swap *swap_ary,
			      int swap_max, int flags);

#define	SVAR(var) __STRING(var)	/* to force expansion */
#define	KGET(idx, var)							\
	KGET1(idx, &var, sizeof(var), SVAR(var))
#define	KGET1(idx, p, s, msg)						\
	KGET2(kvm_swap_nl[idx].n_value, p, s, msg)
#define	KGET2(addr, p, s, msg)						\
	if (kvm_read(kd, (u_long)(addr), p, s) != s)			\
		warnx("cannot read %s: %s", msg, kvm_geterr(kd))
#define	KGETN(idx, var)							\
	KGET1N(idx, &var, sizeof(var), SVAR(var))
#define	KGET1N(idx, p, s, msg)						\
	KGET2N(kvm_swap_nl[idx].n_value, p, s, msg)
#define	KGET2N(addr, p, s, msg)						\
	((kvm_read(kd, (u_long)(addr), p, s) == s) ? 1 : 0)
#define	KGETRET(addr, p, s, msg)					\
	if (kvm_read(kd, (u_long)(addr), p, s) != s) {			\
		warnx("cannot read %s: %s", msg, kvm_geterr(kd));	\
		return (0);						\
	}

#define GETSWDEVNAME(dev, str, flags)					\
	if (dev == NODEV) {						\
		strlcpy(str, "[NFS swap]", sizeof(str));		\
	} else {							\
		snprintf(						\
		    str, sizeof(str), "%s%s",				\
		    ((flags & SWIF_DEV_PREFIX) ? _PATH_DEV : ""),	\
		    devname(dev, S_IFCHR)				\
		);							\
	}

int
kvm_getswapinfo(
	kvm_t *kd, 
	struct kvm_swap *swap_ary,
	int swap_max, 
	int flags
) {
	int i, ti, swi;
	swblk_t ttl;
	struct swdevt *sw;
	struct swdevt swinfo;

	/*
	 * clear cache
	 */
	if (kd == NULL) {
		kvm_swap_nl_cached = 0;
		return(0);
	}

	if (swap_max < 1)
		return (-1);

	/*
	 * Use sysctl if possible
	 */
	if (kvm_ishost(kd) && (flags & SWIF_DUMP_TREE) == 0) {
		ti = kvm_getswapinfo_sysctl(kd, swap_ary, swap_max, flags);
		if (ti >= 0)
			return(ti);
	}

	/*
	 * namelist
	 */
	if (!nlist_init(kd))
		return (-1);

	swi = unswdev;
	if (swi >= swap_max)
		swi = swap_max - 1;

	bzero(swap_ary, sizeof(struct kvm_swap) * (swi + 1));

	KGET(NL_SWDEVT, sw);
	for (i = ti = 0; i < nswdev; ++i) {
		KGET2(&sw[i], &swinfo, sizeof(swinfo), "swinfo");

		if (swinfo.sw_nblks == 0)
			continue;

		/*
		 * The first dmmax is never allocated to avoid
		 * trashing the disklabels.
		 */
		ttl = swinfo.sw_nblks - dmmax;
		if (ttl == 0)
			continue;

		swap_ary[swi].ksw_total += ttl;
		swap_ary[swi].ksw_used += swinfo.sw_nused;

		if (ti < swi) {
			swap_ary[ti].ksw_total = ttl;
			swap_ary[ti].ksw_used = swinfo.sw_nused;
			swap_ary[ti].ksw_flags = swinfo.sw_flags;
			GETSWDEVNAME(swinfo.sw_dev, swap_ary[ti].ksw_devname,
			    flags);
			++ti;
		}
	}

	if (flags & SWIF_DUMP_TREE)
		dump_blist(kd);
	return (swi);
}

static int
nlist_init(kvm_t *kd)
{
	int i;
	struct swdevt *sw;
	struct swdevt swinfo;

	if (kvm_swap_nl_cached)
		return (1);

	if (kvm_nlist(kd, kvm_swap_nl) < 0)
		return (0);

	/*
	 * required entries
	 */
	if (kvm_swap_nl[NL_SWDEVT].n_value == 0 ||
	    kvm_swap_nl[NL_NSWDEV].n_value == 0 ||
	    kvm_swap_nl[NL_DMMAX].n_value == 0 ||
	    kvm_swap_nl[NL_SWAPBLIST].n_type == 0) {
		return (0);
	}

	/*
	 * get globals, type of swap
	 */
	KGET(NL_NSWDEV, nswdev);
	KGET(NL_DMMAX, dmmax);

	/*
	 * figure out how many actual swap devices are enabled
	 */
	KGET(NL_SWDEVT, sw);
	for (i = unswdev = 0; i < nswdev; ++i) {
		KGET2(&sw[i], &swinfo, sizeof(swinfo), "swinfo");
		if (swinfo.sw_nblks)
			++unswdev;

	}

	kvm_swap_nl_cached = 1;
	return (1);
}

/*
 * scanradix() - support routine for radix scanner
 */

#define TABME	tab, tab, ""

static int
scanradix(
	blmeta_t *scan, 
	blmeta_t *scan_cache,
	swblk_t blk,
	int64_t radix,
	swblk_t skip,
	swblk_t count,
	kvm_t *kd,
	int dmmax, 
	int nswdev,
	int64_t *availp,
	int tab
) {
	blmeta_t meta;
	blmeta_t scan_array[BLIST_BMAP_RADIX];
	int i;

	if (scan_cache) {
		meta = *scan_cache;
	} else if (skip == BLIST_META_RADIX) {
		if (kvm_read(kd, (u_long)scan, scan_array, sizeof(scan_array)) != sizeof(scan_array)) {
			warnx("cannot read %s: %s", "blmeta_t", kvm_geterr(kd));
			bzero(scan_array, sizeof(scan_array));
		}
		meta = scan_array[0];
	} else {
		KGET2(scan, &meta, sizeof(meta), "blmeta_t");
	}

	/*
	 * Terminator
	 */
	if (meta.bm_bighint == (swblk_t)-1) {
		printf("%*.*s(0x%06jx,%jd) Terminator\n",
		    TABME,
		    (intmax_t)blk,
		    (intmax_t)radix
		);
		return(-1);
	}

	if (radix == BLIST_BMAP_RADIX) {
		/*
		 * Leaf bitmap
		 */
		printf("%*.*s(0x%06jx,%jd) Bitmap %016jx big=%jd\n",
		    TABME,
		    (intmax_t)blk,
		    (intmax_t)radix,
		    (intmax_t)meta.u.bmu_bitmap,
		    (intmax_t)meta.bm_bighint
		);

		if (meta.u.bmu_bitmap) {
			for (i = 0; i < BLIST_BMAP_RADIX; ++i) {
				if (meta.u.bmu_bitmap & (1 << i))
					++*availp;
			}
		}
	} else if (meta.u.bmu_avail == radix) {
		/*
		 * Meta node if all free
		 */
		printf("%*.*s(0x%06jx,%jd) Submap ALL-FREE (big=%jd) {\n",
		    TABME,
		    (intmax_t)blk,
		    (intmax_t)radix,
		    (intmax_t)meta.bm_bighint
		);
		*availp += radix;
	} else if (meta.u.bmu_avail == 0) {
		/*
		 * Meta node if all used
		 */
		printf("%*.*s(0x%06jx,%jd) Submap ALL-ALLOCATED (big=%jd)\n",
		    TABME,
		    (intmax_t)blk,
		    (intmax_t)radix,
		    (intmax_t)meta.bm_bighint
		);
	} else {
		/*
		 * Meta node if not all free
		 */
		int i;
		int next_skip;
		int64_t avail_tmp = 0;

		printf("%*.*s(0x%06jx,%jd) Submap avail=%jd big=%jd {\n",
		    TABME,
		    (intmax_t)blk,
		    (intmax_t)radix,
		    (intmax_t)meta.u.bmu_avail,
		    (intmax_t)meta.bm_bighint
		);

		radix /= BLIST_META_RADIX;
		next_skip = skip / BLIST_META_RADIX;

		for (i = 1; i <= skip; i += next_skip) {
			int r;
			swblk_t vcount = (count > radix) ?
					(swblk_t)radix : count;

			r = scanradix(
			    &scan[i],
			    ((next_skip == 1) ? &scan_array[i] : NULL),
			    blk,
			    radix,
			    next_skip - 1,
			    vcount,
			    kd,
			    dmmax,
			    nswdev,
			    &avail_tmp,
			    tab + 4
			);
			if (r < 0)
				break;
			blk += (swblk_t)radix;
		}
		*availp += avail_tmp;
		if (avail_tmp == meta.u.bmu_avail)
			printf("%*.*s}\n", TABME);
		else
			printf("%*.*s} (AVAIL MISMATCH %jd/%jd\n",
				TABME,
				(intmax_t)avail_tmp,
				(intmax_t)meta.u.bmu_avail);
	}
	return(0);
}

static void
dump_blist(kvm_t *kd)
{
	struct blist *swapblist = NULL;
	struct blist blcopy = { 0 };
	int64_t avail = 0;

	KGET(NL_SWAPBLIST, swapblist);

	if (swapblist == NULL) {
		printf("radix tree: NULL - no swap in system\n");
		return;
	}

	KGET2(swapblist, &blcopy, sizeof(blcopy), "*swapblist");

	printf("radix tree: %jd/%jd/%jd blocks, %jdK wired\n",
		(intmax_t)blcopy.bl_free,
		(intmax_t)blcopy.bl_blocks,
		(intmax_t)blcopy.bl_radix,
		(intmax_t)((blcopy.bl_rootblks * sizeof(blmeta_t) + 1023)/
		    1024)
	);

	scanradix(
	    blcopy.bl_root,
	    NULL,
	    0,
	    blcopy.bl_radix,
	    blcopy.bl_skip,
	    blcopy.bl_rootblks,
	    kd,
	    dmmax,
	    nswdev,
	    &avail,
	    0
	);
	printf("final availability: %jd\n", (intmax_t)avail);
}

static
int
kvm_getswapinfo_sysctl(kvm_t *kd, struct kvm_swap *swap_ary,
		       int swap_max, int flags)
{
	size_t bytes = 0;
	size_t ksize;
	int ti;
	int swi;
	int n;
	int i;
	char *xswbuf;
	struct xswdev *xsw;

	if (sysctlbyname("vm.swap_info_array", NULL, &bytes, NULL, 0) < 0)
		return(-1);
	if (bytes == 0)
		return(-1);

	xswbuf = malloc(bytes);
	if (sysctlbyname("vm.swap_info_array", xswbuf, &bytes, NULL, 0) < 0) {
		free(xswbuf);
		return(-1);
	}
	if (bytes == 0) {
		free(xswbuf);
		return(-1);
	}

	/*
	 * Calculate size of xsw entry returned by kernel (it can be larger
	 * than the one we have if there is a version mismatch).
	 */
	ksize = ((struct xswdev *)xswbuf)->xsw_size;
	n = (int)(bytes / ksize);

	/*
	 * Calculate the number of live swap devices and calculate
	 * the swap_ary[] index used for the cumulative result (swi)
	 */
	for (i = swi = 0; i < n; ++i) {
		xsw = (void *)((char *)xswbuf + i * ksize);
		if ((xsw->xsw_flags & SW_FREED) == 0)
			continue;
		++swi;
	}
	if (swi >= swap_max)
		swi = swap_max - 1;

	bzero(swap_ary, sizeof(struct kvm_swap) * (swi + 1));

	/*
	 * Accumulate results.  If the provided swap_ary[] is too
	 * small will only populate up to the available entries,
	 * but we always populate the cumulative results entry.
	 */
	for (i = ti = 0; i < n; ++i) {
		xsw = (void *)((char *)xswbuf + i * ksize);

		if ((xsw->xsw_flags & SW_FREED) == 0)
			continue;

		swap_ary[swi].ksw_total += xsw->xsw_nblks;
		swap_ary[swi].ksw_used += xsw->xsw_used;

		if (ti < swi) {
			swap_ary[ti].ksw_total = xsw->xsw_nblks;
			swap_ary[ti].ksw_used = xsw->xsw_used;
			swap_ary[ti].ksw_flags = xsw->xsw_flags;
			GETSWDEVNAME(xsw->xsw_dev, swap_ary[ti].ksw_devname,
			    flags);
			++ti;
		}
	}

	free(xswbuf);
	return(swi);
}
