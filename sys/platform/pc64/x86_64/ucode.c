/*
 * Copyright (c) 2026 The DragonFly Project.  All rights reserved.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Apply a CPU microcode update before the kernel looks at what the CPU can
 * do.  cpuctl(4) can do the same thing from userland, but only long after
 * identify_cpu() has cached the feature words and the APs have started, so
 * anything a microcode update adds or removes is missed.  Here the image is
 * preloaded by the loader:
 *
 *	cpu_microcode_load="YES"
 *	cpu_microcode_name="amd-ucode.bin"
 *
 * and applied to the BSP before identify_cpu(), then to each AP as it comes
 * up.  Only the AMD container format is understood; Intel needs its own
 * parser and nobody has one to test against yet.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/linker.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

#include <machine/ucode.h>

/*
 * AMD container format.  A file is one or more containers back to back; each
 * is a magic word, an equivalence table mapping CPUID signatures to a patch
 * id, and then the patches themselves.
 */
#define	AMD_CONTAINER_MAGIC	0x00414d44	/* "DMA\0" */
#define	AMD_SECTION_EQUIV	0x00000000
#define	AMD_SECTION_PATCH	0x00000001

struct amd_section_header {
	uint32_t	type;
	uint32_t	size;
} __packed;

struct amd_equiv_entry {
	uint32_t	installed_cpu;
	uint32_t	fixed_errata_mask;
	uint32_t	fixed_errata_compare;
	uint16_t	equiv_id;
	uint16_t	reserved;
} __packed;

struct amd_patch_header {
	uint32_t	date;
	uint32_t	patch_id;
	uint16_t	mc_patch_data_id;
	uint8_t		mc_patch_data_len;
	uint8_t		init_flag;
	uint32_t	mc_patch_data_checksum;
	uint32_t	nb_dev_id;
	uint32_t	sb_dev_id;
	uint16_t	processor_rev_id;
	uint8_t		nb_rev_id;
	uint8_t		sb_rev_id;
	uint8_t		bios_api_rev;
	uint8_t		reserved1[3];
	uint32_t	match_reg[8];
} __packed;

/*
 * The patch has to be 16-byte aligned when its address is handed to the CPU,
 * and there is no allocator this early, so it is staged here.  AMD patches
 * run to a few kilobytes; a page is generous and costs nothing that is not
 * already reserved.
 */
#define	UCODE_STAGE_SIZE	PAGE_SIZE
static uint8_t ucode_stage[UCODE_STAGE_SIZE] __aligned(16);
static size_t ucode_stage_len;
static uint32_t ucode_stage_rev;

/*
 * identify_cpu() has not run when the BSP stages microcode, so this must
 * not rely on cpu_vendor_id.
 */
static bool
ucode_is_amd_cpu(void)
{
	uint32_t regs[4];

	do_cpuid(0, regs);
	return (regs[1] == 0x68747541 && regs[3] == 0x69746e65 &&
		regs[2] == 0x444d4163);
}
static uint64_t
ucode_amd_rev(void)
{
	return (rdmsr(MSR_AMD_PATCH_LEVEL));
}

/*
 * Walk one container looking for a patch whose processor_rev_id matches the
 * equivalence entry for this CPU.  Returns the end of the container so the
 * caller can move on to the next one, or NULL if the data stops making sense.
 */
