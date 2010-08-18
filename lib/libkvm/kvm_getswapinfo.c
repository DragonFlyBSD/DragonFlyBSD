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

static void getswapinfo_radix(kvm_t *kd, struct kvm_swap *swap_ary,
			      int swap_max, int flags);
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

int
kvm_getswapinfo(
	kvm_t *kd, 
	struct kvm_swap *swap_ary,
	int swap_max, 
	int flags
) {
	int ti;

	/*
	 * clear cache
	 */
	if (kd == NULL) {
		kvm_swap_nl_cached = 0;
		return(0);
	}

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
	if (kvm_swap_nl_cached == 0) {
		struct swdevt *sw;

		if (kvm_nlist(kd, kvm_swap_nl) < 0)
			return(-1);

		/*
		 * required entries
		 */

		if (
		    kvm_swap_nl[NL_SWDEVT].n_value == 0 ||
		    kvm_swap_nl[NL_NSWDEV].n_value == 0 ||
		    kvm_swap_nl[NL_DMMAX].n_value == 0 ||
		    kvm_swap_nl[NL_SWAPBLIST].n_type == 0
		) {
			return(-1);
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
		for (unswdev = nswdev - 1; unswdev >= 0; --unswdev) {
			struct swdevt swinfo;

			KGET2(&sw[unswdev], &swinfo, sizeof(swinfo), "swinfo");
			if (swinfo.sw_nblks)
				break;
		}
		++unswdev;

		kvm_swap_nl_cached = 1;
	}

	{
		struct swdevt *sw;
		int i;

		ti = unswdev;
		if (ti >= swap_max)
			ti = swap_max - 1;

		if (ti >= 0)
			bzero(swap_ary, sizeof(struct kvm_swap) * (ti + 1));

		KGET(NL_SWDEVT, sw);
		for (i = 0; i < unswdev; ++i) {
			struct swdevt swinfo;
			int ttl;

			KGET2(&sw[i], &swinfo, sizeof(swinfo), "swinfo");

			/*
			 * old style: everything in DEV_BSIZE'd chunks,
			 * convert to pages.
			 *
			 * new style: swinfo in DEV_BSIZE'd chunks but dmmax
			 * in pages.
			 *
			 * The first dmmax is never allocating to avoid 
			 * trashing the disklabels
			 */

			ttl = swinfo.sw_nblks - dmmax;

			if (ttl == 0)
				continue;

			if (i < ti) {
				swap_ary[i].ksw_total = ttl;
				swap_ary[i].ksw_used = ttl;
				swap_ary[i].ksw_flags = swinfo.sw_flags;
				if (swinfo.sw_dev == NODEV) {
					snprintf(
					    swap_ary[i].ksw_devname,
					    sizeof(swap_ary[i].ksw_devname),
					    "%s",
					    "[NFS swap]"
					);
				} else {
					snprintf(
					    swap_ary[i].ksw_devname,
					    sizeof(swap_ary[i].ksw_devname),
					    "%s%s",
					    ((flags & SWIF_DEV_PREFIX) ? _PATH_DEV : ""),
					    devname(swinfo.sw_dev, S_IFCHR)
					);
				}
			}
			if (ti >= 0) {
				swap_ary[ti].ksw_total += ttl;
				swap_ary[ti].ksw_used += ttl;
			}
		}
	}

	getswapinfo_radix(kd, swap_ary, swap_max, flags);
	return(ti);
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
	struct kvm_swap *swap_ary,
	int swap_max,
	int tab,
	int flags
) {
	blmeta_t meta;
	blmeta_t scan_array[BLIST_BMAP_RADIX];
	int ti = (unswdev >= swap_max) ? swap_max - 1 : unswdev;

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
		if (flags & SWIF_DUMP_TREE) {
			printf("%*.*s(0x%06x,%lld) Terminator\n",
			    TABME,
			    blk, 
			    (long long)radix
			);
		}
		return(-1);
	}

	if (radix == BLIST_BMAP_RADIX) {
		/*
		 * Leaf bitmap
		 */
		int i;

		if (flags & SWIF_DUMP_TREE) {
			printf("%*.*s(0x%06x,%lld) Bitmap %08x big=%d\n",
			    TABME,
			    blk, 
			    (long long)radix,
			    (int)meta.u.bmu_bitmap,
			    meta.bm_bighint
			);
		}

		/*
		 * If not all allocated, count.
		 */
		if (meta.u.bmu_bitmap != 0) {
			for (i = 0; i < BLIST_BMAP_RADIX && i < count; ++i) {
				/*
				 * A 0 bit means allocated
				 */
				if ((meta.u.bmu_bitmap & (1 << i))) {
					int t = 0;

					if (nswdev)
						t = (blk + i) / dmmax % nswdev;
					if (t < ti)
						--swap_ary[t].ksw_used;
					if (ti >= 0)
						--swap_ary[ti].ksw_used;
				}
			}
		}
	} else if (meta.u.bmu_avail == radix) {
		/*
		 * Meta node if all free
		 */
		if (flags & SWIF_DUMP_TREE) {
			printf("%*.*s(0x%06x,%lld) Submap ALL-FREE {\n",
			    TABME,
			    blk, 
			    (long long)radix
			);
		}
		/*
		 * Note: both dmmax and radix are powers of 2.  However, dmmax
		 * may be larger then radix so use a smaller increment if
		 * necessary.
		 */
		{
			int t;
			int tinc = dmmax;

			while (tinc > radix)
				tinc >>= 1;

			for (t = blk; t < blk + radix; t += tinc) {
				int u = (nswdev) ? (t / dmmax % nswdev) : 0;

				if (u < ti)
					swap_ary[u].ksw_used -= tinc;
				if (ti >= 0)
					swap_ary[ti].ksw_used -= tinc;
			}
		}
	} else if (meta.u.bmu_avail == 0) {
		/*
		 * Meta node if all used
		 */
		if (flags & SWIF_DUMP_TREE) {
			printf("%*.*s(0x%06x,%lld) Submap ALL-ALLOCATED\n",
			    TABME,
			    blk, 
			    (long long)radix
			);
		}
	} else {
		/*
		 * Meta node if not all free
		 */
		int i;
		int next_skip;

		if (flags & SWIF_DUMP_TREE) {
			printf("%*.*s(0x%06x,%lld) Submap avail=%d big=%d {\n",
			    TABME,
			    blk, 
			    (long long)radix,
			    (int)meta.u.bmu_avail,
			    meta.bm_bighint
			);
		}

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
			    swap_ary,
			    swap_max,
			    tab + 4,
			    flags
			);
			if (r < 0)
				break;
			blk += (swblk_t)radix;
		}
		if (flags & SWIF_DUMP_TREE) {
			printf("%*.*s}\n", TABME);
		}
	}
	return(0);
}

