/*-
 * Copyright (c) 1998 Doug Rabson
 * Copyright (c) 2004 Peter Wemm
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
 *
 * $FreeBSD: src/sys/kern/link_elf.c,v 1.24 1999/12/24 15:33:36 bde Exp $
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/linker.h>
#include <machine/elf.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_zone.h>
#include <vm/vm_object.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

static int	link_elf_obj_preload_file(const char *, linker_file_t *);
static int	link_elf_obj_preload_finish(linker_file_t);
static int	link_elf_obj_load_file(const char *, linker_file_t *);
static int
link_elf_obj_lookup_symbol(linker_file_t, const char *,
		       c_linker_sym_t *);
static int	link_elf_obj_symbol_values(linker_file_t, c_linker_sym_t, linker_symval_t *);
static int
link_elf_obj_search_symbol(linker_file_t, caddr_t value,
		       c_linker_sym_t * sym, long *diffp);

static void	link_elf_obj_unload_file(linker_file_t);
static int
link_elf_obj_lookup_set(linker_file_t, const char *,
		    void ***, void ***, int *);
static void	link_elf_obj_reloc_local(linker_file_t lf);
static int elf_obj_lookup(linker_file_t lf, Elf_Size symidx, int deps, Elf_Addr *);

static struct linker_class_ops link_elf_obj_class_ops = {
	link_elf_obj_load_file,
	link_elf_obj_preload_file,
};

static struct linker_file_ops link_elf_obj_file_ops = {
	.lookup_symbol = link_elf_obj_lookup_symbol,
	.symbol_values = link_elf_obj_symbol_values,
	.search_symbol = link_elf_obj_search_symbol,
	.preload_finish = link_elf_obj_preload_finish,
	.unload = link_elf_obj_unload_file,
	.lookup_set = link_elf_obj_lookup_set,
};

typedef struct {
	void           *addr;
	Elf_Off		size;
	int		flags;
	int		sec;		/* Original section */
	char           *name;
}		Elf_progent;

typedef struct {
	Elf_Rel        *rel;
	int		nrel;
	int		sec;
}		Elf_relent;

typedef struct {
	Elf_Rela       *rela;
	int		nrela;
	int		sec;
}		Elf_relaent;


typedef struct elf_file {
	int		preloaded;

	caddr_t		address;	/* Relocation address */
	size_t		bytes;		/* Chunk size in bytes */
	vm_object_t	object;		/* VM object to hold file pages */
	Elf_Shdr       *e_shdr;

	Elf_progent    *progtab;
	int		nprogtab;

	Elf_relaent    *relatab;
	int		nrelatab;

	Elf_relent     *reltab;
	int		nreltab;

	Elf_Sym        *ddbsymtab;	/* The symbol table we are using */
	long		ddbsymcnt;	/* Number of symbols */
	caddr_t		ddbstrtab;	/* String table */
	long		ddbstrcnt;	/* number of bytes in string table */

	caddr_t		shstrtab;	/* Section name string table */
	long		shstrcnt;	/* number of bytes in string table */

	caddr_t		ctftab;		/* CTF table */
	long		ctfcnt;		/* number of bytes in CTF table */
	caddr_t		ctfoff;		/* CTF offset table */
	caddr_t		typoff;		/* Type offset table */
	long		typlen;		/* Number of type entries. */

}              *elf_file_t;

static int	relocate_file(linker_file_t lf);

/*
 * The kernel symbol table starts here.
 */
extern struct _dynamic _DYNAMIC;

static void
link_elf_obj_init(void *arg)
{
#if ELF_TARG_CLASS == ELFCLASS32
	linker_add_class("elf32", NULL, &link_elf_obj_class_ops);
#else
	linker_add_class("elf64", NULL, &link_elf_obj_class_ops);
#endif
}

SYSINIT(link_elf, SI_BOOT2_KLD, SI_ORDER_SECOND, link_elf_obj_init, 0);

static void
link_elf_obj_error(const char *file, const char *s)
{
	kprintf("kldload: %s: %s\n", file, s);
}

