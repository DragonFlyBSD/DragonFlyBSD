/*-
 * Copyright 1996-1998 John D. Polstra.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/i386/elf_machdep.c,v 1.8 1999/12/21 11:14:02 eivind Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/linker.h>
#include <sys/sysent.h>
#include <sys/imgact_elf.h>
#include <sys/syscall.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <machine/elf.h>
#include <machine/md_var.h>

static struct sysentvec elf64_dragonfly_sysvec = {
        .sv_size	= SYS_MAXSYSCALL,
        .sv_table	= sysent,
        .sv_mask	= -1,
        .sv_sigsize	= 0,
        .sv_sigtbl	= NULL,
        .sv_errsize	= 0,
        .sv_errtbl	= NULL,
        .sv_transtrap	= NULL,
        .sv_fixup	= __elfN(dragonfly_fixup),
        .sv_sendsig	= sendsig,
        .sv_sigcode	= sigcode,
        .sv_szsigcode	= &szsigcode,
        .sv_prepsyscall	= NULL,
	.sv_name	= "DragonFly ELF64",
	.sv_coredump	= __elfN(coredump),
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
};

static Elf64_Brandinfo dragonfly_brand_info = {
        .brand		= ELFOSABI_NONE,
        .machine	= EM_X86_64,
        .compat_3_brand	= "DragonFly",
        .emul_path	= NULL,
        .interp_path	= "/libexec/ld-elf.so.2",
        .sysvec		= &elf64_dragonfly_sysvec,
        .interp_newpath	= NULL,
        .flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE,
        .brand_note	= &elf64_dragonfly_brandnote,
};

SYSINIT(elf64, SI_SUB_EXEC, SI_ORDER_FIRST,
        (sysinit_cfunc_t) elf64_insert_brand_entry,
        &dragonfly_brand_info);

static Elf64_Brandinfo freebsd_brand_info = {
        .brand		= ELFOSABI_FREEBSD,
        .machine	= EM_X86_64,
        .compat_3_brand	= "FreeBSD",
        .emul_path	= NULL,
        .interp_path	= "/usr/libexec/ld-elf.so.1",
        .sysvec		= &elf64_dragonfly_sysvec,
        .interp_newpath	= NULL,
        .flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE,
        .brand_note	= &elf64_freebsd_brandnote,
};

SYSINIT(elf64_fbsd, SI_SUB_EXEC, SI_ORDER_ANY,
        (sysinit_cfunc_t) elf64_insert_brand_entry,
        &freebsd_brand_info);

/* Process one elf relocation with addend. */
static int
elf_reloc_internal(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, int local, elf_lookup_fn lookup)
{
	Elf64_Addr *where, val;
	Elf32_Addr *where32, val32;
	Elf_Addr addr;
	Elf_Addr addend;
	Elf_Size rtype, symidx;
	const Elf_Rel *rel;
	const Elf_Rela *rela;

	switch (type) {
	case ELF_RELOC_REL:
		rel = (const Elf_Rel *)data;
		where = (Elf_Addr *) (relocbase + rel->r_offset);
		rtype = ELF_R_TYPE(rel->r_info);
		symidx = ELF_R_SYM(rel->r_info);
		/* Addend is 32 bit on 32 bit relocs */
		switch (rtype) {
		case R_X86_64_PC32:
		case R_X86_64_32:
		case R_X86_64_32S:
			addend = *(Elf32_Addr *)where;
			break;
		default:
			addend = *where;
			break;
		}
		break;
	case ELF_RELOC_RELA:
		rela = (const Elf_Rela *)data;
		where = (Elf_Addr *) (relocbase + rela->r_offset);
		addend = rela->r_addend;
		rtype = ELF_R_TYPE(rela->r_info);
		symidx = ELF_R_SYM(rela->r_info);
		break;
	default:
		panic("unknown reloc type %d", type);
	}

	switch (rtype) {

		case R_X86_64_NONE:	/* none */
			break;

		case R_X86_64_64:		/* S + A */
			if (lookup(lf, symidx, 1, &addr))
				return -1;
			val = addr + addend;
			if (*where != val)
				*where = val;
			break;

		case R_X86_64_PC32:	/* S + A - P */
			if (lookup(lf, symidx, 1, &addr))
				return -1;
			where32 = (Elf32_Addr *)where;
			val32 = (Elf32_Addr)(addr + addend - (Elf_Addr)where);
			if (*where32 != val32)
				*where32 = val32;
			break;

		case R_X86_64_32:	/* S + A zero extend */
		case R_X86_64_32S:	/* S + A sign extend */
			if (lookup(lf, symidx, 1, &addr))
				return -1;
			val32 = (Elf32_Addr)(addr + addend);
			where32 = (Elf32_Addr *)where;
			if (*where32 != val32)
				*where32 = val32;
			break;

		case R_X86_64_COPY:	/* none */
			/*
			 * There shouldn't be copy relocations in kernel
			 * objects.
			 */
			kprintf("kldload: unexpected R_COPY relocation\n");
			return -1;
			break;

		case R_X86_64_GLOB_DAT:	/* S */
		case R_X86_64_JMP_SLOT:	/* XXX need addend + offset */
			if (lookup(lf, symidx, 1, &addr))
				return -1;
			if (*where != addr)
				*where = addr;
			break;

		case R_X86_64_RELATIVE:	/* B + A */
			addr = relocbase + addend;
			val = addr;
			if (*where != val)
				*where = val;
			break;

		default:
			kprintf("kldload: unexpected relocation type %ld\n",
			       rtype);
			return -1;
	}
	return(0);
}

int
elf_reloc(linker_file_t lf, Elf_Addr relocbase, const void *data, int type,
    elf_lookup_fn lookup)
{

	return (elf_reloc_internal(lf, relocbase, data, type, 0, lookup));
}

int
elf_reloc_local(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, elf_lookup_fn lookup)
{

	return (elf_reloc_internal(lf, relocbase, data, type, 1, lookup));
}
