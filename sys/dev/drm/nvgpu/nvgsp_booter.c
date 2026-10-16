/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Booter ucode container parser.
 *
 * NVIDIA's GSP-boot blobs (booter_load-570.144.bin and friends) wrap
 * the actual code and data inside three nested headers, all little
 * endian:
 *
 *   Offset 0:                 nvgsp_booter_bin_hdr
 *      |
 *      bin_hdr.header_offset -> nvgsp_booter_hs_header_v2
 *                                  |
 *                                  hs.header_offset
 *                                       -> nvgsp_booter_hs_load_header_v2
 *
 *      bin_hdr.data_offset   -> raw code / data section,
 *                               described by the load header above.
 *
 * The load header tells us:
 *   * os_code  -- non-secure ("nmem") portion
 *   * os_data  -- DMEM data
 *   * app[0]   -- secure ("imem") portion, where HS code lives
 *
 * Structure layouts and field semantics are NVIDIA's, taken from
 * linux/drivers/gpu/drm/nouveau/include/nvfw/{fw,hs}.h (MIT).
 */

#include "nvgsp_priv.h"
#include "nvgsp_falcon.h"

#include <sys/firmware.h>
#include <sys/libkern.h>		/* memcpy */

struct nvgsp_booter_bin_hdr {
	uint32_t bin_magic;	/* 0x10de */
	uint32_t bin_ver;
	uint32_t bin_size;
	uint32_t header_offset;	/* -> hs_header */
	uint32_t data_offset;
	uint32_t data_size;
} __packed;

struct nvgsp_booter_hs_header_v2 {
	uint32_t sig_prod_offset;
	uint32_t sig_prod_size;
	uint32_t patch_loc;
	uint32_t patch_sig;
	uint32_t meta_data_offset;
	uint32_t meta_data_size;
	uint32_t num_sig;
	uint32_t header_offset;	/* -> hs_load_header */
	uint32_t header_size;
} __packed;

struct nvgsp_booter_hs_load_header_v2 {
	uint32_t os_code_offset;	/* non-secure code in data section */
	uint32_t os_code_size;
	uint32_t os_data_offset;	/* dmem data section */
	uint32_t os_data_size;
	uint32_t num_apps;
	struct {
		uint32_t offset;	/* secure code section */
		uint32_t size;
		uint32_t data_offset;
		uint32_t data_size;
	} app[];
} __packed;

#define NVGSP_BOOTER_BIN_MAGIC	0x000010deu

int
nvgsp_booter_parse(struct nvgsp_state *sc, const struct firmware *fw,
    struct nvgsp_booter_info *info)
{
	const uint8_t *base = fw->data;
	const struct nvgsp_booter_bin_hdr *bh;
	const struct nvgsp_booter_hs_header_v2 *hs;
	const struct nvgsp_booter_hs_load_header_v2 *lh;
	uint32_t patch_loc, patch_sig, num_sig;

	if (fw->datasize < sizeof(*bh))
		return (EIO);
	bh = (const struct nvgsp_booter_bin_hdr *)base;
	if (bh->bin_magic != NVGSP_BOOTER_BIN_MAGIC) {
		nvgpu_log(NVGPU_LOG_DEBUG, "booter: bad bin magic 0x%08x\n", bh->bin_magic);
		return (EIO);
	}
	/*
	 * bin_size is informational and may exceed the actual file size
	 * (linux-firmware seems to strip some trailing material). Validate
	 * only the offsets that we actually index into.
	 */
	if (bh->header_offset >= fw->datasize ||
	    bh->data_offset   >= fw->datasize ||
	    bh->data_offset + bh->data_size > fw->datasize) {
		nvgpu_log(NVGPU_LOG_DEBUG, "booter: bin_hdr offsets out of range "
		    "(hdr=0x%x data=0x%x+%u file=%zu)\n",
		    bh->header_offset, bh->data_offset, bh->data_size,
		    fw->datasize);
		return (EIO);
	}

	hs = (const struct nvgsp_booter_hs_header_v2 *)
	    (base + bh->header_offset);
	if (bh->header_offset + sizeof(*hs) > fw->datasize)
		return (EIO);
	if (hs->header_offset + sizeof(*lh) > fw->datasize)
		return (EIO);

	lh = (const struct nvgsp_booter_hs_load_header_v2 *)
	    (base + hs->header_offset);
	if (lh->num_apps < 1) {
		nvgpu_log(NVGPU_LOG_DEBUG, "booter: no apps in load header (num_apps=%u)\n",
		    lh->num_apps);
		return (EIO);
	}

