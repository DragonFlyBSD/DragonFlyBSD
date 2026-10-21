/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * FwSec extraction and execution on SEC2.
 *
 * NVIDIA's FwSec HS ucode lives embedded in VBIOS. It sets up the FRTS
 * region in VRAM and programs the WPR2 hardware-protected window
 * registers (0x1fa824/8). Those registers are PLM-locked from PRI access
 * by RM, so writing them from the kernel is not possible; they have to
 * be programmed from HS Falcon mode, which is exactly what FwSec runs in.
 *
 * Path:
 *   1. Scan our PROM-extracted VBIOS for a FALCON_UCODE_DESC_V2 with
 *      the FwSec layout (size=60, ver=2, version-avail bit set).
 *   2. Parse the descriptor; body data follows immediately after.
 *   3. Stage IMEM (nsec+sec) and DMEM into a DMA-coherent buffer.
 *   4. Patch DMEM at the descriptor's interfaceOffset to walk the
 *      Falcon Application Interface header, locate the DMEM_MAPPER_V3
 *      entry, and write the FRTS command (init_cmd=0x15 plus a
 *      FWSECLIC_FRTS_CMD payload at cmd_in_buffer_offset).
 *   5. Load the generic ACR bootloader (nvidia/tu102/acr/bl) into
 *      SEC2 IMEM at top, write its DMEM descriptor (flcn_bl_dmem_desc_v2)
 *      with the staged ucode's DMA address, start, wait for halt.
 *   6. Verify by reading WPR2_LO/HI and the FWSEC scratch register.
 *
 * Source references:
 *   linux/drivers/gpu/drm/nouveau/nvkm/subdev/gsp/fwsec.c
 *   open-rm/src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_frts_tu102.c
 *   open-rm/src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_falcon_tu102.c
 */

#include "nvgsp_priv.h"
#include "nvgsp_falcon.h"

#include <sys/firmware.h>
#include <sys/ktr.h>
#include <sys/libkern.h>		/* memcpy/memset */

static MALLOC_DEFINE(M_NVGSP_FWSEC, "nvgsp_fwsec", "nvgsp FwSec staging");

#ifndef KTR_NVGPU
#define KTR_NVGPU KTR_ALL
#endif

KTR_INFO_MASTER_EXTERN(nvgpu);
KTR_INFO(KTR_NVGPU, nvgpu, gsp_fwsec_stage, 18,
    "gsp fwsec stage=%u command=%u error=%d", uint32_t stage,
    uint32_t command, int error);

/* ----- Wire-level types (MIT-derived from open-rm/nouveau) ----------- */

/* FALCON_UCODE_DESC_V2 -- 60 bytes */
struct nvgsp_fud_v2 {
	uint32_t hdr;	/* bit0=ver_avail, bit2=encrypted, [15:8]=ver, [31:16]=size */
	uint32_t stored_size;
	uint32_t uncompressed_size;
	uint32_t virtual_entry;
	uint32_t interface_offset;
	uint32_t imem_phys_base;
	uint32_t imem_load_size;
	uint32_t imem_virt_base;
	uint32_t imem_sec_base;
	uint32_t imem_sec_size;
	uint32_t dmem_offset;
	uint32_t dmem_phys_base;
	uint32_t dmem_load_size;
	uint32_t alt_imem_load_size;
	uint32_t alt_dmem_load_size;
} __packed;

/* nvfw_bin_hdr wrapper (acr_bl uses this format too) */
struct nvgsp_bin_hdr {
	uint32_t bin_magic;	/* 0x10de */
	uint32_t bin_ver;
	uint32_t bin_size;
	uint32_t header_offset;
	uint32_t data_offset;
	uint32_t data_size;
} __packed;

/* nvfw_bl_desc -- inside acr bl bin, at header_offset */
struct nvgsp_bl_desc {
	uint32_t start_tag;
	uint32_t dmem_load_off;
	uint32_t code_off;
	uint32_t code_size;
	uint32_t data_off;
	uint32_t data_size;
} __packed;

/* Falcon Application Interface header v1 -- at dmem[interfaceOffset] */
struct nvgsp_appif_hdr_v1 {
	uint8_t ver;
	uint8_t hdr;	/* header size = offset of first entry */
	uint8_t len;	/* entry length */
	uint8_t cnt;	/* number of entries */
} __packed;

struct nvgsp_appif_entry_v1 {
	uint32_t id;
	uint32_t dmem_offset;	/* offset within DMEM of the application's struct */
} __packed;

#define NVGSP_APPIF_ID_DMEMMAPPER	0x4