static const uint8_t *
ucode_amd_scan(const uint8_t *p, const uint8_t *end, uint32_t sig,
	       const struct amd_patch_header **bestp, size_t *bestlen)
{
	const struct amd_section_header *sh;
	const struct amd_equiv_entry *eq;
	uint32_t magic;
	uint16_t want = 0;
	int found = 0;

	if ((size_t)(end - p) < sizeof(magic) + sizeof(*sh))
		return (NULL);
	memcpy(&magic, p, sizeof(magic));
	if (magic != AMD_CONTAINER_MAGIC)
		return (NULL);
	p += sizeof(magic);

	sh = (const struct amd_section_header *)p;
	if (sh->type != AMD_SECTION_EQUIV)
		return (NULL);
	p += sizeof(*sh);
	if (sh->size > (size_t)(end - p))
		return (NULL);

	for (eq = (const struct amd_equiv_entry *)p;
	     (const uint8_t *)(eq + 1) <= p + sh->size; eq++) {
		if (eq->installed_cpu == 0)
			break;
		if (eq->installed_cpu == sig) {
			want = eq->equiv_id;
			found = 1;
			break;
		}
	}
	p += sh->size;

	while ((size_t)(end - p) >= sizeof(*sh)) {
		const struct amd_patch_header *ph;

		sh = (const struct amd_section_header *)p;
		if (sh->type != AMD_SECTION_PATCH)
			break;
		p += sizeof(*sh);
		if (sh->size > (size_t)(end - p) || sh->size < sizeof(*ph))
			return (NULL);

		ph = (const struct amd_patch_header *)p;
		if (found && ph->processor_rev_id == want &&
		    (*bestp == NULL || ph->patch_id > (*bestp)->patch_id)) {
			*bestp = ph;
			*bestlen = sh->size;
		}
		p += sh->size;
	}

	return (p);
}

/*
 * Find the image the loader preloaded, pick the patch for this CPU and stage
 * it.  Called on the BSP only, before identify_cpu().
 */
static void
ucode_amd_stage(void)
{
	const struct amd_patch_header *best = NULL;
	const uint8_t *p, *end;
	size_t bestlen = 0;
	caddr_t mod, info;
	uint32_t sig, regs[4];
	size_t len;

	mod = preload_search_by_type("cpu_microcode");
	if (mod == NULL)
		return;
	info = preload_search_info(mod, MODINFO_ADDR);
	if (info == NULL)
		return;
	p = *(const uint8_t **)info;
	info = preload_search_info(mod, MODINFO_SIZE);
	if (info == NULL)
		return;
	len = *(const size_t *)info;
	if (p == NULL || len == 0)
		return;
	end = p + len;

	do_cpuid(1, regs);
	sig = regs[0];

	while (p != NULL && p < end)
		p = ucode_amd_scan(p, end, sig, &best, &bestlen);

	if (best == NULL) {
		kprintf("ucode: no AMD microcode for CPUID %#x\n", sig);
		return;
	}
	if (bestlen > sizeof(ucode_stage)) {
		kprintf("ucode: patch %#x is %zu bytes, larger than the "
			"staging buffer\n", best->patch_id, bestlen);
		return;
	}

	memcpy(ucode_stage, best, bestlen);
	ucode_stage_len = bestlen;
	ucode_stage_rev = best->patch_id;
}

/*
 * Hand the staged patch to this CPU.  Safe to call when nothing is staged,
 * and safe to call twice: the CPU ignores a patch it already has or that is
 * older than the one it is running.
 */
void
ucode_apply(void)
{
	uint64_t before, after;
	uint32_t regs[4];

	if (ucode_stage_len == 0 || !ucode_is_amd_cpu())
		return;

	before = ucode_amd_rev();
	if (before >= ucode_stage_rev)
		return;

	wrmsr(MSR_AMD_PATCH_LOADER, (uintptr_t)ucode_stage);
	do_cpuid(0, regs);		/* serialize */
	after = ucode_amd_rev();

	if (after != before) {
		kprintf("ucode: cpu%d microcode %#jx -> %#jx\n", mycpuid,
			(uintmax_t)before, (uintmax_t)after);
	} else {
		kprintf("ucode: cpu%d microcode %#jx unchanged, wanted %#x\n",
			mycpuid, (uintmax_t)before, ucode_stage_rev);
	}
}

/*
 * Called from the BSP before identify_cpu(), which is the last moment at
 * which an update can still change what the kernel believes the CPU can do.
 */
void
ucode_load_bsp(void)
{
	if (!ucode_is_amd_cpu())
		return;
	ucode_amd_stage();
	ucode_apply();
}
