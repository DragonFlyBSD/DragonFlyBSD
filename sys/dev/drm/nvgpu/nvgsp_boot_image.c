/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GSP-RM boot staging: prepare everything booter_load needs to put GSP
 * onto the RISC-V core.
 *
 * Inputs (all in sysmem, all DMA-coherent):
 *   - GSP-RM ELF image (gsp-570.144.bin, ~27 MiB)
 *     Reachable to the GPU via a 3-level "radix3" page table.
 *   - GSP bootloader image (bootloader-570.144.bin, ~4 KiB)
 *     Reachable as a single contiguous DMA region. Its
 *     RM_RISCV_UCODE_DESC header carries the monitorCode /
 *     monitorData / manifest offsets the booter needs.
 *   - GspFwWprMeta (256 B): the handoff struct we already
 *     allocated in nvgsp_meta.c. Filled here with all the
 *     addresses + sizes + FB-layout offsets booter expects.
 *
 * References:
 *   linux nouveau:
 *     drivers/gpu/drm/nouveau/nvkm/subdev/gsp/tu102.c
 *     drivers/gpu/drm/nouveau/nvkm/subdev/gsp/rm/r535/gsp.c
 *       (nvgsp_radix3_sg, r535_gsp_rm_boot_ctor)
 *   open-rm 570.144:
 *     src/nvidia/arch/nvalloc/common/inc/gsp/gsp_fw_wpr_meta.h
 *     src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_tu102.c
 */

#include "nvgsp_priv.h"

#include <sys/firmware.h>	/* firmware_get / firmware_put */

/*
 * Minimal ELF64 little-endian section-table parser. The GSP-RM image
 * ships as a relocatable ELF carrying:
 *   .fwimage              -- the actual GSP-RM ucode bytes (~28 MiB)
 *   .fwsignature_tu10x    -- the 4 KiB signature this chip needs
 *   (plus per-arch signatures for other chips, and .fwversion)
 * The booter wants the .fwimage section as the radix3-mapped sysmem
 * payload and the .fwsignature_<arch> section pointed to by
 * GspFwWprMeta.sysmemAddrOfSignature -- it walks .fwimage and verifies
 * against the signature. Passing the whole ELF blob as the "image"
 * (which is what we did initially) makes the booter reject with a
 * generic INVALID_ACCESS_TYPE (0x1d) because the bytes it walks bear
 * no resemblance to the signed RM code.
 */
struct nvgsp_elf64_hdr {
	uint8_t  e_ident[16];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
} __packed;

struct nvgsp_elf64_shdr {
	uint32_t sh_name;
	uint32_t sh_type;
	uint64_t sh_flags;
	uint64_t sh_addr;
	uint64_t sh_offset;
	uint64_t sh_size;
	uint32_t sh_link;
	uint32_t sh_info;
	uint64_t sh_addralign;
	uint64_t sh_entsize;
} __packed;

static int
nvgsp_boot_image_find_elf_section(const uint8_t *data, uint32_t data_size, const char *name,
    uint64_t *out_off, uint64_t *out_size)
{
	const struct nvgsp_elf64_hdr *eh;
	const struct nvgsp_elf64_shdr *sh, *shstr;
	const char *names;
	uint16_t i;

	if (data_size < sizeof(*eh))
		return (EIO);
	eh = (const struct nvgsp_elf64_hdr *)data;
	if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
	    eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F' ||
	    eh->e_ident[4] != 2 /* ELFCLASS64 */ ||
	    eh->e_ident[5] != 1 /* ELFDATA2LSB */)
		return (EIO);
	if (eh->e_shoff + (uint64_t)eh->e_shnum * eh->e_shentsize > data_size ||
	    eh->e_shstrndx >= eh->e_shnum)
		return (EIO);

	sh = (const struct nvgsp_elf64_shdr *)(data + eh->e_shoff);
	shstr = &sh[eh->e_shstrndx];
	if (shstr->sh_offset + shstr->sh_size > data_size)
		return (EIO);
	names = (const char *)data + shstr->sh_offset;

	for (i = 0; i < eh->e_shnum; i++) {
		if (sh[i].sh_name >= shstr->sh_size)
			continue;
		if (strcmp(names + sh[i].sh_name, name) != 0)
			continue;
		if (sh[i].sh_offset + sh[i].sh_size > data_size)
			return (EIO);
		*out_off  = sh[i].sh_offset;
		*out_size = sh[i].sh_size;
		return (0);
	}
	return (ENOENT);
}

