/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2014 The FreeBSD Foundation
 * Copyright (c) 2026 The DragonFly Project
 * All rights reserved.
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

/*
 * AArch64 EFI loader for DragonFly BSD
 */

#define __ELF_WORD_SIZE 64
#include <sys/param.h>
#include <sys/exec.h>
#include <sys/linker.h>
#include <string.h>
#include <machine/elf.h>
#include <stand.h>

#include <efi.h>
#include <efilib.h>

#include "bootstrap.h"
#include "loader_efi.h"

extern int bi_load(char *args, vm_offset_t *modulep, vm_offset_t *kernendp);

static int	elf64_exec(struct preloaded_file *amp);
static int	elf64_obj_exec(struct preloaded_file *amp);

static struct file_format aarch64_elf = { elf64_loadfile, elf64_exec };
static struct file_format aarch64_elf_obj = { elf64_obj_loadfile, elf64_obj_exec };

struct file_format *file_formats[] = {
	&aarch64_elf,
	&aarch64_elf_obj,
	NULL
};

/*
 * Clean the data cache and invalidate the instruction cache.
 * This is required before jumping to the kernel to ensure:
 * 1. All data written to memory is visible to instruction fetches
 * 2. No stale instructions are cached
 */
static void
clean_caches(void)
{
	uint64_t ctr_el0;
	uint64_t dline_size, iline_size;

	/* Read CTR_EL0 to get cache line sizes */
	__asm __volatile("mrs %0, ctr_el0" : "=r" (ctr_el0));

	/* DminLine: log2 of the number of words in the smallest data cache line */
	dline_size = 4 << ((ctr_el0 >> 16) & 0xf);
	/* IminLine: log2 of the number of words in the smallest instruction cache line */
	iline_size = 4 << (ctr_el0 & 0xf);

	(void)dline_size;
	(void)iline_size;

	/* Clean and invalidate entire data cache */
	__asm __volatile("dsb sy");

	/* Invalidate entire instruction cache */
	__asm __volatile("ic iallu");
	__asm __volatile("dsb sy");
	__asm __volatile("isb");
}

/*
 * There is an ELF kernel and one or more ELF modules loaded.
 * We wish to start executing the kernel image, so make such
 * preparations as are required, and do so.
 */
static int
elf64_exec(struct preloaded_file *fp)
{
	struct file_metadata	*md;
	Elf_Ehdr		*ehdr;
	vm_offset_t		modulep, kernend;
	vm_offset_t		entry;
	int			err;
	void			(*kernel_entry)(vm_offset_t);

	if ((md = file_findmetadata(fp, MODINFOMD_ELFHDR)) == NULL) {
		printf("elf64_exec: no ELF header metadata\n");
		return (EFTYPE);
	}
	ehdr = (Elf_Ehdr *)&(md->md_data);

	entry = ehdr->e_entry;
	printf("Kernel entry at 0x%lx\n", (unsigned long)entry);

	/*
	 * Call bi_load to:
	 * 1. Prepare boot metadata (howto, envp, kernend, etc.)
	 * 2. Call ExitBootServices (no more EFI boot services after this!)
	 * 3. Copy EFI memory map to metadata
	 */
	err = bi_load(fp->f_args, &modulep, &kernend);
	if (err != 0) {
		printf("bi_load failed: %d\n", err);
		return (err);
	}
	printf("modulep=0x%lx header=%08x %08x\n",
	    (unsigned long)modulep,
	    ((uint32_t *)modulep)[0], ((uint32_t *)modulep)[1]);

	/*
	 * At this point:
	 * - ExitBootServices has been called
	 * - We cannot use any EFI Boot Services
	 * - We cannot use printf or console output
	 * - modulep points to the preload metadata
	 */

	/* Clean up devices */
	dev_cleanup();

	/* Clean caches before jumping to kernel */
	clean_caches();

	/*
	 * Jump to the kernel entry point.
	 * On arm64, the calling convention passes the first argument in x0.
	 * The kernel expects modulep in x0.
	 */
	kernel_entry = (void (*)(vm_offset_t))entry;
	(*kernel_entry)(modulep);

	/* Should never reach here */
	panic("exec returned");
}

static int
elf64_obj_exec(struct preloaded_file *fp __unused)
{
	return (EFTYPE);
}