/* DMEMMAPPER_V3 -- 56 bytes, at dmem[entry.dmem_offset] for DMEMMAPPER entry */
struct nvgsp_dmemmap_v3 {
	uint32_t signature;
	uint16_t version;
	uint16_t size;
	uint32_t cmd_in_buffer_offset;
	uint32_t cmd_in_buffer_size;
	uint32_t cmd_out_buffer_offset;
	uint32_t cmd_out_buffer_size;
	uint32_t nvf_img_data_buffer_offset;
	uint32_t nvf_img_data_buffer_size;
	uint32_t printf_buffer_hdr;
	uint32_t ucode_build_time_stamp;
	uint32_t ucode_signature;
	uint32_t init_cmd;
	uint32_t ucode_feature;
	uint32_t ucode_cmd_mask0;
	uint32_t ucode_cmd_mask1;
	uint32_t multi_tgt_tbl;
} __packed;

#define NVGSP_DMEMMAP_CMD_FRTS	0x15
#define NVGSP_DMEMMAP_CMD_SB	0x19

/* read_vbios + frts_region cmd; written at dmemmap.cmd_in_buffer_offset */
struct nvgsp_fwsec_frts_cmd {
	struct {
		uint32_t ver;
		uint32_t hdr;
		uint64_t addr;
		uint32_t size;
		uint32_t flags;
	} __packed read_vbios;
	struct {
		uint32_t ver;
		uint32_t hdr;
		uint32_t addr_4k;
		uint32_t size_4k;
		uint32_t media_type;
	} __packed frts_region;
} __packed;

#define NVGSP_FRTS_MEDIA_FB	2

/* flcn_bl_dmem_desc_v2 -- same as in nvgsp_booter.c */
struct nvgsp_bl_dmem_desc_v2 {
	uint32_t reserved[4];
	uint32_t signature[4];
	uint32_t ctx_dma;
	uint64_t code_dma_base;
	uint32_t non_sec_code_off;
	uint32_t non_sec_code_size;
	uint32_t sec_code_off;
	uint32_t sec_code_size;
	uint32_t code_entry_point;
	uint64_t data_dma_base;
	uint32_t data_size;
	uint32_t argc;
	uint32_t argv;
} __packed;

#define NVGSP_FLCN_DMAIDX_PHYS_SYS_NCOH		4
#define NVGSP_FBIF_TRANSCFG(i)			(0x600u + (i) * 4u)
#define NVGSP_FBIF_TRANSCFG_NCOH_PHYS		0x00000005u

#define NVGSP_FUD_V2_SIZE			60
#define NVGSP_FUD_V2_HDR_VER_AVAIL		0x01u
#define NVGSP_FUD_V2_HDR_VER			2

/* ----- Implementation ----------------------------------------------- */

/*
 * Find the Nth FwSec V2 descriptor candidate in PROM. Two candidates
 * with the same code/dmem sizes typically exist: one signed for
 * "debug" fused chips and one for "production" fused chips. The chip's
 * HS hardware accepts only the matching one and rejects the other with
 * a silent halt + DEAD5EC3 scrub of the SEC IMEM. We try them in order.
 */
static int
nvgsp_fwsec_find_v2_nth(struct nvgsp_state *sc, uint32_t skip,
    uint32_t *out_offset)
{
	uint32_t a;
	uint32_t seen = 0;

	if (sc->vbios == NULL || sc->vbios_size < 64)
		return (ENXIO);

	for (a = 0; a + NVGSP_FUD_V2_SIZE <= sc->vbios_size; a += 4) {
		uint32_t hdr = nvgsp_le32(&sc->vbios[a]);

		if ((hdr & 0xff) & NVGSP_FUD_V2_HDR_VER_AVAIL &&
		    ((hdr >> 8) & 0xff) == NVGSP_FUD_V2_HDR_VER &&
		    ((hdr >> 16) & 0xffff) == NVGSP_FUD_V2_SIZE) {
			if (seen++ == skip) {
				*out_offset = a;
				return (0);
			}
		}
	}
	return (ENOENT);
}

static int
nvgsp_fwsec_find_v2(struct nvgsp_state *sc, uint32_t *out_offset)
{
	return nvgsp_fwsec_find_v2_nth(sc, 0, out_offset);
}