static int
link_elf_obj_preload_file(const char *filename, linker_file_t *result)
{
	Elf_Ehdr       *hdr;
	Elf_Shdr       *shdr;
	Elf_Sym	*es;
	caddr_t		modptr, baseptr, sizeptr;
	char           *type;
	elf_file_t	ef;
	linker_file_t	lf;
	Elf_Addr	off;
	int		error, i, j, pb, ra, rl, shstrindex, symstrindex, symtabindex;

	/*
	 * Look to see if we have the module preloaded.
	 */
	modptr = preload_search_by_name(filename);
	if (modptr == NULL)
		return ENOENT;

	/* It's preloaded, check we can handle it and collect information */
	type = (char *)preload_search_info(modptr, MODINFO_TYPE);
	baseptr = preload_search_info(modptr, MODINFO_ADDR);
	sizeptr = preload_search_info(modptr, MODINFO_SIZE);
	hdr = (Elf_Ehdr *) preload_search_info(modptr, MODINFO_METADATA |
					       MODINFOMD_ELFHDR);
	shdr = (Elf_Shdr *) preload_search_info(modptr, MODINFO_METADATA |
						MODINFOMD_SHDR);
	if (type == NULL ||
	    (strcmp(type, "elf" __XSTRING(__ELF_WORD_SIZE) " obj module") != 0 &&
	     strcmp(type, "elf obj module") != 0)) {
		return (EFTYPE);
	}
	if (baseptr == NULL || sizeptr == NULL || hdr == NULL || shdr == NULL)
		return (EINVAL);

	ef = kmalloc(sizeof(struct elf_file), M_LINKER, M_WAITOK | M_ZERO);
	ef->preloaded = 1;
	ef->address = *(caddr_t *) baseptr;
	ef->bytes = 0;
	lf = linker_make_file(filename, ef, &link_elf_obj_file_ops);
	if (lf == NULL) {
		kfree(ef, M_LINKER);
		return ENOMEM;
	}
	lf->address = ef->address;
	lf->size = *(size_t *) sizeptr;

	if (hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
	    hdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	    hdr->e_ident[EI_VERSION] != EV_CURRENT ||
	    hdr->e_version != EV_CURRENT ||
	    hdr->e_type != ET_REL ||
	    hdr->e_machine != ELF_TARG_MACH) {
		error = EFTYPE;
		goto out;
	}
	ef->e_shdr = shdr;

	/* Scan the section header for information and table sizing. */
	symtabindex = -1;
	symstrindex = -1;
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
			ef->nprogtab++;
			break;
		case SHT_SYMTAB:
			symtabindex = i;
			symstrindex = shdr[i].sh_link;
			break;
		case SHT_REL:
			ef->nreltab++;
			break;
		case SHT_RELA:
			ef->nrelatab++;
			break;
		}
	}

	shstrindex = hdr->e_shstrndx;
	if (ef->nprogtab == 0 || symstrindex < 0 ||
	    symstrindex >= hdr->e_shnum ||
	    shdr[symstrindex].sh_type != SHT_STRTAB || shstrindex == 0 ||
	    shstrindex >= hdr->e_shnum ||
	    shdr[shstrindex].sh_type != SHT_STRTAB) {
		error = ENOEXEC;
		goto out;
	}
	/* Allocate space for tracking the load chunks */
	if (ef->nprogtab != 0)
		ef->progtab = kmalloc(ef->nprogtab * sizeof(*ef->progtab),
				     M_LINKER, M_WAITOK | M_ZERO);
	if (ef->nreltab != 0)
		ef->reltab = kmalloc(ef->nreltab * sizeof(*ef->reltab),
				    M_LINKER, M_WAITOK | M_ZERO);
	if (ef->nrelatab != 0)
		ef->relatab = kmalloc(ef->nrelatab * sizeof(*ef->relatab),
				     M_LINKER, M_WAITOK | M_ZERO);
	if ((ef->nprogtab != 0 && ef->progtab == NULL) ||
	    (ef->nreltab != 0 && ef->reltab == NULL) ||
	    (ef->nrelatab != 0 && ef->relatab == NULL)) {
		error = ENOMEM;
		goto out;
	}
	/* XXX, relocate the sh_addr fields saved by the loader. */
	off = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_addr != 0 && (off == 0 || shdr[i].sh_addr < off))
			off = shdr[i].sh_addr;
	}
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_addr != 0)
			shdr[i].sh_addr = shdr[i].sh_addr - off +
				(Elf_Addr) ef->address;
	}

	ef->ddbsymcnt = shdr[symtabindex].sh_size / sizeof(Elf_Sym);
	ef->ddbsymtab = (Elf_Sym *) shdr[symtabindex].sh_addr;
	ef->ddbstrcnt = shdr[symstrindex].sh_size;
	ef->ddbstrtab = (char *)shdr[symstrindex].sh_addr;
	ef->shstrcnt = shdr[shstrindex].sh_size;
	ef->shstrtab = (char *)shdr[shstrindex].sh_addr;

	/* Now fill out progtab and the relocation tables. */
	pb = 0;
	rl = 0;
	ra = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
			ef->progtab[pb].addr = (void *)shdr[i].sh_addr;
			if (shdr[i].sh_type == SHT_PROGBITS)
				ef->progtab[pb].name = "<<PROGBITS>>";
			else
				ef->progtab[pb].name = "<<NOBITS>>";
			ef->progtab[pb].size = shdr[i].sh_size;
			ef->progtab[pb].sec = i;
			if (ef->shstrtab && shdr[i].sh_name != 0)
				ef->progtab[pb].name =
					ef->shstrtab + shdr[i].sh_name;
#if 0
			if (ef->progtab[pb].name != NULL &&
			    !strcmp(ef->progtab[pb].name, "set_pcpu")) {
				void           *dpcpu;

				dpcpu = dpcpu_alloc(shdr[i].sh_size);
				if (dpcpu == NULL) {
					error = ENOSPC;
					goto out;
				}
				memcpy(dpcpu, ef->progtab[pb].addr,
				       ef->progtab[pb].size);
				dpcpu_copy(dpcpu, shdr[i].sh_size);
				ef->progtab[pb].addr = dpcpu;
#ifdef VIMAGE
			} else if (ef->progtab[pb].name != NULL &&
				   !strcmp(ef->progtab[pb].name, VNET_SETNAME)) {
				void           *vnet_data;

				vnet_data = vnet_data_alloc(shdr[i].sh_size);
				if (vnet_data == NULL) {
					error = ENOSPC;
					goto out;
				}
				memcpy(vnet_data, ef->progtab[pb].addr,
				       ef->progtab[pb].size);
				vnet_data_copy(vnet_data, shdr[i].sh_size);
				ef->progtab[pb].addr = vnet_data;
#endif
			}