static void
getswapinfo_radix(kvm_t *kd, struct kvm_swap *swap_ary, int swap_max, int flags)
{
	struct blist *swapblist = NULL;
	struct blist blcopy = { 0 };

	KGET(NL_SWAPBLIST, swapblist);

	if (swapblist == NULL) {
		if (flags & SWIF_DUMP_TREE)
			printf("radix tree: NULL - no swap in system\n");
		return;
	}

	KGET2(swapblist, &blcopy, sizeof(blcopy), "*swapblist");

	if (flags & SWIF_DUMP_TREE) {
		printf("radix tree: %d/%d/%lld blocks, %dK wired\n",
			blcopy.bl_free,
			blcopy.bl_blocks,
			(long long)blcopy.bl_radix,
			(int)((blcopy.bl_rootblks * sizeof(blmeta_t) + 1023)/
			    1024)
		);
	}

	/*
	 * XXX Scan the radix tree in the kernel if we have more then one
	 *     swap device so we can get per-device statistics.  This can
	 *     get nasty because swap devices are interleaved based on the
	 *     maximum of (4), so the blist winds up not using any shortcuts.
	 *
	 *     Otherwise just pull the free count out of the blist header,
	 *     which is a billion times faster.
	 */
	if ((flags & SWIF_DUMP_TREE) || unswdev > 1) {
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
		    swap_ary,
		    swap_max,
		    0,
		    flags
		);
	} else {
		swap_ary[0].ksw_used -= blcopy.bl_free;
	}
}

static
int
kvm_getswapinfo_sysctl(kvm_t *kd, struct kvm_swap *swap_ary,
		       int swap_max, int flags)
{
	size_t bytes = 0;
	size_t ksize;
	int ti;
	int n;
	int i;
	char *xswbuf;
	struct xswdev *xsw;

	if (sysctlbyname("vm.swap_info_array", NULL, &bytes, NULL, 0) < 0)
		return(-1);
	if (bytes == 0)
		return(-1);
	xswbuf = malloc(bytes);
	if (sysctlbyname("vm.swap_info_array", xswbuf, &bytes, NULL, 0) < 0)
		return(-1);
	if (bytes == 0)
		return(-1);

	bzero(swap_ary, sizeof(struct kvm_swap) * swap_max);
	--swap_max;

	/*
	 * Calculate size of xsw entry returned by kernel (it can be larger
	 * than the one we have if there is a version mismatch).
	 *
	 * Then iterate the list looking for live swap devices.
	 */
	ksize = ((struct xswdev *)xswbuf)->xsw_size;
	n = (int)(bytes / ksize);

	for (i = ti = 0; i < n && ti < swap_max; ++i) {
		xsw = (void *)((char *)xswbuf + i * ksize);

		if ((xsw->xsw_flags & SW_FREED) == 0)
			continue;

		swap_ary[ti].ksw_total = xsw->xsw_nblks;
		swap_ary[ti].ksw_used = xsw->xsw_used;
		swap_ary[ti].ksw_flags = xsw->xsw_flags;

		if (xsw->xsw_dev == NODEV) {
			snprintf(
			    swap_ary[ti].ksw_devname,
			    sizeof(swap_ary[ti].ksw_devname),
			    "%s",
			    "[NFS swap]"
			);
		} else {
			snprintf(
			    swap_ary[ti].ksw_devname,
			    sizeof(swap_ary[ti].ksw_devname),
			    "%s%s",
			    ((flags & SWIF_DEV_PREFIX) ? _PATH_DEV : ""),
			    devname(xsw->xsw_dev, S_IFCHR)
			);
		}
		++ti;
	}
	return(ti);
}