	/* Patch info: each is a u32 at an absolute offset within the blob. */
	if (hs->patch_loc + 4 > fw->datasize ||
	    hs->patch_sig + 4 > fw->datasize ||
	    hs->num_sig   + 4 > fw->datasize)
		return (EIO);
	patch_loc = *(const uint32_t *)(base + hs->patch_loc);
	patch_sig = *(const uint32_t *)(base + hs->patch_sig);
	num_sig   = *(const uint32_t *)(base + hs->num_sig);

	info->blob	  = base;
	info->blob_size	  = fw->datasize;
	info->data_offset = bh->data_offset;
	info->data_size	  = bh->data_size;

	info->nmem_offset = lh->os_code_offset;	/* relative to blob */
	info->nmem_size   = lh->os_code_size;
	info->imem_offset = lh->app[0].offset;
	info->imem_size   = lh->app[0].size;
	info->dmem_offset = lh->os_data_offset;
	info->dmem_size   = lh->os_data_size;
	info->boot_addr   = lh->os_code_offset;	/* per nouveau tu102 */

	info->sig_prod_offset = hs->sig_prod_offset;
	info->sig_prod_size   = hs->sig_prod_size;
	info->patch_loc	      = patch_loc;
	info->patch_sig	      = patch_sig;
	info->num_sig	      = num_sig;

	nvgpu_log(NVGPU_LOG_DEBUG, "booter: bin magic=0x%x ver=%u size=%u, hs@0x%x ld@0x%x data@0x%x+%u\n",
	    bh->bin_magic, bh->bin_ver, bh->bin_size,
	    bh->header_offset, hs->header_offset,
	    bh->data_offset, bh->data_size);
	nvgpu_log(NVGPU_LOG_DEBUG, "booter: nmem(off=0x%x sz=%u) imem(off=0x%x sz=%u) "
	    "dmem(off=0x%x sz=%u) boot_addr=0x%x apps=%u\n",
	    info->nmem_offset, info->nmem_size,
	    info->imem_offset, info->imem_size,
	    info->dmem_offset, info->dmem_size,
	    info->boot_addr, lh->num_apps);
	nvgpu_log(NVGPU_LOG_DEBUG, "booter: sig_prod off=0x%x sz=%u, patch loc/sig/num = %u/%u/%u\n",
	    info->sig_prod_offset, info->sig_prod_size,
	    info->patch_loc, info->patch_sig, info->num_sig);

	return (0);
}

/*
 * Bootloader DMEM descriptor v2. The booter's nmem (non-secure)
 * bootloader stub reads this at DMEM offset 0 to know where the rest of
 * the image lives in system DMA memory and which DMA index to use for
 * fetching it. Layout matches NVIDIA's flcn_bl_dmem_desc_v2.
 */
struct nvgsp_bl_dmem_desc_v2 {
	uint32_t	reserved[4];
	uint32_t	signature[4];
	uint32_t	ctx_dma;
	uint64_t	code_dma_base;
	uint32_t	non_sec_code_off;
	uint32_t	non_sec_code_size;
	uint32_t	sec_code_off;
	uint32_t	sec_code_size;
	uint32_t	code_entry_point;
	uint64_t	data_dma_base;
	uint32_t	data_size;
	uint32_t	argc;
	uint32_t	argv;
} __packed;

/* FALCON_DMAIDX values, see linux/nvkm/engine/falcon.h. */
#define NVGSP_FLCN_DMAIDX_PHYS_SYS_NCOH	4

/* FBIF TRANSCFG[ctx_dma]: tells Falcon how to interpret this DMA index.
 * Value 0x5 == TARGET=NONCOHERENT_SYSMEM | MEMTYPE=PHYSICAL. */
#define NVGSP_FBIF_TRANSCFG(i)		(0x600u + (i) * 4u)
#define NVGSP_FBIF_TRANSCFG_NCOH_PHYS	0x00000005u

