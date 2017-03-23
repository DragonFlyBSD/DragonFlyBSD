/*
 * Copyright (c) 2011 The FreeBSD Foundation
 * Copyright (c) 2015-2017 François Tigeot
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov under sponsorship from
 * the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _AGP_INTEL_GTT_H_
#define _AGP_INTEL_GTT_H_

#include <sys/param.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <linux/types.h>
#include <linux/scatterlist.h>

/* Special gtt memory types */
#define AGP_DCACHE_MEMORY	1
#define AGP_PHYS_MEMORY		2

/* New caching attributes for gen6/sandybridge */
#define AGP_USER_CACHED_MEMORY_LLC_MLC (AGP_USER_TYPES + 2)
#define AGP_USER_UNCACHED_MEMORY (AGP_USER_TYPES + 4)

/* flag for GFDT type */
#define AGP_USER_CACHED_MEMORY_GFDT (1 << 3)

struct intel_gtt {
	/* Size of memory reserved for graphics by the BIOS */
	u_int stolen_size;
	/* Total number of gtt entries. */
	u_int gtt_total_entries;
	/* Part of the gtt that is mappable by the cpu, for those chips where
	 * this is not the full gtt. */
	u_int gtt_mappable_entries;
	/* Whether we idle the gpu before mapping/unmapping */
	unsigned int do_idle_maps : 1;
	/* Share the scratch page dma with ppgtts. */
	vm_paddr_t scratch_page_dma;
	struct vm_page *scratch_page;
	/* for ppgtt PDE access */
	uint32_t *gtt;
	/* needed for ioremap in drm/i915 */
	bus_addr_t gma_bus_addr;
};

struct intel_gtt agp_intel_gtt_get(device_t dev);
int agp_intel_gtt_chipset_flush(device_t dev);
void agp_intel_gtt_clear_range(device_t dev, u_int first_entry,
    u_int num_entries);
void agp_intel_gtt_insert_pages(device_t dev, u_int first_entry,
    u_int num_entries, vm_page_t *pages, u_int flags);

void intel_gtt_get(u64 *gtt_total, size_t *stolen_size,
		   phys_addr_t *mappable_base, u64 *mappable_end);

int intel_gtt_chipset_flush(void);
void intel_gtt_clear_range(u_int first_entry, u_int num_entries);

void intel_gtt_insert_sg_entries(struct sg_table *st,
				 unsigned int pg_start,
				 unsigned int flags);

void intel_gtt_sync_pte(u_int entry);
void intel_gtt_write(u_int entry, uint32_t val);

static inline void intel_gmch_remove(void)
{
}

bool intel_enable_gtt(void);

#endif		/* _AGP_INTEL_GTT_H_ */