static void
nvgsp_fwsec_patch_dmem(struct nvgsp_state *sc, uint8_t *dmem, uint32_t dmem_size,
    uint32_t intf_off, uint32_t init_cmd, uint64_t frts_addr_bytes,
    uint32_t frts_size_bytes)
{
	struct nvgsp_appif_hdr_v1 *hdr;
	uint32_t entry_off;
	int i;

	if (intf_off + sizeof(*hdr) > dmem_size) {
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: appif hdr OOB (intf=0x%x dmem=%u)\n",
		    intf_off, dmem_size);
		return;
	}
	hdr = (struct nvgsp_appif_hdr_v1 *)(dmem + intf_off);
	if (hdr->ver != 1) {
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: appif unsupported version %u\n", hdr->ver);
		return;
	}

	entry_off = intf_off + hdr->hdr;
	for (i = 0; i < hdr->cnt; i++) {
		struct nvgsp_appif_entry_v1 *ent;
		struct nvgsp_dmemmap_v3 *dmm;
		struct nvgsp_fwsec_frts_cmd *cmd;

		if (entry_off + sizeof(*ent) > dmem_size)
			break;
		ent = (struct nvgsp_appif_entry_v1 *)(dmem + entry_off);

		if (ent->id != NVGSP_APPIF_ID_DMEMMAPPER) {
			entry_off += hdr->len;
			continue;
		}

		if (ent->dmem_offset + sizeof(*dmm) > dmem_size) {
			nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: dmemmap OOB (off=0x%x)\n",
			    ent->dmem_offset);
			return;
		}
		dmm = (struct nvgsp_dmemmap_v3 *)(dmem + ent->dmem_offset);
		dmm->init_cmd = init_cmd;

		if (dmm->cmd_in_buffer_offset + sizeof(*cmd) > dmem_size) {
			nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: cmd buf OOB (off=0x%x)\n",
			    dmm->cmd_in_buffer_offset);
			return;
		}
		cmd = (struct nvgsp_fwsec_frts_cmd *)(dmem + dmm->cmd_in_buffer_offset);
		memset(cmd, 0, sizeof(*cmd));
		cmd->read_vbios.ver = 1;
		cmd->read_vbios.hdr = sizeof(cmd->read_vbios);
		cmd->read_vbios.flags = 2;
		if (init_cmd == NVGSP_DMEMMAP_CMD_FRTS) {
			cmd->frts_region.ver = 1;
			cmd->frts_region.hdr = sizeof(cmd->frts_region);
			cmd->frts_region.addr_4k =
			    (uint32_t)(frts_addr_bytes >> 12);
			cmd->frts_region.size_4k = frts_size_bytes >> 12;
			cmd->frts_region.media_type = NVGSP_FRTS_MEDIA_FB;
		}

		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: patched DMEM appif id=%u dmem_off=0x%x "
		    "cmd_in=0x%x init_cmd=0x%x frts=0x%jx+%u KiB\n",
		    ent->id, ent->dmem_offset, dmm->cmd_in_buffer_offset,
		    init_cmd, (uintmax_t)frts_addr_bytes,
		    frts_size_bytes >> 10);
		return;
	}
	nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: no DMEMMAPPER entry in appif (cnt=%u)\n", hdr->cnt);
}

