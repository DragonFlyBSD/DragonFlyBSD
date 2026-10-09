/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Phase 2 milestone: round-trip GET_GSP_STATIC_INFO RPC.
 * Extracts internal client/device/subdevice handles + BAR PDE bases
 * for use by Phase 3.
 *
 * Field offsets are r570-specific (derived empirically from a dump of
 * the GspStaticConfigInfo reply). When we eventually mirror the full
 * struct in nvgsp_abi.h we can replace these magic offsets with
 * struct field accessors.
 */

#include "nvgsp_priv.h"
#include <sys/libkern.h>

#define NV_VGPU_MSG_FUNCTION_GET_GSP_STATIC_INFO	65
#define NVGSP_STATIC_INFO_SIZE	0xa00	/* covers fields up to ecidInfo */

/* r570 GspStaticConfigInfo field offsets (verified via dump). */
#define OFF_BAR1_PDE_BASE		0x600	/* NvU64 */
#define OFF_BAR2_PDE_BASE		0x608	/* NvU64 */
#define OFF_GPU_NAME_STRING		0x4ec	/* char[64] */
#define OFF_H_INTERNAL_CLIENT		0x640	/* NvU32 */
#define OFF_H_INTERNAL_DEVICE		0x644	/* NvU32 */
#define OFF_H_INTERNAL_SUBDEVICE	0x648	/* NvU32 */

int
nvgsp_static_query_info(struct nvgsp_state *sc)
{
	uint8_t *r;
	uint32_t i, j, m, scan;
	char buf[80];

	nvgpu_log(NVGPU_LOG_DEBUG, "static_info: issuing fn=65 RECV...\n");

	r = nvgsp_rpc_rd(sc,
	    NV_VGPU_MSG_FUNCTION_GET_GSP_STATIC_INFO,
	    NVGSP_STATIC_INFO_SIZE);
	if (r == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG, "static_info: rpc_rd failed\n");
		return (EIO);
	}

	sc->gsp_internal_client    = *(uint32_t *)(r + OFF_H_INTERNAL_CLIENT);
	sc->gsp_internal_device    = *(uint32_t *)(r + OFF_H_INTERNAL_DEVICE);
	sc->gsp_internal_subdevice = *(uint32_t *)(r + OFF_H_INTERNAL_SUBDEVICE);
	sc->gsp_bar1_pdb           = *(uint64_t *)(r + OFF_BAR1_PDE_BASE);
	sc->gsp_bar2_pdb           = *(uint64_t *)(r + OFF_BAR2_PDE_BASE);

	/* Extract GPU name string for the dmesg. */
	scan = NVGSP_STATIC_INFO_SIZE;
	for (i = 0; i + 16 <= scan; i++) {
		if (memcmp(r + i, "NVIDIA", 6) == 0 ||
		    memcmp(r + i, "GeForce", 7) == 0 ||
		    memcmp(r + i, "RTX", 3) == 0) {
			m = 0;
			for (j = 0; j < 79 && i + j < scan; j++) {
				char c = r[i + j];
				if (c == 0)
					break;
				if (c >= 0x20 && c < 0x7f)
					buf[m++] = c;
			}
			buf[m] = 0;
			nvgpu_log(NVGPU_LOG_DEBUG, "static_info: GPU = \"%s\"\n", buf);
			break;
		}
	}

	nvgpu_log(NVGPU_LOG_DEBUG, "static_info: hInternalClient=0x%08x device=0x%08x subdevice=0x%08x\n",
	    sc->gsp_internal_client, sc->gsp_internal_device,
	    sc->gsp_internal_subdevice);
	nvgpu_log(NVGPU_LOG_DEBUG, "static_info: bar1PdeBase=0x%llx bar2PdeBase=0x%llx\n",
	    (unsigned long long)sc->gsp_bar1_pdb,
	    (unsigned long long)sc->gsp_bar2_pdb);

	/* fbRegionInfoParams — r570 layout. See script comments. */
	{
		const uint32_t OFF_NUM_FB_REGIONS = 0x158;
		const uint32_t OFF_FB_REGION_ARRAY = 0x160;
		const uint32_t FB_REGION_STRIDE = 48;
		uint32_t n = *(uint32_t *)(r + OFF_NUM_FB_REGIONS);
		uint64_t best_base = 0, best_size = 0;
		uint32_t i;

		if (n > 16)
			n = 16;
		for (i = 0; i < n; i++) {
			uint8_t *e = r + OFF_FB_REGION_ARRAY + i * FB_REGION_STRIDE;
			uint64_t base  = *(uint64_t *)(e + 0);
			uint64_t limit = *(uint64_t *)(e + 8);
			uint64_t rsvd  = *(uint64_t *)(e + 16);
			uint8_t  prot  = *(uint8_t  *)(e + 30);
			uint64_t size;

			if (limit <= base)
				continue;
			size = (limit + 1) - base;
			nvgpu_log(NVGPU_LOG_DEBUG, "static_info: fb_region[%u] base=0x%llx limit=0x%llx "
			    "rsvd=0x%llx prot=%u\n", i,
			    (unsigned long long)base,
			    (unsigned long long)limit,
			    (unsigned long long)rsvd, prot);
			if (rsvd != 0 || prot != 0)
				continue;
			if (size > best_size) {
				best_base = base;
				best_size = size;
			}
		}
		sc->fb_usable_base = best_base;
		sc->fb_usable_size = best_size;
		nvgpu_log(NVGPU_LOG_DEBUG, "static_info: usable VRAM region 0x%llx + 0x%llx\n",
		    (unsigned long long)best_base,
		    (unsigned long long)best_size);
	}

	nvgsp_rpc_complete(sc, r);
	return (0);
}