static MALLOC_DEFINE(M_NVGSP_BOOT, "nvgsp_boot", "nvgsp GSP boot stage");

#define NVGSP_PAGE_SIZE	4096u
#define NVGSP_PAGE_SHIFT	12

/* Wire-format RM_RISCV_UCODE_DESC -- the header inside bootloader.bin. */
struct nvgsp_riscv_ucode_desc {
	uint32_t version;
	uint32_t bootloaderOffset;
	uint32_t bootloaderSize;
	uint32_t bootloaderParamOffset;
	uint32_t bootloaderParamSize;
	uint32_t riscvElfOffset;
	uint32_t riscvElfSize;
	uint32_t appVersion;
	uint32_t manifestOffset;
	uint32_t manifestSize;
	uint32_t monitorDataOffset;
	uint32_t monitorDataSize;
	uint32_t monitorCodeOffset;
	uint32_t monitorCodeSize;
	uint32_t bIsMonitorEnabled;
	uint32_t swbromCodeOffset;
	uint32_t swbromCodeSize;
	uint32_t swbromDataOffset;
	uint32_t swbromDataSize;
} __packed;

/* Same wrapping bin_hdr we already use for booter/acr_bl. */
struct nvgsp_bin_hdr {
	uint32_t bin_magic;	/* 0x10de */
	uint32_t bin_ver;
	uint32_t bin_size;
	uint32_t header_offset;
	uint32_t data_offset;
	uint32_t data_size;
} __packed;

struct nvgsp_wpr_meta;	/* defined in nvgsp_meta.c */

/*
 * Build a 3-level page table over a single contiguous DMA region.
 *  L0: 1 page, 1 entry  -> bus addr of L1
 *  L1: 1 page, N entries -> bus addrs of each L2 page (N = ceil(npages/512))
 *  L2: each page covers 512 data pages -> bus addrs of data pages
 *
 * All levels live in the SAME page-table dmamem allocation (sc->gsp_radix3),
 * one 4-KiB page after another, so we don't need separate allocations for
 * each level. L0 sits at offset 0, L1 at offset 4 KiB, L2 starts at 8 KiB.
 *
 * Returns the sysmem bus address of L0 (= sysmemAddrOfRadix3Elf).
 */
static uint64_t
nvgsp_boot_image_build_radix3(struct nvgsp_state *sc, uint64_t data_pa,
    uint32_t data_size)
{
	uint32_t n_data_pg = (data_size + NVGSP_PAGE_SIZE - 1) /
	    NVGSP_PAGE_SIZE;
	uint32_t n_l2_pg = (n_data_pg + 511) / 512;
	uint64_t *l0, *l1, *l2;
	uint64_t l1_pa, l2_pa_base;
	uint32_t i;

	if (sc->gsp_radix3.kva == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: radix3 alloc missing\n");
		return (0);
	}

	l0 = (uint64_t *)((uint8_t *)sc->gsp_radix3.kva + 0);
	l1 = (uint64_t *)((uint8_t *)sc->gsp_radix3.kva + NVGSP_PAGE_SIZE);
	l2 = (uint64_t *)((uint8_t *)sc->gsp_radix3.kva + 2 * NVGSP_PAGE_SIZE);
	l1_pa      = sc->gsp_radix3.paddr + NVGSP_PAGE_SIZE;
	l2_pa_base = sc->gsp_radix3.paddr + 2 * NVGSP_PAGE_SIZE;

	memset(sc->gsp_radix3.kva, 0, (2 + n_l2_pg) * NVGSP_PAGE_SIZE);

	l0[0] = l1_pa;
	for (i = 0; i < n_l2_pg; i++)
		l1[i] = l2_pa_base + (uint64_t)i * NVGSP_PAGE_SIZE;
	for (i = 0; i < n_data_pg; i++)
		l2[i] = data_pa + (uint64_t)i * NVGSP_PAGE_SIZE;

	nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: radix3 built: data %u pages, L2 %u pages; "
	    "L0=0x%llx L1=0x%llx L2=0x%llx\n",
	    n_data_pg, n_l2_pg,
	    (unsigned long long)sc->gsp_radix3.paddr,
	    (unsigned long long)l1_pa,
	    (unsigned long long)l2_pa_base);
	return (sc->gsp_radix3.paddr);
}