int
nvgsp_booter_run(struct nvgsp_state *sc, const struct nvgsp_booter_info *bi,
    uint32_t mb0_in, uint32_t mb1_in)
{
	struct nvgsp_falcon *sec2 = sc->sec2;
	uint32_t mb0, mb1, cpuctl, dmactl;
	uint8_t *data;
	int error;

	if (bi->blob == NULL || sec2 == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG, "booter: cannot start (booter info or sec2 missing)\n");
		return (ENXIO);
	}

	/* A previous run (booter_load at attach) may still own the staging
	 * buffer; this run replaces it. */
	if (sc->booter_dma.kva != NULL)
		nvgsp_dma_free_dmamem(sc, &sc->booter_dma);

	/*
	 * 1. Stage the booter's data section in our own buffer so we can
	 * patch the HS signature into it before PIO-loading to Falcon.
	 * NO BL+DMA path here -- the booter is a complete HS image and
	 * nouveau gm200_flcn_fw_load uploads nmem+imem+dmem via PIO when
	 * fw->boot == NULL. We are not using sc->booter_dma for DMA; it
	 * is just our editable copy of the signed image.
	 */
	error = nvgsp_dma_alloc_dmamem(sc, bi->data_size, 4096, &sc->booter_dma);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "booter: dma alloc failed (%d)\n", error);
		return (error);
	}
	memcpy(sc->booter_dma.kva, bi->blob + bi->data_offset, bi->data_size);
	data = (uint8_t *)sc->booter_dma.kva;
	nvgpu_log(NVGPU_LOG_DEBUG, "booter: staged %u bytes (data section) to kva=%p\n",
	    bi->data_size, data);

	/*
	 * 1b. Patch the HS signature into the staged data section.
	 *
	 * Per nouveau:
	 *   src = blob[sig_prod_offset + patch_sig * sig_size]
	 *         (sig_prod_offset is offset within WHOLE blob, in the
	 *         HS header region before data_offset)
	 *   dst = fw.img[patch_loc]
	 *         (patch_loc is offset within fw.img = blob + data_offset,
	 *         pointing into the dmem section once data is uploaded)
	 *
	 * Earlier code subtracted data_offset from patch_loc; that was
	 * wrong because patch_loc is ALREADY data-section-relative.
	 */
	if (bi->num_sig > 0 && bi->sig_prod_size > 0) {
		uint32_t sig_size = bi->sig_prod_size / bi->num_sig;
		uint32_t src_off  = bi->sig_prod_offset +
		    bi->patch_sig * sig_size;
		uint32_t dst_off  = bi->patch_loc;

		if (src_off + sig_size <= bi->blob_size &&
		    dst_off + sig_size <= bi->data_size) {
			memcpy(data + dst_off, bi->blob + src_off, sig_size);
			nvgpu_log(NVGPU_LOG_DEBUG, "booter: patched %u-byte sig idx=%u from blob+0x%x to data+0x%x\n",
			    sig_size, bi->patch_sig, src_off, dst_off);
		} else {
			nvgpu_log(NVGPU_LOG_DEBUG, "booter: sig patch OOB (src=0x%x+%u dst=0x%x+%u, "
			    "blob_size=%u data_size=%u)\n",
			    src_off, sig_size, dst_off, sig_size,
			    bi->blob_size, bi->data_size);
		}
	}

	/*
	 * 2. Reset SEC2 + disable context requirement.
	 */
	{
		int rerr = nvgsp_falcon_reset_eng(sec2);
		if (rerr != 0)
			nvgpu_log(NVGPU_LOG_DEBUG, "booter: SEC2 reset_eng returned %d (continuing)\n",
			    rerr);
	}
	nvgsp_falcon_disable_ctx_req(sec2);

	/*
	 * 3. Upload nmem (NS code), imem (SEC code) and dmem to Falcon
	 * via PIO. This mirrors nouveau gm200_flcn_fw_load's fw->boot ==
	 * NULL fall-through path (gm200.c:299-314): three PIO writes
	 * for NS code (IMEM, not secure), SEC code (IMEM, secure tag),
	 * and DMEM data.
	 *
	 * Source offsets (within our staged data buffer):
	 *   nmem at data + 0                       (nmem_base_img = 0)
	 *   imem at data + nmem_size               (imem_base_img = nmem_size)
	 *   dmem at data + dmem_offset             (dmem_base_img = dmem_offset)
	 */
	error = nvgsp_falcon_load_imem(sec2,
	    data + 0,
	    bi->nmem_offset,
	    bi->nmem_size,
	    bi->nmem_offset >> 8,
	    0, false);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "booter: load_imem(nmem) failed (%d)\n", error);
		goto out_free;
	}
	error = nvgsp_falcon_load_imem(sec2,
	    data + bi->nmem_size,
	    bi->imem_offset,
	    bi->imem_size,
	    bi->imem_offset >> 8,
	    0, true);	/* secure tag */
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "booter: load_imem(sec) failed (%d)\n", error);
		goto out_free;
	}
	error = nvgsp_falcon_load_dmem(sec2,
	    data + bi->dmem_offset,
	    0,
	    bi->dmem_size,
	    0);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "booter: load_dmem failed (%d)\n", error);
		goto out_free;
	}
	nvgpu_log(NVGPU_LOG_DEBUG, "booter: PIO uploaded nmem@imem[0x%x]+%u (tag 0x%x), "
	    "imem@imem[0x%x]+%u (tag 0x%x SEC), dmem[0]+%u\n",
	    bi->nmem_offset, bi->nmem_size, bi->nmem_offset >> 8,
	    bi->imem_offset, bi->imem_size, bi->imem_offset >> 8,
	    bi->dmem_size);

	/*
	 * 7. Set BOOTVEC and mailbox inputs. The booter expects the
	 * sysmem physical address of the GspFwWprMeta struct in
	 * MAILBOX0 (low 32) / MAILBOX1 (high 32). Without this the
	 * booter halts with mb0 = 0x31 ("no wpr_meta provided").
	 */
	nvgsp_falcon_set_bootvec(sec2, bi->boot_addr);
	nvgsp_falcon_wr32(sec2, NVGSP_FLCN_MAILBOX0, mb0_in);
	nvgsp_falcon_wr32(sec2, NVGSP_FLCN_MAILBOX1, mb1_in);
	nvgpu_log(NVGPU_LOG_DEBUG, "booter: mailbox inputs mb0=0x%08x mb1=0x%08x\n", mb0_in, mb1_in);

	nvgpu_log(NVGPU_LOG_DEBUG, "booter: starting SEC2 (bootvec=0x%x)\n",
	    bi->boot_addr);

	/* 8. Start and wait. */
	nvgsp_falcon_start(sec2);
	error = nvgsp_falcon_wait_for_halt(sec2, 2000000);	/* 2 s */

	mb0    = nvgsp_falcon_rd32(sec2, NVGSP_FLCN_MAILBOX0);
	mb1    = nvgsp_falcon_rd32(sec2, NVGSP_FLCN_MAILBOX1);
	cpuctl = nvgsp_falcon_rd32(sec2, NVGSP_FLCN_CPUCTL);
	dmactl = nvgsp_falcon_rd32(sec2, NVGSP_FLCN_DMACTL);

	nvgpu_log(NVGPU_LOG_DEBUG, "booter: %s mb0=0x%08x mb1=0x%08x cpuctl=0x%08x dmactl=0x%08x\n",
	    error == 0 ? "halted" : "timed out",
	    mb0, mb1, cpuctl, dmactl);

	/*
	 * Post-halt diagnostics: dump SEC2 internal state + a few PMC
	 * scratch regs in case the booter signaled there. Also peek the
	 * first few u32 of DMEM in case it wrote a status block.
	 */
	{
		uint32_t sctl   = nvgsp_falcon_rd32(sec2, 0x240);
		uint32_t exci   = nvgsp_falcon_rd32(sec2, 0x024);
		uint32_t irqstat= nvgsp_falcon_rd32(sec2, 0x008);
		uint32_t dmemc, d0, d1, d2, d3;
		uint32_t wpr2_lo = nvgsp_rd32(sc, 0x1fa824);
		uint32_t wpr2_hi = nvgsp_rd32(sc, 0x1fa828);

		nvgpu_log(NVGPU_LOG_DEBUG, "booter: SCTL=0x%08x EXCI=0x%08x IRQSTAT=0x%08x WPR2=0x%08x/0x%08x\n",
		    sctl, exci, irqstat, wpr2_lo, wpr2_hi);

		dmemc = 0x0u | (1u << 25);	/* DMEM[0], AINCR */
		nvgsp_falcon_wr32(sec2, 0x1c0, dmemc);
		d0 = nvgsp_falcon_rd32(sec2, 0x1c4);
		d1 = nvgsp_falcon_rd32(sec2, 0x1c4);
		d2 = nvgsp_falcon_rd32(sec2, 0x1c4);
		d3 = nvgsp_falcon_rd32(sec2, 0x1c4);
		nvgpu_log(NVGPU_LOG_DEBUG, "booter: DMEM[0..0x10] = %08x %08x %08x %08x\n",
		    d0, d1, d2, d3);
		nvgpu_log(NVGPU_LOG_DEBUG, "booter: scratch[c..f] = %08x %08x %08x %08x\n",
		    nvgsp_rd32(sc, 0x001430), nvgsp_rd32(sc, 0x001434),
		    nvgsp_rd32(sc, 0x001438), nvgsp_rd32(sc, 0x00143c));
	}

	return (error);