#endif
			/* Update all symbol values with the offset. */
			for (j = 0; j < ef->ddbsymcnt; j++) {
				es = &ef->ddbsymtab[j];
				if (es->st_shndx != i)
					continue;
				es->st_value += (Elf_Addr) ef->progtab[pb].addr;
			}
			pb++;
			break;
		case SHT_REL:
			ef->reltab[rl].rel = (Elf_Rel *) shdr[i].sh_addr;
			ef->reltab[rl].nrel = shdr[i].sh_size / sizeof(Elf_Rel);
			ef->reltab[rl].sec = shdr[i].sh_info;
			rl++;
			break;
		case SHT_RELA:
			ef->relatab[ra].rela = (Elf_Rela *) shdr[i].sh_addr;
			ef->relatab[ra].nrela =
				shdr[i].sh_size / sizeof(Elf_Rela);
			ef->relatab[ra].sec = shdr[i].sh_info;
			ra++;
			break;
		}
	}
	if (pb != ef->nprogtab)
		panic("lost progbits");
	if (rl != ef->nreltab)
		panic("lost reltab");
	if (ra != ef->nrelatab)
		panic("lost relatab");

	/* Local intra-module relocations */
	link_elf_obj_reloc_local(lf);

	*result = lf;
	return (0);

out:
	/* preload not done this way */
	linker_file_unload(lf /* , LINKER_UNLOAD_FORCE */ );
	return (error);
}

static int
link_elf_obj_preload_finish(linker_file_t lf)
{
	int error;

	error = relocate_file(lf);

	return (error);
}