int
nvgsp_boot_prepare_image(struct nvgsp_state *sc)
{
	const struct firmware *gsp_fw, *bl_fw;
	char gsp_name[128], bootloader_name[128];
	const struct nvgsp_bin_hdr *bl_hdr;
	const struct nvgsp_riscv_ucode_desc *bl_desc;
	struct nvgsp_wpr_meta *meta;
	uint32_t img_size, img_pages, l2_pages, radix3_alloc_size;
	uint32_t bl_size, bl_payload_off, bl_payload_size;
	uint64_t l0_pa;
	int error;

	if (sc->wpr_meta.kva == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: wpr_meta not allocated\n");
		return (ENXIO);
	}

	error = nvgpu_chip_format_firmware_name(sc->chip,
	    NVGPU_FIRMWARE_GSP, gsp_name, sizeof(gsp_name));
	if (error != 0)
		return (error);

	gsp_fw = firmware_get(gsp_name);
	if (gsp_fw == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: %s firmware not loaded\n", gsp_name);
		return (ENOENT);
	}
	error = nvgpu_chip_format_firmware_name(sc->chip,
	    NVGPU_FIRMWARE_BOOTLOADER, bootloader_name, sizeof(bootloader_name));
	if (error != 0) {
		firmware_put(gsp_fw, FIRMWARE_UNLOAD);
		return (error);
	}

	bl_fw = firmware_get(bootloader_name);
	if (bl_fw == NULL) {
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: %s firmware not loaded\n",
		    bootloader_name);
		firmware_put(gsp_fw, FIRMWARE_UNLOAD);
		return (ENOENT);
	}

	{
		uint64_t fwimage_off, fwimage_size;
		uint64_t sig_off, sig_size;

		error = nvgsp_boot_image_find_elf_section(gsp_fw->data, gsp_fw->datasize,
		    ".fwimage", &fwimage_off, &fwimage_size);
		if (error != 0) {
			nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: ELF has no .fwimage (%d)\n", error);
			goto out_put;
		}
		error = nvgsp_boot_image_find_elf_section(gsp_fw->data, gsp_fw->datasize,
		    sc->chip->fw_signature, &sig_off, &sig_size);
		if (error != 0) {
			nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: ELF has no %s (%d)\n",
			    sc->chip->fw_signature, error);
			goto out_put;
		}
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: ELF .fwimage @0x%llx+%llu %s @0x%llx+%llu\n",
		    (unsigned long long)fwimage_off,
		    (unsigned long long)fwimage_size,
		    sc->chip->fw_signature,
		    (unsigned long long)sig_off,
		    (unsigned long long)sig_size);

		/* Stage signature: allocate sysmem, copy. */
		error = nvgsp_dma_alloc_dmamem(sc, roundup(sig_size,
		    NVGSP_PAGE_SIZE), NVGSP_PAGE_SIZE, &sc->gsp_sig);
		if (error != 0) {
			nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: sig dma alloc failed (%d)\n", error);
			goto out_put;
		}
		memcpy(sc->gsp_sig.kva, gsp_fw->data + sig_off, sig_size);
		sc->gsp_sig_size = sig_size;

		img_size = (uint32_t)fwimage_size;
		/* Stash the data offset for the memcpy below. */
		sc->gsp_fwimage_off = (uint32_t)fwimage_off;
	}
	img_pages = (img_size + NVGSP_PAGE_SIZE - 1) / NVGSP_PAGE_SIZE;
	l2_pages = (img_pages + 511) / 512;
	radix3_alloc_size = (2 + l2_pages) * NVGSP_PAGE_SIZE;

	bl_size = bl_fw->datasize;
	if (bl_size < sizeof(*bl_hdr)) {
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: BL fw too small (%u)\n",
		    bl_size);
		error = EIO;
		goto out_put;
	}
	bl_hdr = (const struct nvgsp_bin_hdr *)bl_fw->data;
	if (bl_hdr->header_offset + sizeof(*bl_desc) > bl_size ||
	    bl_hdr->data_offset + bl_hdr->data_size > bl_size) {
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: BL bin_hdr OOB (hdr@0x%x data@0x%x+%u of %u)\n",
		    bl_hdr->header_offset, bl_hdr->data_offset,
		    bl_hdr->data_size, bl_size);
		error = EIO;
		goto out_put;
	}
	bl_desc = (const struct nvgsp_riscv_ucode_desc *)
	    (bl_fw->data + bl_hdr->header_offset);
	bl_payload_off  = bl_hdr->data_offset;
	bl_payload_size = bl_hdr->data_size;

	nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: gsp_image=%u B (%u pages, %u L2), BL=%u B "
	    "(payload@0x%x size %u)\n",
	    img_size, img_pages, l2_pages,
	    bl_size, bl_payload_off, bl_payload_size);
	nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: BL desc ver=%u appVer=0x%x monitorCode@0x%x+%u "
	    "monitorData@0x%x+%u manifest@0x%x+%u\n",
	    bl_desc->version, bl_desc->appVersion,
	    bl_desc->monitorCodeOffset, bl_desc->monitorCodeSize,
	    bl_desc->monitorDataOffset, bl_desc->monitorDataSize,
	    bl_desc->manifestOffset, bl_desc->manifestSize);

	/* 1. Allocate GSP image buffer (contig, page-aligned). */
	error = nvgsp_dma_alloc_dmamem(sc,
	    (bus_size_t)img_pages * NVGSP_PAGE_SIZE,
	    NVGSP_PAGE_SIZE, &sc->gsp_image);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: GSP image dma alloc failed (%d) for %u B\n",
		    error, img_pages * NVGSP_PAGE_SIZE);
		goto out_put;
	}
	memcpy(sc->gsp_image.kva, gsp_fw->data + sc->gsp_fwimage_off, img_size);

	/* 2. Allocate radix3 page-table buffer. */
	error = nvgsp_dma_alloc_dmamem(sc, radix3_alloc_size,
	    NVGSP_PAGE_SIZE, &sc->gsp_radix3);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: radix3 dma alloc failed (%d) for %u B\n",
		    error, radix3_alloc_size);
		nvgsp_dma_free_dmamem(sc, &sc->gsp_image);
		goto out_put;
	}

	l0_pa = nvgsp_boot_image_build_radix3(sc, sc->gsp_image.paddr,
	    img_pages * NVGSP_PAGE_SIZE);

	/* 3. Allocate BL buffer and copy the data section. */
	error = nvgsp_dma_alloc_dmamem(sc,
	    roundup(bl_payload_size, NVGSP_PAGE_SIZE),
	    NVGSP_PAGE_SIZE, &sc->gsp_bl);
	if (error != 0) {
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: BL dma alloc failed (%d)\n", error);
		nvgsp_dma_free_dmamem(sc, &sc->gsp_radix3);
		nvgsp_dma_free_dmamem(sc, &sc->gsp_image);
		goto out_put;
	}
	memcpy(sc->gsp_bl.kva, bl_fw->data + bl_payload_off,
	    bl_payload_size);

	/*
	 * 4. Fill the wpr_meta. We position FRTS at the top of VRAM
	 * (same as our FwSec FRTS request) and stack the rest below
	 * it. WPR2 sub-regions go top-down:
	 *
	 *   frts.addr + frts.size  (= gspFwWprEnd)
	 *   frts.addr              <- frtsOffset
	 *   bootBin                <- bootBinOffset (= frtsOffset - bl_size, 4K aligned down)
	 *   GSP ELF                <- gspFwOffset (= bootBinOffset - img_size, 64K aligned down)
	 *   WPR heap               <- gspFwHeapOffset (some heap size below)
	 *   wpr_meta               <- gspFwWprStart   (128K aligned down, sizeof(meta) below)
	 *   non-WPR heap           <- nonWprHeapOffset (1 MiB)
	 *
	 * We use the same fb_size detection as FwSec.
	 */
	meta = (struct nvgsp_wpr_meta *)sc->wpr_meta.kva;
	{
		uint32_t lmr_v = nvgsp_rd32(sc, 0x100ce0);
		uint32_t lmag_v = (lmr_v & 0x3f0u) >> 4;
		uint32_t lsca_v = lmr_v & 0xfu;
		uint64_t fb_sz  = (uint64_t)lmag_v << (lsca_v + 20);
		uint64_t bios_addr;
		uint64_t frts_off, frts_sz = 0x100000;
		uint64_t boot_off, gsp_off, heap_off, heap_size;
		uint64_t wpr_start, non_wpr_off, non_wpr_sz = 0x100000;
		uint32_t vga = nvgsp_rd32(sc, NV_PDISP_VGA_CR);
		uint64_t fb_gb;

		if (lmr_v & 0x40000000u)
			fb_sz = fb_sz / 16 * 15;
		bios_addr = fb_sz - 0x100000;
		if (vga & NV_PDISP_VGA_CR_ENABLED) {
			uint64_t staged = ((uint64_t)(vga & 0xffffff00u)) << 8;
			if (staged < bios_addr)
				bios_addr = fb_sz - 0x20000;
			else
				bios_addr = staged;
		}

		/*
		 * WPR heap size for r570 + TU102 (uses LibOS2, NOT LibOS3):
		 *   r570_wpr_libos2 (nouveau rm/r570/rm.c:9):
		 *     os_carveout_size = LIBOS2 (0 MiB -- no FB carveout for v2)
		 *     base_size        = TU10X  (8 MiB)
		 *     heap_size_min    = LIBOS2 (64 MiB)
		 *   heap = 0 + 8 + ALIGN(96 KiB * fb_gb, 1 MiB) + ALIGN(48 KiB * 2048, 1 MiB)
		 *        clamped to 64 MiB
		 *   For 11 GiB FB: 0 + 8 + 2 + 96 = 106 MiB.
		 */
		fb_gb = (fb_sz + ((1ULL << 30) - 1)) >> 30;
		heap_size = (0ULL << 20)               /* LibOS2: no carveout */
		    + (8ULL << 20)                     /* RM base, TU10X */
		    + roundup((96ULL << 10) * fb_gb, 1ULL << 20)
		    + roundup(((48ULL << 10) * 2048ULL), 1ULL << 20);
		if (heap_size < (64ULL << 20))
			heap_size = (64ULL << 20);

		/*
		 * Align tu102.c:323-336 exactly:
		 *   frts.addr   = ALIGN_DOWN(bios_addr, 0x20000) - frts.size
		 *   boot.addr   = ALIGN_DOWN(frts.addr - boot.size, 0x1000)
		 *   elf.addr    = ALIGN_DOWN(boot.addr - elf.size, 0x10000)
		 *   heap.addr   = ALIGN_DOWN(elf.addr - heap.size, 0x100000)
		 *   heap.size   = ALIGN_DOWN(elf.addr - heap.addr, 0x100000)
		 *   wpr2.addr   = ALIGN_DOWN(heap.addr - sizeof(meta), 0x100000)
		 *   non_wpr.addr= wpr2.addr - non_wpr.size
		 */
		frts_off  = (bios_addr & ~(uint64_t)0x1ffffu) - frts_sz;
		boot_off  = (frts_off - bl_payload_size) & ~(uint64_t)0xfffu;
		gsp_off   = (boot_off - img_size) & ~(uint64_t)0xffffu;
		heap_off  = (gsp_off - heap_size) & ~(uint64_t)0xfffffu;
		/* Recompute heap_size from the actual aligned span */
		heap_size = (gsp_off - heap_off) & ~(uint64_t)0xfffffu;
		heap_off  = gsp_off - heap_size;
		wpr_start = (heap_off - sizeof(*meta)) & ~(uint64_t)0xfffffu;
		non_wpr_off = wpr_start - non_wpr_sz;

		meta->sysmemAddrOfRadix3Elf = l0_pa;
		meta->sizeOfRadix3Elf = img_size;
		meta->sysmemAddrOfBootloader = sc->gsp_bl.paddr;
		meta->sizeOfBootloader = bl_payload_size;
		meta->bootloaderCodeOffset = bl_desc->monitorCodeOffset;
		meta->bootloaderDataOffset = bl_desc->monitorDataOffset;
		meta->bootloaderManifestOffset = bl_desc->manifestOffset;
		meta->sysmemAddrOfSignature = sc->gsp_sig.paddr;
		meta->sizeOfSignature = sc->gsp_sig_size;

		meta->gspFwRsvdStart = non_wpr_off;
		meta->nonWprHeapOffset = non_wpr_off;
		meta->nonWprHeapSize = non_wpr_sz;
		meta->gspFwWprStart = wpr_start;
		meta->gspFwHeapOffset = heap_off;
		meta->gspFwHeapSize = heap_size;
		meta->gspFwOffset = gsp_off;
		meta->bootBinOffset = boot_off;
		meta->frtsOffset = frts_off;
		meta->frtsSize = frts_sz;
		meta->gspFwWprEnd = frts_off + frts_sz;
		meta->fbSize = fb_sz;
		meta->vgaWorkspaceOffset = bios_addr;
		meta->vgaWorkspaceSize = fb_sz - bios_addr;
		meta->bootCount = 0;
		meta->verified = 0;

		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: FB layout fb=0x%llx bios=0x%llx frts=0x%llx+0x%llx\n",
		    (unsigned long long)fb_sz,
		    (unsigned long long)bios_addr,
		    (unsigned long long)frts_off,
		    (unsigned long long)frts_sz);
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: WPR2 [0x%llx..0x%llx) bootbin=0x%llx gspfw=0x%llx heap=0x%llx\n",
		    (unsigned long long)wpr_start,
		    (unsigned long long)meta->gspFwWprEnd,
		    (unsigned long long)boot_off,
		    (unsigned long long)gsp_off,
		    (unsigned long long)heap_off);
		nvgpu_log(NVGPU_LOG_DEBUG, "gsp_boot: meta.radix3=0x%llx (size %u) bl=0x%llx (size %u)\n",
		    (unsigned long long)meta->sysmemAddrOfRadix3Elf,
		    (uint32_t)meta->sizeOfRadix3Elf,
		    (unsigned long long)meta->sysmemAddrOfBootloader,
		    (uint32_t)meta->sizeOfBootloader);
	}

	firmware_put(bl_fw, FIRMWARE_UNLOAD);
	firmware_put(gsp_fw, FIRMWARE_UNLOAD);
	return (0);

out_put:
	firmware_put(bl_fw, FIRMWARE_UNLOAD);
	firmware_put(gsp_fw, FIRMWARE_UNLOAD);
	return (error);
}

void
nvgsp_boot_release_image(struct nvgsp_state *sc)
{
	if (sc->gsp_sig.kva != NULL)
		nvgsp_dma_free_dmamem(sc, &sc->gsp_sig);
	if (sc->gsp_bl.kva != NULL)
		nvgsp_dma_free_dmamem(sc, &sc->gsp_bl);
	if (sc->gsp_radix3.kva != NULL)
		nvgsp_dma_free_dmamem(sc, &sc->gsp_radix3);
	if (sc->gsp_image.kva != NULL)
		nvgsp_dma_free_dmamem(sc, &sc->gsp_image);
}