out_free:
	nvgsp_dma_free_dmamem(sc, &sc->booter_dma);
	return (error);
}

void
nvgsp_booter_release(struct nvgsp_state *sc)
{
	if (sc->booter_dma.kva != NULL)
		nvgsp_dma_free_dmamem(sc, &sc->booter_dma);
}

#define NVGSP_WPR2_ADDR_LO	0x1fa824
#define NVGSP_WPR2_ADDR_HI	0x1fa828
#define NVGSP_MB0_SHUTDOWN	0x80000000u

void
nvgsp_shutdown_backend(struct nvgsp_state *sc)
{
	struct nvgsp_booter_info bi;
	uint32_t base = sc->chip->gsp_base;
	uint32_t mb0, wpr2_hi;
	int err, polls;

	if (!sc->gsp_running)
		return;

	/* 1. Ask GSP-RM to shut down (reply is polled from the msgq; no
	 * IRQ or drain thread required). */
	lwkt_gettoken(&sc->gsp_tok);
	err = nvgsp_rpc_get_unloading_guest_driver_state(sc);
	lwkt_reltoken(&sc->gsp_tok);
	if (err != 0)
		nvgpu_log(NVGPU_LOG_INFO, "gsp: UNLOADING_GUEST_DRIVER rpc err=%d\n", err);

	/* 2. Wait up to 2s for GSP-RM to report halt in its MAILBOX0. */
	mb0 = 0;
	for (polls = 0; polls < 2000; polls++) {
		mb0 = nvgsp_rd32(sc, base + 0x040);
		if (mb0 == NVGSP_MB0_SHUTDOWN)
			break;
		DELAY(1000);
	}
	nvgpu_log(NVGPU_LOG_INFO, "gsp: shutdown MB0=0x%08x after %d ms%s\n",
	    mb0, polls,
	    mb0 == NVGSP_MB0_SHUTDOWN ? "" : " (halt not confirmed)");
	sc->gsp_running = false;

	/* 3. Reset the GSP falcon out of RISC-V mode. */
	if (sc->gsp != NULL) {
		err = nvgsp_falcon_reset_eng(sc->gsp);
		if (err != 0)
			nvgpu_log(NVGPU_LOG_INFO, "gsp: shutdown falcon reset err=%d\n", err);
	}

	/* 4. FWSEC-SB (driver shutdown counterpart of attach-time FRTS). */
	err = nvgsp_fwsec_run_cmd(sc, NVGSP_FWSEC_CMD_SB, 0, 0);
	if (err != 0)
		nvgpu_log(NVGPU_LOG_INFO, "gsp: FWSEC-SB err=%d\n", err);

	/* 5. Tear down WPR2 so the next attach can boot GSP-RM again. */
	wpr2_hi = nvgsp_rd32(sc, NVGSP_WPR2_ADDR_HI);
	if (wpr2_hi == 0) {
		nvgpu_log(NVGPU_LOG_INFO, "gsp: WPR2 already clear, booter_unload skipped\n");
		return;
	}
	if (sc->fw_booter_unload == NULL) {
		nvgpu_log(NVGPU_LOG_INFO, "gsp: no booter_unload blob; WPR2 stays set (0x%08x)\n",
		    wpr2_hi);
		return;
	}
	if (nvgsp_booter_parse(sc, sc->fw_booter_unload, &bi) != 0) {
		nvgpu_log(NVGPU_LOG_INFO, "gsp: booter_unload parse failed\n");
		return;
	}
	err = nvgsp_booter_run(sc, &bi, 0xff, 0xff);
	wpr2_hi = nvgsp_rd32(sc, NVGSP_WPR2_ADDR_HI);
	nvgpu_log(NVGPU_LOG_INFO, "gsp: booter_unload %s, WPR2_HI=0x%08x%s\n",
	    err == 0 ? "halted" : "error",
	    wpr2_hi, wpr2_hi == 0 ? " (torn down)" : " (still set!)");
}