static int
link_elf_obj_load_file(const char *filename, linker_file_t * result)
{
	struct nlookupdata nd;
	struct thread  *td = curthread;	/* XXX */
	struct proc    *p = td->td_proc;
	char           *pathname;
	struct vnode   *vp;
	Elf_Ehdr       *hdr;
	Elf_Shdr       *shdr;
	Elf_Sym        *es;
	int		nbytes, i, j;
	vm_offset_t	mapbase;
	size_t		mapsize;
	int		error = 0;
	int		resid;
	elf_file_t	ef;
	linker_file_t	lf;
	int		symtabindex;
	int		symstrindex;
	int		shstrindex;
	int		nsym;
	int		pb, rl, ra;
	int		alignmask;

	/* XXX Hack for firmware loading where p == NULL */
	if (p == NULL) {
		p = &proc0;
	}

	KKASSERT(p != NULL);
	if (p->p_ucred == NULL) {
		kprintf("link_elf_obj_load_file: cannot load '%s' from filesystem"
			" this early\n", filename);
		return ENOENT;
	}
	shdr = NULL;
	lf = NULL;
	mapsize = 0;
	hdr = NULL;
	pathname = linker_search_path(filename);
	if (pathname == NULL)
		return ENOENT;

	error = nlookup_init(&nd, pathname, UIO_SYSSPACE, NLC_FOLLOW | NLC_LOCKVP);
	if (error == 0)
		error = vn_open(&nd, NULL, FREAD, 0);
	kfree(pathname, M_LINKER);
	if (error) {
		nlookup_done(&nd);
		return error;
	}
	vp = nd.nl_open_vp;
	nd.nl_open_vp = NULL;
	nlookup_done(&nd);

	/*
	 * Read the elf header from the file.
	 */
	hdr = kmalloc(sizeof(*hdr), M_LINKER, M_WAITOK);
	error = vn_rdwr(UIO_READ, vp, (void *)hdr, sizeof(*hdr), 0,
			UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred, &resid);
	if (error)
		goto out;
	if (resid != 0) {
		error = ENOEXEC;
		goto out;
	}
	if (!IS_ELF(*hdr)) {
		error = ENOEXEC;
		goto out;
	}

	if (hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS
	    || hdr->e_ident[EI_DATA] != ELF_TARG_DATA) {
		link_elf_obj_error(filename, "Unsupported file layout");
		error = ENOEXEC;
		goto out;
	}
	if (hdr->e_ident[EI_VERSION] != EV_CURRENT
	    || hdr->e_version != EV_CURRENT) {
		link_elf_obj_error(filename, "Unsupported file version");
		error = ENOEXEC;
		goto out;
	}
	if (hdr->e_type != ET_REL) {
		error = ENOSYS;
		goto out;
	}
	if (hdr->e_machine != ELF_TARG_MACH) {
		link_elf_obj_error(filename, "Unsupported machine");
		error = ENOEXEC;
		goto out;
	}

	ef = kmalloc(sizeof(struct elf_file), M_LINKER, M_WAITOK | M_ZERO);
	lf = linker_make_file(filename, ef, &link_elf_obj_file_ops);
	if (lf == NULL) {
		kfree(ef, M_LINKER);
		error = ENOMEM;
		goto out;
	}
	ef->nprogtab = 0;
	ef->e_shdr = NULL;
	ef->nreltab = 0;
	ef->nrelatab = 0;

	/* Allocate and read in the section header */
	nbytes = hdr->e_shnum * hdr->e_shentsize;
	if (nbytes == 0 || hdr->e_shoff == 0 ||
	    hdr->e_shentsize != sizeof(Elf_Shdr)) {
		error = ENOEXEC;
		goto out;
	}
	shdr = kmalloc(nbytes, M_LINKER, M_WAITOK);
	ef->e_shdr = shdr;
	error = vn_rdwr(UIO_READ, vp, (caddr_t) shdr, nbytes, hdr->e_shoff,
			UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred, &resid);
	if (error)
		goto out;
	if (resid) {
		error = ENOEXEC;
		goto out;
	}
	/* Scan the section header for information and table sizing. */
	nsym = 0;
	symtabindex = -1;
	symstrindex = -1;
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
			ef->nprogtab++;
			break;
		case SHT_SYMTAB:
			nsym++;
			symtabindex = i;
			symstrindex = shdr[i].sh_link;
			break;
		case SHT_REL:
			ef->nreltab++;
			break;
		case SHT_RELA:
			ef->nrelatab++;
			break;
		case SHT_STRTAB:
			break;
		}
	}
	if (ef->nprogtab == 0) {
		link_elf_obj_error(filename, "file has no contents");
		error = ENOEXEC;
		goto out;
	}
	if (nsym != 1) {
		/* Only allow one symbol table for now */
		link_elf_obj_error(filename, "file has no valid symbol table");
		error = ENOEXEC;
		goto out;
	}
	if (symstrindex < 0 || symstrindex > hdr->e_shnum ||
	    shdr[symstrindex].sh_type != SHT_STRTAB) {
		link_elf_obj_error(filename, "file has invalid symbol strings");
		error = ENOEXEC;
		goto out;
	}
	/* Allocate space for tracking the load chunks */
	if (ef->nprogtab != 0)
		ef->progtab = kmalloc(ef->nprogtab * sizeof(*ef->progtab),
				      M_LINKER, M_WAITOK | M_ZERO);
	if (ef->nreltab != 0)
		ef->reltab = kmalloc(ef->nreltab * sizeof(*ef->reltab),
				     M_LINKER, M_WAITOK | M_ZERO);
	if (ef->nrelatab != 0)
		ef->relatab = kmalloc(ef->nrelatab * sizeof(*ef->relatab),
				      M_LINKER, M_WAITOK | M_ZERO);
	if ((ef->nprogtab != 0 && ef->progtab == NULL) ||
	    (ef->nreltab != 0 && ef->reltab == NULL) ||
	    (ef->nrelatab != 0 && ef->relatab == NULL)) {
		error = ENOMEM;
		goto out;
	}
	if (symtabindex == -1)
		panic("lost symbol table index");
	/* Allocate space for and load the symbol table */
	ef->ddbsymcnt = shdr[symtabindex].sh_size / sizeof(Elf_Sym);
	ef->ddbsymtab = kmalloc(shdr[symtabindex].sh_size, M_LINKER, M_WAITOK);
	error = vn_rdwr(UIO_READ, vp, (void *)ef->ddbsymtab,
			shdr[symtabindex].sh_size, shdr[symtabindex].sh_offset,
			UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred, &resid);
	if (error)
		goto out;
	if (resid != 0) {
		error = EINVAL;
		goto out;
	}
	if (symstrindex == -1)
		panic("lost symbol string index");
	/* Allocate space for and load the symbol strings */
	ef->ddbstrcnt = shdr[symstrindex].sh_size;
	ef->ddbstrtab = kmalloc(shdr[symstrindex].sh_size, M_LINKER, M_WAITOK);
	error = vn_rdwr(UIO_READ, vp, ef->ddbstrtab,
			shdr[symstrindex].sh_size, shdr[symstrindex].sh_offset,
			UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred, &resid);
	if (error)
		goto out;
	if (resid != 0) {
		error = EINVAL;
		goto out;
	}
	/* Do we have a string table for the section names?  */
	shstrindex = -1;
	if (hdr->e_shstrndx != 0 &&
	    shdr[hdr->e_shstrndx].sh_type == SHT_STRTAB) {
		shstrindex = hdr->e_shstrndx;
		ef->shstrcnt = shdr[shstrindex].sh_size;
		ef->shstrtab = kmalloc(shdr[shstrindex].sh_size, M_LINKER,
				       M_WAITOK);
		error = vn_rdwr(UIO_READ, vp, ef->shstrtab,
				shdr[shstrindex].sh_size, shdr[shstrindex].sh_offset,
				UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred, &resid);
		if (error)
			goto out;
		if (resid != 0) {
			error = EINVAL;
			goto out;
		}
	}
	/* Size up code/data(progbits) and bss(nobits). */
	alignmask = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
			alignmask = shdr[i].sh_addralign - 1;
			mapsize += alignmask;
			mapsize &= ~alignmask;
			mapsize += shdr[i].sh_size;
			break;
		}
	}

	/*
	 * We know how much space we need for the text/data/bss/etc. This
	 * stuff needs to be in a single chunk so that profiling etc can get
	 * the bounds and gdb can associate offsets with modules
	 */
	ef->object = vm_object_allocate(OBJT_DEFAULT,
					round_page(mapsize) >> PAGE_SHIFT);
	if (ef->object == NULL) {
		error = ENOMEM;
		goto out;
	}
	vm_object_hold(ef->object);
	vm_object_reference_locked(ef->object);
	ef->address = (caddr_t) vm_map_min(&kernel_map);
	ef->bytes = 0;

	/*
	 * In order to satisfy x86_64's architectural requirements on the
	 * location of code and data in the kernel's address space, request a
	 * mapping that is above the kernel.
	 *
	 * vkernel64's text+data is outside the managed VM space entirely.
	 */