int
nvgsp_fwsec_run_cmd(struct nvgsp_state *sc, uint32_t init_cmd,
    uint64_t frts_addr, uint32_t frts_size)
{
	uint32_t desc_off;
	const struct nvgsp_fud_v2 *desc;
	const uint8_t *body;
	uint32_t imem_total, dmem_size, ucode_size;
	struct nvgsp_dmamem fw_dma;
	struct nvgsp_falcon *sec2 = sc->gsp; /* FwSec runs on GSP-Falcon, not SEC2 */
	const struct firmware *bl_fw;
	char bl_name[128];
	const struct nvgsp_bin_hdr *bl_bh;
	const struct nvgsp_bl_desc *bl_bd;
	struct nvgsp_bl_dmem_desc_v2 bl_desc;
	uint32_t mb0, mb1, cpuctl, wpr2_lo, wpr2_hi;
	int error;

	KTR_LOG(nvgpu_gsp_fwsec_stage, 0u, init_cmd, 0);
	nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: entry gsp_flcn=%p vbios=%p sz=%u\n",
	    sec2, sc->vbios, sc->vbios_size);
	if (sec2 == NULL || sc->vbios == NULL)
		return (ENXIO);

	/*
	 * VBIOS stitch is ONLY needed when the legacy BIOS POST didn't
	 * run for the GPU (e.g. OVMF + x-vga passthrough with no CSM).
	 * On bare-metal DragonFly the host BIOS posts the GPU normally
	 * and NV_PDISP_VGA_CR already advertises a valid staged VBIOS;
	 * stitching on top would corrupt the BIOS's chosen layout.
	 */
	{
		uint32_t vga = nvgsp_rd32(sc, NV_PDISP_VGA_CR);
		bool already_staged = (vga & NV_PDISP_VGA_CR_ENABLED) &&
		    (vga & NV_PDISP_VGA_CR_TARGET_MASK) ==
		     NV_PDISP_VGA_CR_TARGET_VRAM;

		if (already_staged) {
			nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: legacy POST already staged VBIOS "
			    "(VGA_CR=0x%08x), skipping software stitch\n",
			    vga);
		} else {
			uint32_t lmr_v = nvgsp_rd32(sc, 0x100ce0);
			uint32_t lmag_v = (lmr_v & 0x3f0u) >> 4;
			uint32_t lsca_v = lmr_v & 0xfu;
			uint64_t fb_sz  = (uint64_t)lmag_v << (lsca_v + 20);
			uint64_t bios_addr_v;
			uint32_t saved_pramin, vga_val, verify;
			uint32_t pramin_base;
			uint32_t copy_size;
			uint32_t i;

			if (lmr_v & 0x40000000u)
				fb_sz = fb_sz / 16 * 15;
			bios_addr_v = fb_sz - 0x100000;

			copy_size = sc->vbios_size;
			if (copy_size > NV_PRAMIN_SIZE)
				copy_size = NV_PRAMIN_SIZE;

			saved_pramin = nvgsp_rd32(sc, NV_PBUS_PRAMIN);
			pramin_base  = (uint32_t)(bios_addr_v >> 16);
			nvgsp_wr32(sc, NV_PBUS_PRAMIN, pramin_base);

			for (i = 0; i + 4 <= copy_size; i += 4) {
				uint32_t w =
				    (uint32_t)sc->vbios[i + 0]        |
				    ((uint32_t)sc->vbios[i + 1] << 8) |
				    ((uint32_t)sc->vbios[i + 2] << 16)|
				    ((uint32_t)sc->vbios[i + 3] << 24);
				nvgsp_wr32(sc, NV_PRAMIN + i, w);
			}
			verify = nvgsp_rd32(sc, NV_PRAMIN);

			nvgsp_wr32(sc, NV_PBUS_PRAMIN, saved_pramin);

			vga_val = ((uint32_t)(bios_addr_v >> 8) &
			    0xffffff00u) |
			    NV_PDISP_VGA_CR_ENABLED |
			    NV_PDISP_VGA_CR_TARGET_VRAM;
			nvgsp_wr32(sc, NV_PDISP_VGA_CR, vga_val);

			nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: stitched %u B VBIOS -> VRAM 0x%llx; "
			    "first_word=0x%08x (raw[0..3]=%02x %02x %02x %02x) "
			    "VGA_CR set to 0x%08x (was 0x%08x)\n",
			    copy_size, (unsigned long long)bios_addr_v, verify,
			    sc->vbios[0], sc->vbios[1], sc->vbios[2],
			    sc->vbios[3], vga_val, vga);
		}
	}

	/*
	 * Recompute frts_addr/size from the GPU's actual FB layout the
	 * way nouveau tu102_gsp_oneinit / tu102_gsp_vga_workspace_addr do:
	 *   fb_size       = gp102_fb_vidmem_size (decoded from 0x100ce0)
	 *   bios.addr     = vga_workspace addr (top 1 MiB or 128 KiB, see
	 *                   NV_PDISP_VGA_CR @ 0x625f04 -- if VGA aperture
	 *                   is enabled and aimed at VRAM use the staged
	 *                   address, else fb_size - 0x100000)
	 *   frts.size     = 0x100000 (1 MiB, fixed)
	 *   frts.addr     = ALIGN_DOWN(bios.addr, 0x20000) - frts.size
	 * Override the caller-supplied frts_addr/size if the GPU disagrees.
	 */
	{
		uint32_t lmr = nvgsp_rd32(sc, 0x100ce0);
		uint32_t lmag = (lmr & 0x000003f0u) >> 4;
		uint32_t lsca = (lmr & 0x0000000fu);
		uint64_t fb_size = (uint64_t)lmag << (lsca + 20);
		uint32_t vga = nvgsp_rd32(sc, NV_PDISP_VGA_CR);
		uint64_t bios_addr;

		if (lmr & 0x40000000u)
			fb_size = fb_size / 16 * 15;

		bios_addr = fb_size - 0x100000;
		if (vga & NV_PDISP_VGA_CR_ENABLED) {
			uint64_t staged = ((uint64_t)(vga & 0xffffff00u)) << 8;
			/*
			 * Per nouveau tu102_gsp_vga_workspace_addr: on cards
			 * with > 4 GiB VRAM, the 24-bit address field in
			 * 0x625f04 cannot encode top-of-VRAM, so the BIOS
			 * stages at a low address. In that case use
			 * fb_size - 0x20000 so WPR2 lands near the top and
			 * has room below for the GSP image + heap.
			 */
			if (staged < bios_addr)
				bios_addr = fb_size - 0x20000;
			else
				bios_addr = staged;
		}

		frts_size = 0x100000u;
		frts_addr = (bios_addr & ~(uint64_t)0x1ffffu) - frts_size;

		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: FB lmr=0x%08x size=0x%llx vga=0x%08x "
		    "bios_addr=0x%llx -> frts=0x%llx+%uKiB\n",
		    lmr, (unsigned long long)fb_size, vga,
		    (unsigned long long)bios_addr,
		    (unsigned long long)frts_addr, frts_size >> 10);
	}

	/*
	 * Bring the GSP-Falcon to a known state before touching any of its
	 * registers. On TU102 post-OVMF the engine may be in a state where
	 * naive PRI reads hang the bus and crash the VM. The reset register
	 * at base+0x3c0 appears to be safe to write blindly.
	 */
	nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: resetting GSP-Falcon...\n");
	error = nvgsp_falcon_reset_eng(sec2);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: GSP-Falcon reset_eng failed (%d)\n", error);
		return (error);
	}
	KTR_LOG(nvgpu_gsp_fwsec_stage, 1u, init_cmd, 0);
	nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: GSP-Falcon post-reset HWCFG=0x%08x HWCFG2=0x%08x "
	    "DMACTL=0x%08x CPUCTL=0x%08x\n",
	    nvgsp_falcon_rd32(sec2, NVGSP_FLCN_HWCFG),
	    nvgsp_falcon_rd32(sec2, NVGSP_FLCN_HWCFG2),
	    nvgsp_falcon_rd32(sec2, NVGSP_FLCN_DMACTL),
	    nvgsp_falcon_rd32(sec2, NVGSP_FLCN_CPUCTL));

	/* 1. Find FwSec V2 descriptor in PROM */
	/*
	 * Try the SECOND V2 candidate first (the production-signed one).
	 * The first match in PROM is often the DBG variant and gets
	 * rejected with a DEAD5EC3 IMEM scrub on retail chips. Fall back
	 * to the first if the second isn't found.
	 */
	error = nvgsp_fwsec_find_v2_nth(sc, 1, &desc_off);
	if (error != 0)
		error = nvgsp_fwsec_find_v2(sc, &desc_off);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: no V2 desc found\n");
		return (error);
	}
	KTR_LOG(nvgpu_gsp_fwsec_stage, 2u, init_cmd, 0);
	desc = (const struct nvgsp_fud_v2 *)(sc->vbios + desc_off);
	body = sc->vbios + desc_off + NVGSP_FUD_V2_SIZE;

	nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: V2 desc @ 0x%05x imem_load=%u imem_sec=%u imem_virt=0x%x "
	    "dmem_off=0x%x dmem_load=%u intf=0x%x\n",
	    desc_off, desc->imem_load_size, desc->imem_sec_size,
	    desc->imem_virt_base, desc->dmem_offset, desc->dmem_load_size,
	    desc->interface_offset);
	nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: V2 more imem_phys=0x%x imem_sec_base=0x%x dmem_phys=0x%x "
	    "ventry=0x%x stored_size=%u\n",
	    desc->imem_phys_base, desc->imem_sec_base, desc->dmem_phys_base,
	    desc->virtual_entry, desc->stored_size);

	imem_total = desc->imem_load_size;
	dmem_size  = desc->dmem_load_size;
	if (desc_off + NVGSP_FUD_V2_SIZE + desc->dmem_offset + dmem_size >
	    sc->vbios_size)
		return (EIO);

	/* 2. Allocate DMA-coherent staging: imem block + dmem block.
	 * Open-rm aligns code and data sections to 256 bytes individually. */
	ucode_size = roundup(imem_total, 256) + roundup(dmem_size, 256);
	error = nvgsp_dma_alloc_dmamem(sc, ucode_size, 4096, &fw_dma);
	if (error != 0)
		return (error);

	/* Copy IMEM at offset 0; DMEM at offset roundup(imem_total, 256). */
	memcpy(fw_dma.kva, body, imem_total);
	{
		uint32_t dmem_dst = roundup(imem_total, 256);
		memcpy((uint8_t *)fw_dma.kva + dmem_dst,
		    body + desc->dmem_offset, dmem_size);

		/* 3. Patch DMEM (in our buffer) with FRTS command. */
		nvgsp_fwsec_patch_dmem(sc,
		    (uint8_t *)fw_dma.kva + dmem_dst, dmem_size,
		    desc->interface_offset,
		    init_cmd,
		    frts_addr, frts_size);
	}
	KTR_LOG(nvgpu_gsp_fwsec_stage, 3u, init_cmd, 0);

	/*
	 * 4. Build flcn_bl_dmem_desc_v2 pointing at the staged ucode.
	 * Fields match nouveau nvgsp_fwsec_v2 / tu102_gsp_fwsec_load_bld:
	 *   non_sec_code_off/size = IMEMPhysBase / (IMEMLoadSize - IMEMSecSize)
	 *   sec_code_off/size     = IMEMSecBase  / IMEMSecSize  (raw, no rounding)
	 *   code_dma_base         = sysmem PA of the IMEM portion (start of buffer)
	 *   data_dma_base         = sysmem PA of the DMEM portion
	 */
	memset(&bl_desc, 0, sizeof(bl_desc));
	bl_desc.ctx_dma = NVGSP_FLCN_DMAIDX_PHYS_SYS_NCOH;
	bl_desc.code_dma_base     = fw_dma.paddr;
	bl_desc.non_sec_code_off  = desc->imem_phys_base;
	bl_desc.non_sec_code_size = desc->imem_load_size - desc->imem_sec_size;
	bl_desc.sec_code_off      = desc->imem_sec_base;
	bl_desc.sec_code_size     = desc->imem_sec_size;
	bl_desc.code_entry_point  = 0;
	bl_desc.data_dma_base     = fw_dma.paddr + roundup(imem_total, 256);
	bl_desc.data_size         = dmem_size;
	bl_desc.argc = 0;
	bl_desc.argv = 0;

	nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: BL desc ctx_dma=%u code_dma=0x%llx "
	    "ns_off=0x%x ns_sz=%u sec_off=0x%x sec_sz=%u "
	    "data_dma=0x%llx data_sz=%u entry=0x%x\n",
	    bl_desc.ctx_dma, (unsigned long long)bl_desc.code_dma_base,
	    bl_desc.non_sec_code_off, bl_desc.non_sec_code_size,
	    bl_desc.sec_code_off, bl_desc.sec_code_size,
	    (unsigned long long)bl_desc.data_dma_base, bl_desc.data_size,
	    bl_desc.code_entry_point);

	/* 5. Get the generic ACR bootloader firmware. */
	error = nvgpu_chip_format_firmware_name(sc->chip,
	    NVGPU_FIRMWARE_ACR_BL, bl_name, sizeof(bl_name));
	if (error != 0) {
		nvgsp_dma_free_dmamem(sc, &fw_dma);
		return (error);
	}

	bl_fw = firmware_get(bl_name);
	if (bl_fw == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: %s firmware not loaded\n", bl_name);
		nvgsp_dma_free_dmamem(sc, &fw_dma);
		return (ENOENT);
	}
	if (bl_fw->datasize < sizeof(*bl_bh) + sizeof(*bl_bd)) {
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: acr/bl too small\n");
		firmware_put(bl_fw, FIRMWARE_UNLOAD);
		nvgsp_dma_free_dmamem(sc, &fw_dma);
		return (EIO);
	}
	bl_bh = (const struct nvgsp_bin_hdr *)bl_fw->data;
	bl_bd = (const struct nvgsp_bl_desc *)
	    ((const uint8_t *)bl_fw->data + bl_bh->header_offset);
	KTR_LOG(nvgpu_gsp_fwsec_stage, 4u, init_cmd, 0);

	nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: bl bin_size=%u hdr@0x%x data@0x%x; "
	    "bl start_tag=0x%x code_off=0x%x code_size=%u data_size=%u\n",
	    bl_bh->bin_size, bl_bh->header_offset, bl_bh->data_offset,
	    bl_bd->start_tag, bl_bd->code_off, bl_bd->code_size,
	    bl_bd->data_size);

	/*
	 * 6. Configure the engine for the no-inst BL path (matches
	 * nouveau gm200_flcn_fw_load when fw->inst == NULL):
	 *   - 0x624 mask 0x80 -- enable arb-on-noctx scheduling
	 *   - DMACTL = 0       -- clear REQUIRE_CTX (BL uses TRANSCFG slot)
	 *   - FBIF TRANSCFG[ctx_dma] = 0x5 (TARGET=COH_SYS, MEM_TYPE=PHYS)
	 *     so the BL's DMA fetches from code_dma_base hit our sysmem
	 *     buffer (which is bus_dma-coherent).
	 */
	nvgsp_falcon_mask(sec2, 0x624u, 0x00000080u, 0x00000080u);
	/*
	 * Per open-rm kflcnDisableCtxReq_TU102: must set FBIF_CTL bit
	 * ALLOW_PHYS_NO_CTX (so DMA from physical sysmem proceeds without
	 * a context binding) AND clear DMACTL.REQUIRE_CTX. Doing only
	 * DMACTL=0 is not enough -- the FBIF will reject the BL's DMA
	 * and HS authentication will then silently fail (SCTL=0x3000,
	 * mb0 untouched). Use the existing helper.
	 */
	nvgsp_falcon_disable_ctx_req(sec2);
	nvgsp_falcon_mask(sec2, NVGSP_FBIF_TRANSCFG(bl_desc.ctx_dma),
	    0x7u, NVGSP_FBIF_TRANSCFG_NCOH_PHYS);

	/*
	 * (DMA self-test confirmed sysmem->Falcon DMA works. The bug was
	 * that nvgsp_falcon_disable_ctx_req was only updating FBIF_CTL and
	 * not clearing DMACTL, so REQUIRE_CTX kept blocking the BL's DMA.
	 * Both are now done in the helper.)
	 */

	/*
	 * 7. PIO-load the BL into IMEM. Per nouveau gm200_flcn_fw_load,
	 * dest = code.limit - boot_size; tag = boot_addr >> 8 = start_tag.
	 * For our 64 KiB SEC2/GSP IMEM with boot_size=512, dest=0xFE00,
	 * tag=0xFD. The Falcon IMEM tag table thus maps virtual PC
	 * 0xFD00..0xFEFF to physical blocks 0xFE..0xFF. Must come BEFORE
	 * the DMEM desc load (matches nouveau order).
	 */
	{
		uint32_t imem_size_bytes =
		    (nvgsp_falcon_rd32(sec2, NVGSP_FLCN_HWCFG) &
		     NVGSP_FLCN_HWCFG_IMEM_SIZE_MASK) *
		    NVGSP_FLCN_IMEM_BLKSIZE;
		uint32_t imem_dst = imem_size_bytes - bl_bd->code_size;
		const uint8_t *src = (const uint8_t *)bl_fw->data +
		    bl_bh->data_offset + bl_bd->code_off;
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: BL upload imem_size=%u dst=0x%x tag=0x%x size=%u\n",
		    imem_size_bytes, imem_dst, bl_bd->start_tag,
		    bl_bd->code_size);
		error = nvgsp_falcon_load_imem(sec2, src, imem_dst,
		    bl_bd->code_size,
		    bl_bd->start_tag, 0, false);
		if (error != 0) {
			nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: imem bl load failed (%d)\n", error);
			goto out;
		}
	}
	KTR_LOG(nvgpu_gsp_fwsec_stage, 5u, init_cmd, 0);

	/* 8. Now write the BL DMEM descriptor at DMEM offset 0. */
	error = nvgsp_falcon_load_dmem(sec2, &bl_desc, 0, sizeof(bl_desc), 0);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: dmem desc load failed (%d)\n",
		    error);
		goto out;
	}
	KTR_LOG(nvgpu_gsp_fwsec_stage, 6u, init_cmd, 0);

	/*
	 * 9. Set BOOTVEC, initialize mb0 to a sentinel so we can tell
	 * whether FwSec actually wrote it, start, wait halt.
	 * (Nouveau pre-writes 0xcafebeef when no explicit mbox is passed.)
	 */
	nvgsp_falcon_set_bootvec(sec2, bl_bd->start_tag << 8);
	nvgsp_falcon_wr32(sec2, NVGSP_FLCN_MAILBOX0, 0);
	nvgsp_falcon_wr32(sec2, NVGSP_FLCN_MAILBOX1, 0);

	nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: starting SEC2 (bootvec=0x%x ctx_dma=%u)\n",
	    bl_bd->start_tag << 8, bl_desc.ctx_dma);

	KTR_LOG(nvgpu_gsp_fwsec_stage, 7u, init_cmd, 0);
	nvgsp_falcon_start(sec2);
	error = nvgsp_falcon_wait_for_halt(sec2, 5000000);	/* 5 s */
	KTR_LOG(nvgpu_gsp_fwsec_stage, 8u, init_cmd, error);

	mb0 = nvgsp_falcon_rd32(sec2, NVGSP_FLCN_MAILBOX0);
	mb1 = nvgsp_falcon_rd32(sec2, NVGSP_FLCN_MAILBOX1);
	cpuctl = nvgsp_falcon_rd32(sec2, NVGSP_FLCN_CPUCTL);
	wpr2_lo = nvgsp_rd32(sc, 0x001fa824);
	wpr2_hi = nvgsp_rd32(sc, 0x001fa828);

	nvgpu_log(NVGPU_LOG_DEBUG, "fwsec[%s]: %s mb0=0x%08x mb1=0x%08x cpuctl=0x%08x\n",
	    init_cmd == NVGSP_DMEMMAP_CMD_FRTS ? "FRTS" :
	    init_cmd == NVGSP_DMEMMAP_CMD_SB   ? "SB"   : "?",
	    error == 0 ? "halted" : "TIMEOUT",
	    mb0, mb1, cpuctl);
	nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: post-run WPR2 lo=0x%08x hi=0x%08x\n",
	    wpr2_lo, wpr2_hi);

	/*
	 * Dump IMEM[0x300..0x320] and IMEM[0x0..0x20]. If the BL's DMA
	 * successfully loaded NS code at IMEM[0..768) and SEC code at
	 * IMEM[768..38400), these should contain real Falcon instructions
	 * (non-zero). If they're all zero, the BL's DMA didn't actually
	 * pull the code from sysmem and HS auth ran on zeros.
	 */
	{
		uint32_t imemc, w0, w1, w2, w3;
		/* IMEM[0x300] read */
		imemc = (0x300u & 0xffffu) | (1u << 25); /* AINCR */
		nvgsp_falcon_wr32(sec2, 0x180, imemc); /* IMEMC(0) */
		w0 = nvgsp_falcon_rd32(sec2, 0x184); /* IMEMD(0) */
		w1 = nvgsp_falcon_rd32(sec2, 0x184);
		w2 = nvgsp_falcon_rd32(sec2, 0x184);
		w3 = nvgsp_falcon_rd32(sec2, 0x184);
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: IMEM[0x300..0x310] = %08x %08x %08x %08x "
		    "(should match VBIOS body[0x300..])\n", w0, w1, w2, w3);
		/* IMEM[0x0] read (NS code start) */
		imemc = (0x0u & 0xffffu) | (1u << 25);
		nvgsp_falcon_wr32(sec2, 0x180, imemc);
		w0 = nvgsp_falcon_rd32(sec2, 0x184);
		w1 = nvgsp_falcon_rd32(sec2, 0x184);
		w2 = nvgsp_falcon_rd32(sec2, 0x184);
		w3 = nvgsp_falcon_rd32(sec2, 0x184);
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: IMEM[0x0..0x10] = %08x %08x %08x %08x "
		    "(NS code start)\n", w0, w1, w2, w3);
	}
	/*
	 * Per nouveau nvgsp_fwsec_frts: real FRTS status lives in
	 * PMC scratch[0xE] @ 0x001438. Upper 16 bits = error code,
	 * 0 = success. SB uses scratch[0x15]. Also dump a few neighbors
	 * for context.
	 */
	{
		uint32_t sctl   = nvgsp_falcon_rd32(sec2, 0x240);
		uint32_t exci   = nvgsp_falcon_rd32(sec2, 0x024); /* EXCI */
		uint32_t irqstat= nvgsp_falcon_rd32(sec2, 0x008); /* IRQSTAT */
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: SCTL=0x%08x EXCI=0x%08x IRQSTAT=0x%08x\n",
		    sctl, exci, irqstat);
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: scratch[0..7] %08x %08x %08x %08x %08x %08x %08x %08x\n",
		    nvgsp_rd32(sc, 0x001400), nvgsp_rd32(sc, 0x001404),
		    nvgsp_rd32(sc, 0x001408), nvgsp_rd32(sc, 0x00140c),
		    nvgsp_rd32(sc, 0x001410), nvgsp_rd32(sc, 0x001414),
		    nvgsp_rd32(sc, 0x001418), nvgsp_rd32(sc, 0x00141c));
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: scratch[8..f] %08x %08x %08x %08x %08x %08x %08x %08x\n",
		    nvgsp_rd32(sc, 0x001420), nvgsp_rd32(sc, 0x001424),
		    nvgsp_rd32(sc, 0x001428), nvgsp_rd32(sc, 0x00142c),
		    nvgsp_rd32(sc, 0x001430), nvgsp_rd32(sc, 0x001434),
		    nvgsp_rd32(sc, 0x001438), nvgsp_rd32(sc, 0x00143c));
		nvgpu_log(NVGPU_LOG_DEBUG, "fwsec: scratch[14..17] %08x %08x %08x %08x "
		    "(sb_err lo16 of [15])\n",
		    nvgsp_rd32(sc, 0x001450), nvgsp_rd32(sc, 0x001454),
		    nvgsp_rd32(sc, 0x001458), nvgsp_rd32(sc, 0x00145c));
	}

out:
	KTR_LOG(nvgpu_gsp_fwsec_stage, 9u, init_cmd, error);
	firmware_put(bl_fw, FIRMWARE_UNLOAD);
	nvgsp_dma_free_dmamem(sc, &fw_dma);
	return (error);
}