#if defined(__x86_64__) && defined(_KERNEL_VIRTUAL)
	error = vkernel_module_memory_alloc(&mapbase, round_page(mapsize));
	vm_object_drop(ef->object);
#else
	mapbase = KERNBASE;
	error = vm_map_find(&kernel_map, ef->object, 0, &mapbase,
			    round_page(mapsize), PAGE_SIZE,
			    TRUE, VM_MAPTYPE_NORMAL,
			    VM_PROT_ALL, VM_PROT_ALL, FALSE);
	vm_object_drop(ef->object);
	if (error) {
		vm_object_deallocate(ef->object);
		ef->object = NULL;
		goto out;
	}
	/* Wire the pages */
	error = vm_map_wire(&kernel_map, mapbase,
			    mapbase + round_page(mapsize), 0);
#endif
	if (error != KERN_SUCCESS) {
		error = ENOMEM;
		goto out;
	}
	/* Inform the kld system about the situation */
	lf->address = ef->address = (caddr_t) mapbase;
	lf->size = round_page(mapsize);
	ef->bytes = mapsize;

	/*
	 * Now load code/data(progbits), zero bss(nobits), allocate space for
	 * and load relocs
	 */
	pb = 0;
	rl = 0;
	ra = 0;
	alignmask = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		switch (shdr[i].sh_type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
			alignmask = shdr[i].sh_addralign - 1;
			mapbase += alignmask;
			mapbase &= ~alignmask;
			if (ef->shstrtab && shdr[i].sh_name != 0)
				ef->progtab[pb].name =
					ef->shstrtab + shdr[i].sh_name;
			else if (shdr[i].sh_type == SHT_PROGBITS)
				ef->progtab[pb].name = "<<PROGBITS>>";
			else
				ef->progtab[pb].name = "<<NOBITS>>";
#if 0
			if (ef->progtab[pb].name != NULL &&
			    !strcmp(ef->progtab[pb].name, "set_pcpu"))
				ef->progtab[pb].addr =
					dpcpu_alloc(shdr[i].sh_size);
#ifdef VIMAGE
			else if (ef->progtab[pb].name != NULL &&
				 !strcmp(ef->progtab[pb].name, VNET_SETNAME))
				ef->progtab[pb].addr =
					vnet_data_alloc(shdr[i].sh_size);
#endif
			else
#endif
				ef->progtab[pb].addr =
					(void *)(uintptr_t) mapbase;
			if (ef->progtab[pb].addr == NULL) {
				error = ENOSPC;
				goto out;
			}
			ef->progtab[pb].size = shdr[i].sh_size;
			ef->progtab[pb].sec = i;
			if (shdr[i].sh_type == SHT_PROGBITS) {
				error = vn_rdwr(UIO_READ, vp,
						ef->progtab[pb].addr,
						shdr[i].sh_size, shdr[i].sh_offset,
						UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred,
						&resid);
				if (error)
					goto out;
				if (resid != 0) {
					error = EINVAL;
					goto out;
				}
#if 0
				/* Initialize the per-cpu or vnet area. */
				if (ef->progtab[pb].addr != (void *)mapbase &&
				    !strcmp(ef->progtab[pb].name, "set_pcpu"))
					dpcpu_copy(ef->progtab[pb].addr,
						   shdr[i].sh_size);
#ifdef VIMAGE
				else if (ef->progtab[pb].addr !=
					 (void *)mapbase &&
					 !strcmp(ef->progtab[pb].name, VNET_SETNAME))
					vnet_data_copy(ef->progtab[pb].addr,
						       shdr[i].sh_size);
#endif
#endif
			} else
				bzero(ef->progtab[pb].addr, shdr[i].sh_size);

			/* Update all symbol values with the offset. */
			for (j = 0; j < ef->ddbsymcnt; j++) {
				es = &ef->ddbsymtab[j];
				if (es->st_shndx != i)
					continue;
				es->st_value += (Elf_Addr) ef->progtab[pb].addr;
			}
			mapbase += shdr[i].sh_size;
			pb++;
			break;
		case SHT_REL:
			ef->reltab[rl].rel = kmalloc(shdr[i].sh_size, M_LINKER, M_WAITOK);
			ef->reltab[rl].nrel = shdr[i].sh_size / sizeof(Elf_Rel);
			ef->reltab[rl].sec = shdr[i].sh_info;
			error = vn_rdwr(UIO_READ, vp,
					(void *)ef->reltab[rl].rel,
					shdr[i].sh_size, shdr[i].sh_offset,
					UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred, &resid);
			if (error)
				goto out;
			if (resid != 0) {
				error = EINVAL;
				goto out;
			}
			rl++;
			break;
		case SHT_RELA:
			ef->relatab[ra].rela = kmalloc(shdr[i].sh_size, M_LINKER, M_WAITOK);
			ef->relatab[ra].nrela = shdr[i].sh_size / sizeof(Elf_Rela);
			ef->relatab[ra].sec = shdr[i].sh_info;
			error = vn_rdwr(UIO_READ, vp,
					(void *)ef->relatab[ra].rela,
					shdr[i].sh_size, shdr[i].sh_offset,
					UIO_SYSSPACE, IO_NODELOCKED, p->p_ucred, &resid);
			if (error)
				goto out;
			if (resid != 0) {
				error = EINVAL;
				goto out;
			}
			ra++;
			break;
		}
	}
	if (pb != ef->nprogtab)
		panic("lost progbits");
	if (rl != ef->nreltab)
		panic("lost reltab");
	if (ra != ef->nrelatab)
		panic("lost relatab");
	if (mapbase != (vm_offset_t) ef->address + mapsize)
		panic("mapbase 0x%lx != address %p + mapsize 0x%lx (0x%lx)",
		      mapbase, ef->address, mapsize,
		      (vm_offset_t) ef->address + mapsize);

	/* Local intra-module relocations */
	link_elf_obj_reloc_local(lf);

	/* Pull in dependencies */
	error = linker_load_dependencies(lf);
	if (error)
		goto out;

	/* External relocations */
	error = relocate_file(lf);
	if (error)
		goto out;

	*result = lf;

out:
	if (error && lf)
		linker_file_unload(lf /*, LINKER_UNLOAD_FORCE */);
	if (hdr)
		kfree(hdr, M_LINKER);
	vn_unlock(vp);
	vn_close(vp, FREAD, NULL);

	return error;
}

static void
link_elf_obj_unload_file(linker_file_t file)
{
	elf_file_t	ef = file->priv;
	int i;

	if (ef->progtab) {
		for (i = 0; i < ef->nprogtab; i++) {
			if (ef->progtab[i].size == 0)
				continue;
			if (ef->progtab[i].name == NULL)
				continue;
#if 0
			if (!strcmp(ef->progtab[i].name, "set_pcpu"))
				dpcpu_free(ef->progtab[i].addr,
				    ef->progtab[i].size);
#ifdef VIMAGE
			else if (!strcmp(ef->progtab[i].name, VNET_SETNAME))
				vnet_data_free(ef->progtab[i].addr,
				    ef->progtab[i].size);
#endif
#endif
		}
	}
	if (ef->preloaded) {
		if (ef->reltab)
			kfree(ef->reltab, M_LINKER);
		if (ef->relatab)
			kfree(ef->relatab, M_LINKER);
		if (ef->progtab)
			kfree(ef->progtab, M_LINKER);
		if (ef->ctftab)
			kfree(ef->ctftab, M_LINKER);
		if (ef->ctfoff)
			kfree(ef->ctfoff, M_LINKER);
		if (ef->typoff)
			kfree(ef->typoff, M_LINKER);
		if (file->filename != NULL)
			preload_delete_name(file->filename);
		kfree(ef, M_LINKER);
		/* XXX reclaim module memory? */
		return;
	}

	for (i = 0; i < ef->nreltab; i++)
		if (ef->reltab[i].rel)
			kfree(ef->reltab[i].rel, M_LINKER);
	for (i = 0; i < ef->nrelatab; i++)
		if (ef->relatab[i].rela)
			kfree(ef->relatab[i].rela, M_LINKER);
	if (ef->reltab)
		kfree(ef->reltab, M_LINKER);
	if (ef->relatab)
		kfree(ef->relatab, M_LINKER);
	if (ef->progtab)
		kfree(ef->progtab, M_LINKER);

	if (ef->object) {
#if defined(__x86_64__) && defined(_KERNEL_VIRTUAL)
		vkernel_module_memory_free((vm_offset_t)ef->address, ef->bytes);
#else
		vm_map_remove(&kernel_map, (vm_offset_t) ef->address,
		    (vm_offset_t) ef->address +
		    (ef->object->size << PAGE_SHIFT));
#endif
		vm_object_deallocate(ef->object);
		ef->object = NULL;
	}
	if (ef->e_shdr)
		kfree(ef->e_shdr, M_LINKER);
	if (ef->ddbsymtab)
		kfree(ef->ddbsymtab, M_LINKER);
	if (ef->ddbstrtab)
		kfree(ef->ddbstrtab, M_LINKER);
	if (ef->shstrtab)
		kfree(ef->shstrtab, M_LINKER);
	if (ef->ctftab)
		kfree(ef->ctftab, M_LINKER);
	if (ef->ctfoff)
		kfree(ef->ctfoff, M_LINKER);
	if (ef->typoff)
		kfree(ef->typoff, M_LINKER);
	kfree(ef, M_LINKER);
}

static const char *
symbol_name(elf_file_t ef, Elf_Size r_info)
{
	const Elf_Sym  *ref;

	if (ELF_R_SYM(r_info)) {
		ref = ef->ddbsymtab + ELF_R_SYM(r_info);
		return ef->ddbstrtab + ref->st_name;
	} else
		return NULL;
}

static Elf_Addr
findbase(elf_file_t ef, int sec)
{
	int i;
	Elf_Addr base = 0;

	for (i = 0; i < ef->nprogtab; i++) {
		if (sec == ef->progtab[i].sec) {
			base = (Elf_Addr)ef->progtab[i].addr;
			break;
		}
	}
	return base;
}

static int
relocate_file(linker_file_t lf)
{
	elf_file_t	ef = lf->priv;
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	const char *symname;
	const Elf_Sym *sym;
	int i;
	Elf_Size symidx;
	Elf_Addr base;

	/* Perform relocations without addend if there are any: */
	for (i = 0; i < ef->nreltab; i++) {
		rel = ef->reltab[i].rel;
		if (rel == NULL)
			panic("lost a reltab!");
		rellim = rel + ef->reltab[i].nrel;
		base = findbase(ef, ef->reltab[i].sec);
		if (base == 0)
			panic("lost base for reltab");
		for ( ; rel < rellim; rel++) {
			symidx = ELF_R_SYM(rel->r_info);
			if (symidx >= ef->ddbsymcnt)
				continue;
			sym = ef->ddbsymtab + symidx;
			/* Local relocs are already done */
			if (ELF_ST_BIND(sym->st_info) == STB_LOCAL)
				continue;
			if (elf_reloc(lf, base, rel, ELF_RELOC_REL,
			    elf_obj_lookup)) {
				symname = symbol_name(ef, rel->r_info);
				kprintf("link_elf_obj_obj: symbol %s undefined\n",
				    symname);
				return ENOENT;
			}
		}
	}

	/* Perform relocations with addend if there are any: */
	for (i = 0; i < ef->nrelatab; i++) {
		rela = ef->relatab[i].rela;
		if (rela == NULL)
			panic("lost a relatab!");
		relalim = rela + ef->relatab[i].nrela;
		base = findbase(ef, ef->relatab[i].sec);
		if (base == 0)
			panic("lost base for relatab");
		for ( ; rela < relalim; rela++) {
			symidx = ELF_R_SYM(rela->r_info);
			if (symidx >= ef->ddbsymcnt)
				continue;
			sym = ef->ddbsymtab + symidx;
			/* Local relocs are already done */
			if (ELF_ST_BIND(sym->st_info) == STB_LOCAL)
				continue;
			if (elf_reloc(lf, base, rela, ELF_RELOC_RELA,
			    elf_obj_lookup)) {
				symname = symbol_name(ef, rela->r_info);
				kprintf("link_elf_obj_obj: symbol %s undefined\n",
				    symname);
				return ENOENT;
			}
		}
	}

	return 0;
}

static int
link_elf_obj_lookup_symbol(linker_file_t lf, const char *name, c_linker_sym_t *sym)
{
	elf_file_t ef = lf->priv;
	const Elf_Sym *symp;
	const char *strp;
	int i;

	for (i = 0, symp = ef->ddbsymtab; i < ef->ddbsymcnt; i++, symp++) {
		strp = ef->ddbstrtab + symp->st_name;
		if (symp->st_shndx != SHN_UNDEF && strcmp(name, strp) == 0) {
			*sym = (c_linker_sym_t) symp;
			return 0;
		}
	}
	return ENOENT;
}

static int
link_elf_obj_symbol_values(linker_file_t lf, c_linker_sym_t sym,
    linker_symval_t *symval)
{
	elf_file_t ef = lf->priv;
	const Elf_Sym *es = (const Elf_Sym*) sym;

	if (es >= ef->ddbsymtab && es < (ef->ddbsymtab + ef->ddbsymcnt)) {
		symval->name = ef->ddbstrtab + es->st_name;
		symval->value = (caddr_t)es->st_value;
		symval->size = es->st_size;
		return 0;
	}
	return ENOENT;
}

static int
link_elf_obj_search_symbol(linker_file_t lf, caddr_t value,
    c_linker_sym_t *sym, long *diffp)
{
	elf_file_t ef = lf->priv;
	u_long off = (uintptr_t) (void *) value;
	u_long diff = off;
	u_long st_value;
	const Elf_Sym *es;
	const Elf_Sym *best = NULL;
	int i;

	for (i = 0, es = ef->ddbsymtab; i < ef->ddbsymcnt; i++, es++) {
		if (es->st_name == 0)
			continue;
		st_value = es->st_value;
		if (off >= st_value) {
			if (off - st_value < diff) {
				diff = off - st_value;
				best = es;
				if (diff == 0)
					break;
			} else if (off - st_value == diff) {
				best = es;
			}
		}
	}
	if (best == NULL)
		*diffp = off;
	else
		*diffp = diff;
	*sym = (c_linker_sym_t) best;

	return 0;
}

/*
 * Look up a linker set on an ELF system.
 */
static int
link_elf_obj_lookup_set(linker_file_t lf, const char *name,
    void ***startp, void ***stopp, int *countp)
{
	elf_file_t ef = lf->priv;
	void **start, **stop;
	int i, count;

	/* Relative to section number */
	for (i = 0; i < ef->nprogtab; i++) {
		if ((strncmp(ef->progtab[i].name, "set_", 4) == 0) &&
		    strcmp(ef->progtab[i].name + 4, name) == 0) {
			start  = (void **)ef->progtab[i].addr;
			stop = (void **)((char *)ef->progtab[i].addr +
			    ef->progtab[i].size);
			count = stop - start;
			if (startp)
				*startp = start;
			if (stopp)
				*stopp = stop;
			if (countp)
				*countp = count;
			return (0);
		}
	}
	return (ESRCH);
}

/*
 * Symbol lookup function that can be used when the symbol index is known (ie
 * in relocations). It uses the symbol index instead of doing a fully fledged
 * hash table based lookup when such is valid. For example for local symbols.
 * This is not only more efficient, it's also more correct. It's not always
 * the case that the symbol can be found through the hash table.
 */
static int
elf_obj_lookup(linker_file_t lf, Elf_Size symidx, int deps, Elf_Addr *result)
{
	elf_file_t ef = lf->priv;
	const Elf_Sym *sym;
	const char *symbol;

	/* Don't even try to lookup the symbol if the index is bogus. */
	if (symidx >= ef->ddbsymcnt)
		return (ENOENT);

	sym = ef->ddbsymtab + symidx;

	/* Quick answer if there is a definition included. */
	if (sym->st_shndx != SHN_UNDEF) {
		*result = sym->st_value;
		return (0);
	}

	/* If we get here, then it is undefined and needs a lookup. */
	switch (ELF_ST_BIND(sym->st_info)) {
	case STB_LOCAL:
		/* Local, but undefined? huh? */
		return (ENOENT);

	case STB_GLOBAL:
		/* Relative to Data or Function name */
		symbol = ef->ddbstrtab + sym->st_name;

		/* Force a lookup failure if the symbol name is bogus. */
		if (*symbol == 0)
			return (ENOENT);
		return (linker_file_lookup_symbol(lf, symbol, deps, (caddr_t *)result));

	case STB_WEAK:
		kprintf("link_elf_obj_obj: Weak symbols not supported\n");
		return (ENOENT);

	default:
		return (ENOENT);
	}
}

static void
link_elf_obj_fix_link_set(elf_file_t ef)
{
	static const char startn[] = "__start_";
	static const char stopn[] = "__stop_";
	Elf_Sym *sym;
	const char *sym_name, *linkset_name;
	Elf_Addr startp, stopp;
	Elf_Size symidx;
	int start, i;

	startp = stopp = 0;
	for (symidx = 1 /* zero entry is special */;
		symidx < ef->ddbsymcnt; symidx++) {
		sym = ef->ddbsymtab + symidx;
		if (sym->st_shndx != SHN_UNDEF)
			continue;

		sym_name = ef->ddbstrtab + sym->st_name;
		if (strncmp(sym_name, startn, sizeof(startn) - 1) == 0) {
			start = 1;
			linkset_name = sym_name + sizeof(startn) - 1;
		}
		else if (strncmp(sym_name, stopn, sizeof(stopn) - 1) == 0) {
			start = 0;
			linkset_name = sym_name + sizeof(stopn) - 1;
		}
		else
			continue;

		for (i = 0; i < ef->nprogtab; i++) {
			if (strcmp(ef->progtab[i].name, linkset_name) == 0) {
				startp = (Elf_Addr)ef->progtab[i].addr;
				stopp = (Elf_Addr)(startp + ef->progtab[i].size);
				break;
			}
		}
		if (i == ef->nprogtab)
			continue;

		sym->st_value = start ? startp : stopp;
		sym->st_shndx = i;
	}
}

static void
link_elf_obj_reloc_local(linker_file_t lf)
{
	elf_file_t ef = lf->priv;
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	const Elf_Sym *sym;
	Elf_Addr base;
	int i;
	Elf_Size symidx;

	link_elf_obj_fix_link_set(ef);

	/* Perform relocations without addend if there are any: */
	for (i = 0; i < ef->nreltab; i++) {
		rel = ef->reltab[i].rel;
		if (rel == NULL)
			panic("lost a reltab!");
		rellim = rel + ef->reltab[i].nrel;
		base = findbase(ef, ef->reltab[i].sec);
		if (base == 0)
			panic("lost base for reltab");
		for ( ; rel < rellim; rel++) {
			symidx = ELF_R_SYM(rel->r_info);
			if (symidx >= ef->ddbsymcnt)
				continue;
			sym = ef->ddbsymtab + symidx;
			/* Only do local relocs */
			if (ELF_ST_BIND(sym->st_info) != STB_LOCAL)
				continue;
			elf_reloc_local(lf, base, rel, ELF_RELOC_REL,
			    elf_obj_lookup);
		}
	}

	/* Perform relocations with addend if there are any: */
	for (i = 0; i < ef->nrelatab; i++) {
		rela = ef->relatab[i].rela;
		if (rela == NULL)
			panic("lost a relatab!");
		relalim = rela + ef->relatab[i].nrela;
		base = findbase(ef, ef->relatab[i].sec);
		if (base == 0)
			panic("lost base for relatab");
		for ( ; rela < relalim; rela++) {
			symidx = ELF_R_SYM(rela->r_info);
			if (symidx >= ef->ddbsymcnt)
				continue;
			sym = ef->ddbsymtab + symidx;
			/* Only do local relocs */
			if (ELF_ST_BIND(sym->st_info) != STB_LOCAL)
				continue;
			elf_reloc_local(lf, base, rela, ELF_RELOC_RELA,
			    elf_obj_lookup);
		}
	}
}
