/*-
 * Copyright (c) 2000 David O'Brien
 * Copyright (c) 1995-1996 Søren Schmidt
 * Copyright (c) 1996 Peter Wemm
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 * $FreeBSD: src/sys/kern/imgact_elf.c,v 1.73.2.13 2002/12/28 19:49:41 dillon Exp $
 */

#include <sys/param.h>
#include <sys/exec.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/imgact.h>
#include <sys/imgact_elf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/pioctl.h>
#include <sys/procfs.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/vnode.h>
#include <sys/eventhandler.h>

#include <cpu/lwbuf.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <sys/lock.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>

#include <machine/elf.h>
#include <machine/md_var.h>
#include <sys/mount.h>
#include <sys/ckpt.h>

#define OLD_EI_BRAND	8
#define truncps(va,ps)	((va) & ~(ps - 1))
#define aligned(a,t)	(truncps((u_long)(a), sizeof(t)) == (u_long)(a))

static int __elfN(check_header)(const Elf_Ehdr *hdr);
static Elf_Brandinfo *__elfN(get_brandinfo)(struct image_params *imgp,
    const char *interp, int32_t *osrel);
static int __elfN(load_file)(struct proc *p, const char *file, u_long *addr,
    u_long *entry);
static int __elfN(load_section)(struct proc *p,
    struct vmspace *vmspace, struct vnode *vp,
    vm_offset_t offset, caddr_t vmaddr, size_t memsz, size_t filsz,
    vm_prot_t prot);
static int __CONCAT(exec_, __elfN(imgact))(struct image_params *imgp);
static boolean_t __elfN(bsd_trans_osrel)(const Elf_Note *note,
    int32_t *osrel);
static boolean_t __elfN(check_note)(struct image_params *imgp,
    Elf_Brandnote *checknote, int32_t *osrel);
static vm_prot_t __elfN(trans_prot)(Elf_Word);
static Elf_Word __elfN(untrans_prot)(vm_prot_t);
static boolean_t check_PT_NOTE(struct image_params *imgp,
    Elf_Brandnote *checknote, int32_t *osrel, const Elf_Phdr * pnote);
static boolean_t extract_interpreter(struct image_params *imgp,
    const Elf_Phdr *pinterpreter, char *data);

static int elf_legacy_coredump = 0;
static int __elfN(fallback_brand) = -1;
#if defined(__x86_64__)
SYSCTL_NODE(_kern, OID_AUTO, elf64, CTLFLAG_RW, 0, "");
SYSCTL_INT(_debug, OID_AUTO, elf64_legacy_coredump, CTLFLAG_RW,
    &elf_legacy_coredump, 0, "legacy coredump mode");
SYSCTL_INT(_kern_elf64, OID_AUTO, fallback_brand, CTLFLAG_RW,
    &elf64_fallback_brand, 0, "ELF64 brand of last resort");
TUNABLE_INT("kern.elf64.fallback_brand", &elf64_fallback_brand);
#else /* i386 assumed */
SYSCTL_NODE(_kern, OID_AUTO, elf32, CTLFLAG_RW, 0, "");
SYSCTL_INT(_debug, OID_AUTO, elf32_legacy_coredump, CTLFLAG_RW,
    &elf_legacy_coredump, 0, "legacy coredump mode");
SYSCTL_INT(_kern_elf32, OID_AUTO, fallback_brand, CTLFLAG_RW,
    &elf32_fallback_brand, 0, "ELF32 brand of last resort");
TUNABLE_INT("kern.elf32.fallback_brand", &elf32_fallback_brand);
#endif

static Elf_Brandinfo *elf_brand_list[MAX_BRANDS];

static const char DRAGONFLY_ABI_VENDOR[] = "DragonFly";
static const char FREEBSD_ABI_VENDOR[]   = "FreeBSD";

Elf_Brandnote __elfN(dragonfly_brandnote) = {
	.hdr.n_namesz	= sizeof(DRAGONFLY_ABI_VENDOR),
	.hdr.n_descsz	= sizeof(int32_t),
	.hdr.n_type	= 1,
	.vendor		= DRAGONFLY_ABI_VENDOR,
	.flags		= BN_TRANSLATE_OSREL,
	.trans_osrel	= __elfN(bsd_trans_osrel),
};

Elf_Brandnote __elfN(freebsd_brandnote) = {
	.hdr.n_namesz	= sizeof(FREEBSD_ABI_VENDOR),
	.hdr.n_descsz	= sizeof(int32_t),
	.hdr.n_type	= 1,
	.vendor		= FREEBSD_ABI_VENDOR,
	.flags		= BN_TRANSLATE_OSREL,
	.trans_osrel	= __elfN(bsd_trans_osrel),
};

int
__elfN(insert_brand_entry)(Elf_Brandinfo *entry)
{
	int i;

	for (i = 0; i < MAX_BRANDS; i++) {
		if (elf_brand_list[i] == NULL) {
			elf_brand_list[i] = entry;
			break;
		}
	}
	if (i == MAX_BRANDS) {
		uprintf("WARNING: %s: could not insert brandinfo entry: %p\n",
			__func__, entry);
		return (-1);
	}
	return (0);
}

int
__elfN(remove_brand_entry)(Elf_Brandinfo *entry)
{
	int i;

	for (i = 0; i < MAX_BRANDS; i++) {
		if (elf_brand_list[i] == entry) {
			elf_brand_list[i] = NULL;
			break;
		}
	}
	if (i == MAX_BRANDS)
		return (-1);
	return (0);
}

/*
 * Check if an elf brand is being used anywhere in the system.
 *
 * Used by the linux emulation module unloader.  This isn't safe from
 * races.
 */
struct elf_brand_inuse_info {
	int rval;
	Elf_Brandinfo *entry;
};

static int elf_brand_inuse_callback(struct proc *p, void *data);

int
__elfN(brand_inuse)(Elf_Brandinfo *entry)
{
	struct elf_brand_inuse_info info;

	info.rval = FALSE;
	info.entry = entry;
	allproc_scan(elf_brand_inuse_callback, &info);
	return (info.rval);
}

static
int
elf_brand_inuse_callback(struct proc *p, void *data)
{
	struct elf_brand_inuse_info *info = data;

	if (p->p_sysent == info->entry->sysvec) {
		info->rval = TRUE;
		return (-1);
	}
	return (0);
}

static int
__elfN(check_header)(const Elf_Ehdr *hdr)
{
	Elf_Brandinfo *bi;
	int i;

	if (!IS_ELF(*hdr) ||
	    hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
	    hdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	    hdr->e_ident[EI_VERSION] != EV_CURRENT ||
	    hdr->e_phentsize != sizeof(Elf_Phdr) ||
	    hdr->e_ehsize != sizeof(Elf_Ehdr) ||
	    hdr->e_version != ELF_TARG_VER)
		return (ENOEXEC);

	/*
	 * Make sure we have at least one brand for this machine.
	 */

	for (i = 0; i < MAX_BRANDS; i++) {
		bi = elf_brand_list[i];
		if (bi != NULL && bi->machine == hdr->e_machine)
			break;
	}
	if (i == MAX_BRANDS)
		return (ENOEXEC);

	return (0);
}

static int
__elfN(load_section)(struct proc *p, struct vmspace *vmspace, struct vnode *vp,
		 vm_offset_t offset, caddr_t vmaddr, size_t memsz,
		 size_t filsz, vm_prot_t prot)
{
	size_t map_len;
	vm_offset_t map_addr;
	int error, rv, cow;
	int count;
	int shared;
	size_t copy_len;
	vm_object_t object;
	vm_offset_t file_addr;

	object = vp->v_object;
	error = 0;

	/*
	 * In most cases we will be able to use a shared lock on the
	 * object we are inserting into the map.  The lock will be
	 * upgraded in situations where new VM pages must be allocated.
	 */
	vm_object_hold_shared(object);
	shared = 1;

	/*
	 * It's necessary to fail if the filsz + offset taken from the
	 * header is greater than the actual file pager object's size.
	 * If we were to allow this, then the vm_map_find() below would
	 * walk right off the end of the file object and into the ether.
	 *
	 * While I'm here, might as well check for something else that
	 * is invalid: filsz cannot be greater than memsz.
	 */
	if ((off_t)filsz + offset > vp->v_filesize || filsz > memsz) {
		uprintf("elf_load_section: truncated ELF file\n");
		vm_object_drop(object);
		return (ENOEXEC);
	}

	map_addr = trunc_page((vm_offset_t)vmaddr);
	file_addr = trunc_page(offset);

	/*
	 * We have two choices.  We can either clear the data in the last page
	 * of an oversized mapping, or we can start the anon mapping a page
	 * early and copy the initialized data into that first page.  We
	 * choose the second..
	 */
	if (memsz > filsz)
		map_len = trunc_page(offset+filsz) - file_addr;
	else
		map_len = round_page(offset+filsz) - file_addr;

	if (map_len != 0) {
		vm_object_reference_locked(object);

		/* cow flags: don't dump readonly sections in core */
		cow = MAP_COPY_ON_WRITE | MAP_PREFAULT;
		if ((prot & VM_PROT_WRITE) == 0)
			cow |= MAP_DISABLE_COREDUMP;
		if (shared == 0)
			cow |= MAP_PREFAULT_RELOCK;

		count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
		vm_map_lock(&vmspace->vm_map);
		rv = vm_map_insert(&vmspace->vm_map, &count,
				      object, NULL,
				      file_addr,	/* file offset */
				      map_addr,		/* virtual start */
				      map_addr + map_len,/* virtual end */
				      VM_MAPTYPE_NORMAL,
				      prot, VM_PROT_ALL,
				      cow);
		vm_map_unlock(&vmspace->vm_map);
		vm_map_entry_release(count);

		/*
		 * NOTE: Object must have a hold ref when calling
		 * vm_object_deallocate().
		 */
		if (rv != KERN_SUCCESS) {
			vm_object_drop(object);
			vm_object_deallocate(object);
			return (EINVAL);
		}

		/* we can stop now if we've covered it all */
		if (memsz == filsz) {
			vm_object_drop(object);
			return (0);
		}
	}

	/*
	 * We have to get the remaining bit of the file into the first part
	 * of the oversized map segment.  This is normally because the .data
	 * segment in the file is extended to provide bss.  It's a neat idea
	 * to try and save a page, but it's a pain in the behind to implement.
	 */
	copy_len = (offset + filsz) - trunc_page(offset + filsz);
	map_addr = trunc_page((vm_offset_t)vmaddr + filsz);
	map_len = round_page((vm_offset_t)vmaddr + memsz) - map_addr;

	/* This had damn well better be true! */
        if (map_len != 0) {
		count = vm_map_entry_reserve(MAP_RESERVE_COUNT);
		vm_map_lock(&vmspace->vm_map);
		rv = vm_map_insert(&vmspace->vm_map, &count,
					NULL, NULL,
					0,
					map_addr,
					map_addr + map_len,
					VM_MAPTYPE_NORMAL,
					VM_PROT_ALL, VM_PROT_ALL,
					0);
		vm_map_unlock(&vmspace->vm_map);
		vm_map_entry_release(count);
		if (rv != KERN_SUCCESS) {
			vm_object_drop(object);
			return (EINVAL);
		}
	}

	if (copy_len != 0) {
		struct lwbuf *lwb;
		struct lwbuf lwb_cache;
		vm_page_t m;

		m = vm_fault_object_page(object, trunc_page(offset + filsz),
					 VM_PROT_READ, 0, &shared, &error);
		vm_object_drop(object);
		if (m) {
			lwb = lwbuf_alloc(m, &lwb_cache);
			error = copyout((caddr_t)lwbuf_kva(lwb),
					(caddr_t)map_addr, copy_len);
			lwbuf_free(lwb);
			vm_page_unhold(m);
		}
	} else {
		vm_object_drop(object);
	}

	/*
	 * set it to the specified protection
	 */
	if (error == 0) {
		vm_map_protect(&vmspace->vm_map,
			       map_addr, map_addr + map_len,
			       prot, FALSE);
	}
	return (error);
}

/*
 * Load the file "file" into memory.  It may be either a shared object
 * or an executable.
 *
 * The "addr" reference parameter is in/out.  On entry, it specifies
 * the address where a shared object should be loaded.  If the file is
 * an executable, this value is ignored.  On exit, "addr" specifies
 * where the file was actually loaded.
 *
 * The "entry" reference parameter is out only.  On exit, it specifies
 * the entry point for the loaded file.
 */
static int
__elfN(load_file)(struct proc *p, const char *file, u_long *addr, u_long *entry)
{
	struct {
		struct nlookupdata nd;
		struct vattr attr;
		struct image_params image_params;
	} *tempdata;
	const Elf_Ehdr *hdr = NULL;
	const Elf_Phdr *phdr = NULL;
	struct nlookupdata *nd;
	struct vmspace *vmspace = p->p_vmspace;
	struct vattr *attr;
	struct image_params *imgp;
	struct mount *topmnt;
	vm_prot_t prot;
	u_long rbase;
	u_long base_addr = 0;
	int error, i, numsegs;

	tempdata = kmalloc(sizeof(*tempdata), M_TEMP, M_WAITOK);
	nd = &tempdata->nd;
	attr = &tempdata->attr;
	imgp = &tempdata->image_params;

	/*
	 * Initialize part of the common data
	 */
	imgp->proc = p;
	imgp->attr = attr;
	imgp->firstpage = NULL;
	imgp->image_header = NULL;
	imgp->vp = NULL;

	error = nlookup_init(nd, file, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(nd);
	if (error == 0)
		error = cache_vget(&nd->nl_nch, nd->nl_cred,
				   LK_SHARED, &imgp->vp);
	topmnt = nd->nl_nch.mount;
	nlookup_done(nd);
	if (error)
		goto fail;

	/*
	 * Check permissions, modes, uid, etc on the file, and "open" it.
	 */
	error = exec_check_permissions(imgp, topmnt);
	if (error) {
		vn_unlock(imgp->vp);
		goto fail;
	}

	error = exec_map_first_page(imgp);
	/*
	 * Also make certain that the interpreter stays the same, so set
	 * its VTEXT flag, too.
	 */
	if (error == 0)
		vsetflags(imgp->vp, VTEXT);
	vn_unlock(imgp->vp);
	if (error)
                goto fail;

	hdr = (const Elf_Ehdr *)imgp->image_header;
	if ((error = __elfN(check_header)(hdr)) != 0)
		goto fail;
	if (hdr->e_type == ET_DYN)
		rbase = *addr;
	else if (hdr->e_type == ET_EXEC)
		rbase = 0;
	else {
		error = ENOEXEC;
		goto fail;
	}

	/* Only support headers that fit within first page for now      */
	/*    (multiplication of two Elf_Half fields will not overflow) */
	if ((hdr->e_phoff > PAGE_SIZE) ||
	    (hdr->e_phentsize * hdr->e_phnum) > PAGE_SIZE - hdr->e_phoff) {
		error = ENOEXEC;
		goto fail;
	}

	phdr = (const Elf_Phdr *)(imgp->image_header + hdr->e_phoff);
	if (!aligned(phdr, Elf_Addr)) {
		error = ENOEXEC;
		goto fail;
	}

	for (i = 0, numsegs = 0; i < hdr->e_phnum; i++) {
		if (phdr[i].p_type == PT_LOAD && phdr[i].p_memsz != 0) {
			/* Loadable segment */
			prot = __elfN(trans_prot)(phdr[i].p_flags);
			error = __elfN(load_section)(
				    p, vmspace, imgp->vp,
				    phdr[i].p_offset,
				    (caddr_t)phdr[i].p_vaddr +
				    rbase,
				    phdr[i].p_memsz,
				    phdr[i].p_filesz, prot);
			if (error != 0)
				goto fail;
			/*
			 * Establish the base address if this is the
			 * first segment.
			 */
			if (numsegs == 0)
  				base_addr = trunc_page(phdr[i].p_vaddr + rbase);
			numsegs++;
		}
	}
	*addr = base_addr;
	*entry = (unsigned long)hdr->e_entry + rbase;

fail:
	if (imgp->firstpage)
		exec_unmap_first_page(imgp);
	if (imgp->vp) {
		vrele(imgp->vp);
		imgp->vp = NULL;
	}
	kfree(tempdata, M_TEMP);

	return (error);
}

static Elf_Brandinfo *
__elfN(get_brandinfo)(struct image_params *imgp, const char *interp,
    int32_t *osrel)
{
	const Elf_Ehdr *hdr = (const Elf_Ehdr *)imgp->image_header;
	Elf_Brandinfo *bi;
	boolean_t ret;
	int i;

	/* We support four types of branding -- (1) the ELF EI_OSABI field
	 * that SCO added to the ELF spec, (2) FreeBSD 3.x's traditional string
	 * branding within the ELF header, (3) path of the `interp_path' field,
	 * and (4) the ".note.ABI-tag" ELF section.
	 */

	/* Look for an ".note.ABI-tag" ELF section */
	for (i = 0; i < MAX_BRANDS; i++) {
		bi = elf_brand_list[i];

		if (bi == NULL)
			continue;
		if (hdr->e_machine == bi->machine && (bi->flags &
		    (BI_BRAND_NOTE|BI_BRAND_NOTE_MANDATORY)) != 0) {
			ret = __elfN(check_note)(imgp, bi->brand_note, osrel);
			if (ret)
				return (bi);
		}
	}

	/* If the executable has a brand, search for it in the brand list. */
	for (i = 0;  i < MAX_BRANDS;  i++) {
		bi = elf_brand_list[i];

                if (bi == NULL || bi->flags & BI_BRAND_NOTE_MANDATORY)
			continue;
		if (hdr->e_machine == bi->machine &&
		    (hdr->e_ident[EI_OSABI] == bi->brand ||
		    strncmp((const char *)&hdr->e_ident[OLD_EI_BRAND],
		    bi->compat_3_brand, strlen(bi->compat_3_brand)) == 0))
			return (bi);
	}

	/* Lacking a known brand, search for a recognized interpreter. */
	if (interp != NULL) {
		for (i = 0;  i < MAX_BRANDS;  i++) {
			bi = elf_brand_list[i];

                        if (bi == NULL || bi->flags & BI_BRAND_NOTE_MANDATORY)
				continue;
			if (hdr->e_machine == bi->machine &&
			    strcmp(interp, bi->interp_path) == 0)
				return (bi);
		}
	}

	/* Lacking a recognized interpreter, try the default brand */
	for (i = 0; i < MAX_BRANDS; i++) {
		bi = elf_brand_list[i];

		if (bi == NULL || bi->flags & BI_BRAND_NOTE_MANDATORY)
			continue;
		if (hdr->e_machine == bi->machine &&
		    __elfN(fallback_brand) == bi->brand)
			return (bi);
	}
	return (NULL);
}

static int
__CONCAT(exec_,__elfN(imgact))(struct image_params *imgp)
{
	const Elf_Ehdr *hdr = (const Elf_Ehdr *) imgp->image_header;
	const Elf_Phdr *phdr;
	Elf_Auxargs *elf_auxargs;
	struct vmspace *vmspace;
	vm_prot_t prot;
	u_long text_size = 0, data_size = 0, total_size = 0;
	u_long text_addr = 0, data_addr = 0;
	u_long seg_size, seg_addr;
	u_long addr, baddr, et_dyn_addr, entry = 0, proghdr = 0;
	int32_t osrel = 0;
	int error = 0, i, n;
	boolean_t failure;
	char *interp = NULL;
	const char *newinterp = NULL;
	Elf_Brandinfo *brand_info;
	char *path;

	/*
	 * Do we have a valid ELF header ?
	 *
	 * Only allow ET_EXEC & ET_DYN here, reject ET_DYN later if a particular
	 * brand doesn't support it.  Both DragonFly platforms do by default.
	 */
	if (__elfN(check_header)(hdr) != 0 ||
	    (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN))
		return (-1);

	/*
	 * From here on down, we return an errno, not -1, as we've
	 * detected an ELF file.
	 */

	if ((hdr->e_phoff > PAGE_SIZE) ||
	    (hdr->e_phoff + hdr->e_phentsize * hdr->e_phnum) > PAGE_SIZE) {
		/* Only support headers in first page for now */
		return (ENOEXEC);
	}
	phdr = (const Elf_Phdr *)(imgp->image_header + hdr->e_phoff);
	if (!aligned(phdr, Elf_Addr))
		return (ENOEXEC);
	n = 0;
	baddr = 0;
	for (i = 0; i < hdr->e_phnum; i++) {
		if (phdr[i].p_type == PT_LOAD) {
			if (n == 0)
				baddr = phdr[i].p_vaddr;
			n++;
			continue;
		}
		if (phdr[i].p_type == PT_INTERP) {
			/*
			 * If interp is already defined there are more than
			 * one PT_INTERP program headers present.  Take only
			 * the first one and ignore the rest.
			 */
			if (interp != NULL)
				continue;

			if (phdr[i].p_filesz == 0 ||
			    phdr[i].p_filesz > PAGE_SIZE ||
			    phdr[i].p_filesz > MAXPATHLEN)
				return (ENOEXEC);

			interp = kmalloc(phdr[i].p_filesz, M_TEMP, M_WAITOK);
			failure = extract_interpreter(imgp, &phdr[i], interp);
			if (failure) {
				kfree(interp, M_TEMP);
				return (ENOEXEC);
			}
			continue;
		}
	}
	
	brand_info = __elfN(get_brandinfo)(imgp, interp, &osrel);
	if (brand_info == NULL) {
		uprintf("ELF binary type \"%u\" not known.\n",
		    hdr->e_ident[EI_OSABI]);
		if (interp != NULL)
		        kfree(interp, M_TEMP);
		return (ENOEXEC);
	}
	if (hdr->e_type == ET_DYN) {
		if ((brand_info->flags & BI_CAN_EXEC_DYN) == 0) {
		        if (interp != NULL)
		                kfree(interp, M_TEMP);
			return (ENOEXEC);
                }
		/*
		 * Honour the base load address from the dso if it is
		 * non-zero for some reason.
		 */
		if (baddr == 0)
			et_dyn_addr = ET_DYN_LOAD_ADDR;
		else
			et_dyn_addr = 0;
	} else
		et_dyn_addr = 0;

	if (interp != NULL && brand_info->interp_newpath != NULL)
		newinterp = brand_info->interp_newpath;

	exec_new_vmspace(imgp, NULL);

	/*
	 * Yeah, I'm paranoid.  There is every reason in the world to get
	 * VTEXT now since from here on out, there are places we can have
	 * a context switch.  Better safe than sorry; I really don't want
	 * the file to change while it's being loaded.
	 */
	vsetflags(imgp->vp, VTEXT);

	vmspace = imgp->proc->p_vmspace;

	for (i = 0; i < hdr->e_phnum; i++) {
		switch (phdr[i].p_type) {
		case PT_LOAD:	/* Loadable segment */
			if (phdr[i].p_memsz == 0)
				break;
			prot = __elfN(trans_prot)(phdr[i].p_flags);

			if ((error = __elfN(load_section)(
					imgp->proc,
					vmspace,
					imgp->vp,
					phdr[i].p_offset,
					(caddr_t)phdr[i].p_vaddr + et_dyn_addr,
					phdr[i].p_memsz,
					phdr[i].p_filesz,
					prot)) != 0) {
                                if (interp != NULL)
                                        kfree (interp, M_TEMP);
				return (error);
                        }

			/*
			 * If this segment contains the program headers,
			 * remember their virtual address for the AT_PHDR
			 * aux entry. Static binaries don't usually include
			 * a PT_PHDR entry.
			 */
			if (phdr[i].p_offset == 0 &&
			    hdr->e_phoff + hdr->e_phnum * hdr->e_phentsize
				<= phdr[i].p_filesz)
				proghdr = phdr[i].p_vaddr + hdr->e_phoff +
				    et_dyn_addr;

			seg_addr = trunc_page(phdr[i].p_vaddr + et_dyn_addr);
			seg_size = round_page(phdr[i].p_memsz +
			    phdr[i].p_vaddr + et_dyn_addr - seg_addr);

			/*
			 * Is this .text or .data?  We can't use
			 * VM_PROT_WRITE or VM_PROT_EXEC, it breaks the
			 * alpha terribly and possibly does other bad
			 * things so we stick to the old way of figuring
			 * it out:  If the segment contains the program
			 * entry point, it's a text segment, otherwise it
			 * is a data segment.
			 *
			 * Note that obreak() assumes that data_addr + 
			 * data_size == end of data load area, and the ELF
			 * file format expects segments to be sorted by
			 * address.  If multiple data segments exist, the
			 * last one will be used.
			 */
			if (hdr->e_entry >= phdr[i].p_vaddr &&
			    hdr->e_entry < (phdr[i].p_vaddr +
			    phdr[i].p_memsz)) {
				text_size = seg_size;
				text_addr = seg_addr;
				entry = (u_long)hdr->e_entry + et_dyn_addr;
			} else {
				data_size = seg_size;
				data_addr = seg_addr;
			}
			total_size += seg_size;

			/*
			 * Check limits.  It should be safe to check the
			 * limits after loading the segment since we do
			 * not actually fault in all the segment's pages.
			 */
			if (data_size >
			    imgp->proc->p_rlimit[RLIMIT_DATA].rlim_cur ||
			    text_size > maxtsiz ||
			    total_size >
			    imgp->proc->p_rlimit[RLIMIT_VMEM].rlim_cur) {
				if (interp != NULL)
					kfree(interp, M_TEMP);
				error = ENOMEM;
				return (error);
			}
			break;
		case PT_PHDR: 	/* Program header table info */
			proghdr = phdr[i].p_vaddr + et_dyn_addr;
			break;
		default:
			break;
		}
	}

	vmspace->vm_tsize = text_size >> PAGE_SHIFT;
	vmspace->vm_taddr = (caddr_t)(uintptr_t)text_addr;
	vmspace->vm_dsize = data_size >> PAGE_SHIFT;
	vmspace->vm_daddr = (caddr_t)(uintptr_t)data_addr;

	addr = ELF_RTLD_ADDR(vmspace);

	imgp->entry_addr = entry;

	imgp->proc->p_sysent = brand_info->sysvec;
	EVENTHANDLER_INVOKE(process_exec, imgp);

	if (interp != NULL) {
		int have_interp = FALSE;
		if (brand_info->emul_path != NULL &&
		    brand_info->emul_path[0] != '\0') {
			path = kmalloc(MAXPATHLEN, M_TEMP, M_WAITOK);
		        ksnprintf(path, MAXPATHLEN, "%s%s",
			    brand_info->emul_path, interp);
			error = __elfN(load_file)(imgp->proc, path, &addr,
			    &imgp->entry_addr);
			kfree(path, M_TEMP);
			if (error == 0)
				have_interp = TRUE;
		}
		if (!have_interp && newinterp != NULL) {
			error = __elfN(load_file)(imgp->proc, newinterp,
			    &addr, &imgp->entry_addr);
			if (error == 0)
				have_interp = TRUE;
		}
		if (!have_interp) {
			error = __elfN(load_file)(imgp->proc, interp, &addr,
			    &imgp->entry_addr);
		}
		if (error != 0) {
			uprintf("ELF interpreter %s not found\n", interp);
			kfree(interp, M_TEMP);
			return (error);
		}
		kfree(interp, M_TEMP);
	} else
		addr = et_dyn_addr;

	/*
	 * Construct auxargs table (used by the fixup routine)
	 */
	elf_auxargs = kmalloc(sizeof(Elf_Auxargs), M_TEMP, M_WAITOK);
	elf_auxargs->execfd = -1;
	elf_auxargs->phdr = proghdr;
	elf_auxargs->phent = hdr->e_phentsize;
	elf_auxargs->phnum = hdr->e_phnum;
	elf_auxargs->pagesz = PAGE_SIZE;
	elf_auxargs->base = addr;
	elf_auxargs->flags = 0;
	elf_auxargs->entry = entry;

	imgp->auxargs = elf_auxargs;
	imgp->interpreted = 0;
	imgp->proc->p_osrel = osrel;

	return (error);
}

int
__elfN(dragonfly_fixup)(register_t **stack_base, struct image_params *imgp)
{
	Elf_Auxargs *args = (Elf_Auxargs *)imgp->auxargs;
	Elf_Addr *base;
	Elf_Addr *pos;

	base = (Elf_Addr *)*stack_base;
	pos = base + (imgp->args->argc + imgp->args->envc + 2);

	if (args->execfd != -1)
		AUXARGS_ENTRY(pos, AT_EXECFD, args->execfd);
	AUXARGS_ENTRY(pos, AT_PHDR, args->phdr);
	AUXARGS_ENTRY(pos, AT_PHENT, args->phent);
	AUXARGS_ENTRY(pos, AT_PHNUM, args->phnum);
	AUXARGS_ENTRY(pos, AT_PAGESZ, args->pagesz);
	AUXARGS_ENTRY(pos, AT_FLAGS, args->flags);
	AUXARGS_ENTRY(pos, AT_ENTRY, args->entry);
	AUXARGS_ENTRY(pos, AT_BASE, args->base);
	if (imgp->execpathp != 0)
		AUXARGS_ENTRY(pos, AT_EXECPATH, imgp->execpathp);
	AUXARGS_ENTRY(pos, AT_OSRELDATE, osreldate);
	AUXARGS_ENTRY(pos, AT_NULL, 0);

	kfree(imgp->auxargs, M_TEMP);
	imgp->auxargs = NULL;

	base--;
	suword(base, (long)imgp->args->argc);
	*stack_base = (register_t *)base;
	return (0);
}

/*
 * Code for generating ELF core dumps.
 */

typedef int (*segment_callback)(vm_map_entry_t, void *);

/* Closure for cb_put_phdr(). */
struct phdr_closure {
	Elf_Phdr *phdr;		/* Program header to fill in (incremented) */
	Elf_Phdr *phdr_max;	/* Pointer bound for error check */
	Elf_Off offset;		/* Offset of segment in core file */
};

/* Closure for cb_size_segment(). */
struct sseg_closure {
	int count;		/* Count of writable segments. */
	size_t vsize;		/* Total size of all writable segments. */
};

/* Closure for cb_put_fp(). */
struct fp_closure {
	struct vn_hdr *vnh;
	struct vn_hdr *vnh_max;
	int count;
	struct stat *sb;
};

typedef struct elf_buf {
	char	*buf;
	size_t	off;
	size_t	off_max;
} *elf_buf_t;

static void *target_reserve(elf_buf_t target, size_t bytes, int *error);

static int cb_put_phdr (vm_map_entry_t, void *);
static int cb_size_segment (vm_map_entry_t, void *);
static int cb_fpcount_segment(vm_map_entry_t, void *);
static int cb_put_fp(vm_map_entry_t, void *);


static int each_segment (struct proc *, segment_callback, void *, int);
static int __elfN(corehdr)(struct lwp *, int, struct file *, struct ucred *,
			int, elf_buf_t);
enum putmode { WRITE, DRYRUN };
static int __elfN(puthdr)(struct lwp *, elf_buf_t, int sig, enum putmode,
			int, struct file *);
static int elf_putallnotes(struct lwp *, elf_buf_t, int, enum putmode);
static int __elfN(putnote)(elf_buf_t, const char *, int, const void *, size_t);

static int elf_putsigs(struct lwp *, elf_buf_t);
static int elf_puttextvp(struct proc *, elf_buf_t);
static int elf_putfiles(struct proc *, elf_buf_t, struct file *);

int
__elfN(coredump)(struct lwp *lp, int sig, struct vnode *vp, off_t limit)
{
	struct file *fp; 
	int error;

	if ((error = falloc(NULL, &fp, NULL)) != 0)
		return (error);
	fsetcred(fp, lp->lwp_proc->p_ucred);

	/*
	 * XXX fixme.
	 */
	fp->f_type = DTYPE_VNODE;
	fp->f_flag = O_CREAT|O_WRONLY|O_NOFOLLOW;
	fp->f_ops = &vnode_fileops;
	fp->f_data = vp;
	
	error = generic_elf_coredump(lp, sig, fp, limit);

	fp->f_type = 0;
	fp->f_flag = 0;
	fp->f_ops = &badfileops;
	fp->f_data = NULL;
	fdrop(fp);
	return (error);
}

int
generic_elf_coredump(struct lwp *lp, int sig, struct file *fp, off_t limit)
{
	struct proc *p = lp->lwp_proc;
	struct ucred *cred = p->p_ucred;
	int error = 0;
	struct sseg_closure seginfo;
	struct elf_buf target;

	if (!fp)
		kprintf("can't dump core - null fp\n");

	/*
	 * Size the program segments
	 */
	seginfo.count = 0;
	seginfo.vsize = 0;
	each_segment(p, cb_size_segment, &seginfo, 1);

	/*
	 * Calculate the size of the core file header area by making
	 * a dry run of generating it.  Nothing is written, but the
	 * size is calculated.
	 */
	bzero(&target, sizeof(target));
	__elfN(puthdr)(lp, &target, sig, DRYRUN, seginfo.count, fp);

	if (target.off + seginfo.vsize >= limit)
		return (EFAULT);

	/*
	 * Allocate memory for building the header, fill it up,
	 * and write it out.
	 */
	target.off_max = target.off;
	target.off = 0;
	target.buf = kmalloc(target.off_max, M_TEMP, M_WAITOK|M_ZERO);

	error = __elfN(corehdr)(lp, sig, fp, cred, seginfo.count, &target);

	/* Write the contents of all of the writable segments. */
	if (error == 0) {
		Elf_Phdr *php;
		int i;
		ssize_t nbytes;

		php = (Elf_Phdr *)(target.buf + sizeof(Elf_Ehdr)) + 1;
		for (i = 0; i < seginfo.count; i++) {
			error = fp_write(fp, (caddr_t)php->p_vaddr,
					php->p_filesz, &nbytes, UIO_USERSPACE);
			if (error != 0)
				break;
			php++;
		}
	}
	kfree(target.buf, M_TEMP);
	
	return (error);
}

/*
 * A callback for each_segment() to write out the segment's
 * program header entry.
 */
static int
cb_put_phdr(vm_map_entry_t entry, void *closure)
{
	struct phdr_closure *phc = closure;
	Elf_Phdr *phdr = phc->phdr;

	if (phc->phdr == phc->phdr_max)
		return (EINVAL);

	phc->offset = round_page(phc->offset);

	phdr->p_type = PT_LOAD;
	phdr->p_offset = phc->offset;
	phdr->p_vaddr = entry->start;
	phdr->p_paddr = 0;
	phdr->p_filesz = phdr->p_memsz = entry->end - entry->start;
	phdr->p_align = PAGE_SIZE;
	phdr->p_flags = __elfN(untrans_prot)(entry->protection);

	phc->offset += phdr->p_filesz;
	++phc->phdr;
	return (0);
}

/*
 * A callback for each_writable_segment() to gather information about
 * the number of segments and their total size.
 */
static int
cb_size_segment(vm_map_entry_t entry, void *closure)
{
	struct sseg_closure *ssc = closure;

	++ssc->count;
	ssc->vsize += entry->end - entry->start;
	return (0);
}

/*
 * A callback for each_segment() to gather information about
 * the number of text segments.
 */
static int
cb_fpcount_segment(vm_map_entry_t entry, void *closure)
{
	int *count = closure;
	struct vnode *vp;

	if (entry->object.vm_object->type == OBJT_VNODE) {
		vp = (struct vnode *)entry->object.vm_object->handle;
		if ((vp->v_flag & VCKPT) && curproc->p_textvp == vp)
			return (0);
		++*count;
	}
	return (0);
}

static int
cb_put_fp(vm_map_entry_t entry, void *closure) 
{
	struct fp_closure *fpc = closure;
	struct vn_hdr *vnh = fpc->vnh;
	Elf_Phdr *phdr = &vnh->vnh_phdr;
	struct vnode *vp;
	int error;

	/*
	 * If an entry represents a vnode then write out a file handle.
	 *
	 * If we are checkpointing a checkpoint-restored program we do
	 * NOT record the filehandle for the old checkpoint vnode (which
	 * is mapped all over the place).  Instead we rely on the fact
	 * that a checkpoint-restored program does not mmap() the checkpt
	 * vnode NOCORE, so its contents will be written out to the
	 * new checkpoint file.  This is necessary because the 'old'
	 * checkpoint file is typically destroyed when a new one is created
	 * and thus cannot be used to restore the new checkpoint.
	 *
	 * Theoretically we could create a chain of checkpoint files and
	 * operate the checkpointing operation kinda like an incremental
	 * checkpoint, but a checkpoint restore would then likely wind up
	 * referencing many prior checkpoint files and that is a bit over
	 * the top for the purpose of the checkpoint API.
	 */
	if (entry->object.vm_object->type == OBJT_VNODE) {
		vp = (struct vnode *)entry->object.vm_object->handle;
		if ((vp->v_flag & VCKPT) && curproc->p_textvp == vp)
			return (0);
		if (vnh == fpc->vnh_max)
			return (EINVAL);

		if (vp->v_mount)
			vnh->vnh_fh.fh_fsid = vp->v_mount->mnt_stat.f_fsid;
		error = VFS_VPTOFH(vp, &vnh->vnh_fh.fh_fid);
		if (error) {
			char *freepath, *fullpath;

			if (vn_fullpath(curproc, vp, &fullpath, &freepath, 0)) {
				kprintf("Warning: coredump, error %d: cannot store file handle for vnode %p\n", error, vp);
			} else {
				kprintf("Warning: coredump, error %d: cannot store file handle for %s\n", error, fullpath);
				kfree(freepath, M_TEMP);
			}
			error = 0;
		}

		phdr->p_type = PT_LOAD;
		phdr->p_offset = 0;        /* not written to core */
		phdr->p_vaddr = entry->start;
		phdr->p_paddr = 0;
		phdr->p_filesz = phdr->p_memsz = entry->end - entry->start;
		phdr->p_align = PAGE_SIZE;
		phdr->p_flags = 0;
		if (entry->protection & VM_PROT_READ)
			phdr->p_flags |= PF_R;
		if (entry->protection & VM_PROT_WRITE)
			phdr->p_flags |= PF_W;
		if (entry->protection & VM_PROT_EXECUTE)
			phdr->p_flags |= PF_X;
		++fpc->vnh;
		++fpc->count;
	}
	return (0);
}

/*
 * For each writable segment in the process's memory map, call the given
 * function with a pointer to the map entry and some arbitrary
 * caller-supplied data.
 */
static int
each_segment(struct proc *p, segment_callback func, void *closure, int writable)
{
	int error = 0;
	vm_map_t map = &p->p_vmspace->vm_map;
	vm_map_entry_t entry;

	for (entry = map->header.next; error == 0 && entry != &map->header;
	    entry = entry->next) {
		vm_object_t obj;
		vm_object_t lobj;
		vm_object_t tobj;

		/*
		 * Don't dump inaccessible mappings, deal with legacy
		 * coredump mode.
		 *
		 * Note that read-only segments related to the elf binary
		 * are marked MAP_ENTRY_NOCOREDUMP now so we no longer
		 * need to arbitrarily ignore such segments.
		 */
		if (elf_legacy_coredump) {
			if (writable && (entry->protection & VM_PROT_RW) != VM_PROT_RW)
				continue;
		} else {
			if (writable && (entry->protection & VM_PROT_ALL) == 0)
				continue;
		}

		/*
		 * Dont include memory segment in the coredump if
		 * MAP_NOCORE is set in mmap(2) or MADV_NOCORE in
		 * madvise(2).
		 *
		 * Currently we only dump normal VM object maps.  We do
		 * not dump submaps or virtual page tables.
		 */
		if (writable && (entry->eflags & MAP_ENTRY_NOCOREDUMP))
			continue;
		if (entry->maptype != VM_MAPTYPE_NORMAL)
			continue;
		if ((obj = entry->object.vm_object) == NULL)
			continue;

		/*
		 * Find the bottom-most object, leaving the base object
		 * and the bottom-most object held (but only one hold
		 * if they happen to be the same).
		 */
		vm_object_hold_shared(obj);

		lobj = obj;
		while (lobj && (tobj = lobj->backing_object) != NULL) {
			KKASSERT(tobj != obj);
			vm_object_hold_shared(tobj);
			if (tobj == lobj->backing_object) {
				if (lobj != obj) {
					vm_object_lock_swap();
					vm_object_drop(lobj);
				}
				lobj = tobj;
			} else {
				vm_object_drop(tobj);
			}
		}

		/*
		 * The callback only applies to default, swap, or vnode
		 * objects.  Other types of objects such as memory-mapped
		 * devices are ignored.
		 */
		if (lobj->type == OBJT_DEFAULT || lobj->type == OBJT_SWAP ||
		    lobj->type == OBJT_VNODE) {
			error = (*func)(entry, closure);
		}
		if (lobj != obj)
			vm_object_drop(lobj);
		vm_object_drop(obj);
	}
	return (error);
}

static
void *
target_reserve(elf_buf_t target, size_t bytes, int *error)
{
    void *res = NULL;

    if (target->buf) {
	    if (target->off + bytes > target->off_max)
		    *error = EINVAL;
	    else
		    res = target->buf + target->off;
    }
    target->off += bytes;
    return (res);
}

/*
 * Write the core file header to the file, including padding up to
 * the page boundary.
 */
static int
__elfN(corehdr)(struct lwp *lp, int sig, struct file *fp, struct ucred *cred,
	    int numsegs, elf_buf_t target)
{
	int error;
	ssize_t nbytes;

	/*
	 * Fill in the header.  The fp is passed so we can detect and flag
	 * a checkpoint file pointer within the core file itself, because
	 * it may not be restored from the same file handle.
	 */
	error = __elfN(puthdr)(lp, target, sig, WRITE, numsegs, fp);

	/* Write it to the core file. */
	if (error == 0) {
		error = fp_write(fp, target->buf, target->off, &nbytes,
				 UIO_SYSSPACE);
	}
	return (error);
}

static int
__elfN(puthdr)(struct lwp *lp, elf_buf_t target, int sig, enum putmode mode,
    int numsegs, struct file *fp)
{
	struct proc *p = lp->lwp_proc;
	int error = 0;
	size_t phoff;
	size_t noteoff;
	size_t notesz;
	Elf_Ehdr *ehdr;
	Elf_Phdr *phdr;

	ehdr = target_reserve(target, sizeof(Elf_Ehdr), &error);

	phoff = target->off;
	phdr = target_reserve(target, (numsegs + 1) * sizeof(Elf_Phdr), &error);

	noteoff = target->off;
	if (error == 0)
		elf_putallnotes(lp, target, sig, mode);
	notesz = target->off - noteoff;

	/*
	 * put extra cruft for dumping process state here 
	 *  - we really want it be before all the program 
	 *    mappings
	 *  - we just need to update the offset accordingly
	 *    and GDB will be none the wiser.
	 */
	if (error == 0)
		error = elf_puttextvp(p, target);
	if (error == 0)
		error = elf_putsigs(lp, target);
	if (error == 0)
		error = elf_putfiles(p, target, fp);

	/*
	 * Align up to a page boundary for the program segments.  The
	 * actual data will be written to the outptu file, not to elf_buf_t,
	 * so we do not have to do any further bounds checking.
	 */
	target->off = round_page(target->off);
	if (error == 0 && ehdr != NULL) {
		/*
		 * Fill in the ELF header.
		 */
		ehdr->e_ident[EI_MAG0] = ELFMAG0;
		ehdr->e_ident[EI_MAG1] = ELFMAG1;
		ehdr->e_ident[EI_MAG2] = ELFMAG2;
		ehdr->e_ident[EI_MAG3] = ELFMAG3;
		ehdr->e_ident[EI_CLASS] = ELF_CLASS;
		ehdr->e_ident[EI_DATA] = ELF_DATA;
		ehdr->e_ident[EI_VERSION] = EV_CURRENT;
		ehdr->e_ident[EI_OSABI] = ELFOSABI_NONE;
		ehdr->e_ident[EI_ABIVERSION] = 0;
		ehdr->e_ident[EI_PAD] = 0;
		ehdr->e_type = ET_CORE;
		ehdr->e_machine = ELF_ARCH;
		ehdr->e_version = EV_CURRENT;
		ehdr->e_entry = 0;
		ehdr->e_phoff = phoff;
		ehdr->e_flags = 0;
		ehdr->e_ehsize = sizeof(Elf_Ehdr);
		ehdr->e_phentsize = sizeof(Elf_Phdr);
		ehdr->e_phnum = numsegs + 1;
		ehdr->e_shentsize = sizeof(Elf_Shdr);
		ehdr->e_shnum = 0;
		ehdr->e_shstrndx = SHN_UNDEF;
	}
	if (error == 0 && phdr != NULL) {
		/*
		 * Fill in the program header entries.
		 */
		struct phdr_closure phc;

		/* The note segement. */
		phdr->p_type = PT_NOTE;
		phdr->p_offset = noteoff;
		phdr->p_vaddr = 0;
		phdr->p_paddr = 0;
		phdr->p_filesz = notesz;
		phdr->p_memsz = 0;
		phdr->p_flags = 0;
		phdr->p_align = 0;
		++phdr;

		/* All the writable segments from the program. */
		phc.phdr = phdr;
		phc.phdr_max = phdr + numsegs;
		phc.offset = target->off;
		each_segment(p, cb_put_phdr, &phc, 1);
	}
	return (error);
}

/*
 * Append core dump notes to target ELF buffer or simply update target size
 * if dryrun selected.
 */
static int
elf_putallnotes(struct lwp *corelp, elf_buf_t target, int sig,
    enum putmode mode)
{
	struct proc *p = corelp->lwp_proc;
	int error;
	struct {
		prstatus_t status;
		prfpregset_t fpregs;
		prpsinfo_t psinfo;
	} *tmpdata;
	prstatus_t *status;
	prfpregset_t *fpregs;
	prpsinfo_t *psinfo;
	struct lwp *lp;

	/*
	 * Allocate temporary storage for notes on heap to avoid stack overflow.
	 */
	if (mode != DRYRUN) {
		tmpdata = kmalloc(sizeof(*tmpdata), M_TEMP, M_ZERO | M_WAITOK);
		status = &tmpdata->status;
		fpregs = &tmpdata->fpregs;
		psinfo = &tmpdata->psinfo;
	} else {
		tmpdata = NULL;
		status = NULL;
		fpregs = NULL;
		psinfo = NULL;
	}

	/*
	 * Append LWP-agnostic note.
	 */
	if (mode != DRYRUN) {
		psinfo->pr_version = PRPSINFO_VERSION;
		psinfo->pr_psinfosz = sizeof(prpsinfo_t);
		strlcpy(psinfo->pr_fname, p->p_comm,
			sizeof(psinfo->pr_fname));
		/*
		 * XXX - We don't fill in the command line arguments
		 * properly yet.
		 */
		strlcpy(psinfo->pr_psargs, p->p_comm,
			sizeof(psinfo->pr_psargs));
	}
	error =
	    __elfN(putnote)(target, "CORE", NT_PRPSINFO, psinfo, sizeof *psinfo);
	if (error)
		goto exit;

	/*
	 * Append first note for LWP that triggered core so that it is
	 * the selected one when the debugger starts.
	 */
	if (mode != DRYRUN) {
		status->pr_version = PRSTATUS_VERSION;
		status->pr_statussz = sizeof(prstatus_t);
		status->pr_gregsetsz = sizeof(gregset_t);
		status->pr_fpregsetsz = sizeof(fpregset_t);
		status->pr_osreldate = osreldate;
		status->pr_cursig = sig;
		/*
		 * XXX GDB needs unique pr_pid for each LWP and does not
		 * not support pr_pid==0 but lwp_tid can be 0, so hack unique
		 * value.
		 */
		status->pr_pid = corelp->lwp_tid;
		fill_regs(corelp, &status->pr_reg);
		fill_fpregs(corelp, fpregs);
	}
	error =
	    __elfN(putnote)(target, "CORE", NT_PRSTATUS, status, sizeof *status);
	if (error)
		goto exit;
	error =
	    __elfN(putnote)(target, "CORE", NT_FPREGSET, fpregs, sizeof *fpregs);
	if (error)
		goto exit;

	/*
	 * Then append notes for other LWPs.
	 */
	FOREACH_LWP_IN_PROC(lp, p) {
		if (lp == corelp)
			continue;
		/* skip lwps being created */
		if (lp->lwp_thread == NULL)
			continue;
		if (mode != DRYRUN) {
			status->pr_pid = lp->lwp_tid;
			fill_regs(lp, &status->pr_reg);
			fill_fpregs(lp, fpregs);
		}
		error = __elfN(putnote)(target, "CORE", NT_PRSTATUS,
					status, sizeof *status);
		if (error)
			goto exit;
		error = __elfN(putnote)(target, "CORE", NT_FPREGSET,
					fpregs, sizeof *fpregs);
		if (error)
			goto exit;
	}

exit:
	if (tmpdata != NULL)
		kfree(tmpdata, M_TEMP);
	return (error);
}

/*
 * Generate a note sub-structure.
 *
 * NOTE: 4-byte alignment.
 */
static int
__elfN(putnote)(elf_buf_t target, const char *name, int type,
	    const void *desc, size_t descsz)
{
	int error = 0;
	char *dst;
	Elf_Note note;

	note.n_namesz = strlen(name) + 1;
	note.n_descsz = descsz;
	note.n_type = type;
	dst = target_reserve(target, sizeof(note), &error);
	if (dst != NULL)
		bcopy(&note, dst, sizeof note);
	dst = target_reserve(target, note.n_namesz, &error);
	if (dst != NULL)
		bcopy(name, dst, note.n_namesz);
	target->off = roundup2(target->off, sizeof(Elf_Word));
	dst = target_reserve(target, note.n_descsz, &error);
	if (dst != NULL)
		bcopy(desc, dst, note.n_descsz);
	target->off = roundup2(target->off, sizeof(Elf_Word));
	return (error);
}


static int
elf_putsigs(struct lwp *lp, elf_buf_t target)
{
	/* XXX lwp handle more than one lwp */
	struct proc *p = lp->lwp_proc;
	int error = 0;
	struct ckpt_siginfo *csi;

	csi = target_reserve(target, sizeof(struct ckpt_siginfo), &error);
	if (csi) {
		csi->csi_ckptpisz = sizeof(struct ckpt_siginfo);
		bcopy(p->p_sigacts, &csi->csi_sigacts, sizeof(*p->p_sigacts));
		bcopy(&p->p_realtimer, &csi->csi_itimerval, sizeof(struct itimerval));
		bcopy(&lp->lwp_sigmask, &csi->csi_sigmask,
			sizeof(sigset_t));
		csi->csi_sigparent = p->p_sigparent;
	}
	return (error);
}

static int
elf_putfiles(struct proc *p, elf_buf_t target, struct file *ckfp)
{
	int error = 0;
	int i;
	struct ckpt_filehdr *cfh = NULL;
	struct ckpt_fileinfo *cfi;
	struct file *fp;	
	struct vnode *vp;
	/*
	 * the duplicated loop is gross, but it was the only way
	 * to eliminate uninitialized variable warnings 
	 */
	cfh = target_reserve(target, sizeof(struct ckpt_filehdr), &error);
	if (cfh) {
		cfh->cfh_nfiles = 0;		
	}

	/*
	 * ignore STDIN/STDERR/STDOUT.
	 */
	for (i = 3; error == 0 && i < p->p_fd->fd_nfiles; i++) {
		fp = holdfp(p->p_fd, i, -1);
		if (fp == NULL)
			continue;
		/* 
		 * XXX Only checkpoint vnodes for now.
		 */
		if (fp->f_type != DTYPE_VNODE) {
			fdrop(fp);
			continue;
		}
		cfi = target_reserve(target, sizeof(struct ckpt_fileinfo),
					&error);
		if (cfi == NULL) {
			fdrop(fp);
			continue;
		}
		cfi->cfi_index = -1;
		cfi->cfi_type = fp->f_type;
		cfi->cfi_flags = fp->f_flag;
		cfi->cfi_offset = fp->f_offset;
		cfi->cfi_ckflags = 0;

		if (fp == ckfp)
			cfi->cfi_ckflags |= CKFIF_ISCKPTFD;
		/* f_count and f_msgcount should not be saved/restored */
		/* XXX save cred info */

		switch(fp->f_type) {
		case DTYPE_VNODE:
			vp = (struct vnode *)fp->f_data;
			/*
			 * it looks like a bug in ptrace is marking 
			 * a non-vnode as a vnode - until we find the 
			 * root cause this will at least prevent
			 * further panics from truss
			 */
			if (vp == NULL || vp->v_mount == NULL)
				break;
			cfh->cfh_nfiles++;
			cfi->cfi_index = i;
			cfi->cfi_fh.fh_fsid = vp->v_mount->mnt_stat.f_fsid;
			error = VFS_VPTOFH(vp, &cfi->cfi_fh.fh_fid);
			break;
		default:
			break;
		}
		fdrop(fp);
	}
	return (error);
}

static int
elf_puttextvp(struct proc *p, elf_buf_t target)
{
	int error = 0;
	int *vn_count;
	struct fp_closure fpc;
	struct ckpt_vminfo *vminfo;

	vminfo = target_reserve(target, sizeof(struct ckpt_vminfo), &error);
	if (vminfo != NULL) {
		vminfo->cvm_dsize = p->p_vmspace->vm_dsize;
		vminfo->cvm_tsize = p->p_vmspace->vm_tsize;
		vminfo->cvm_daddr = p->p_vmspace->vm_daddr;
		vminfo->cvm_taddr = p->p_vmspace->vm_taddr;
	}

	fpc.count = 0;
	vn_count = target_reserve(target, sizeof(int), &error);
	if (target->buf != NULL) {
		fpc.vnh = (struct vn_hdr *)(target->buf + target->off);
		fpc.vnh_max = fpc.vnh + 
			(target->off_max - target->off) / sizeof(struct vn_hdr);
		error = each_segment(p, cb_put_fp, &fpc, 0);
		if (vn_count)
			*vn_count = fpc.count;
	} else {
		error = each_segment(p, cb_fpcount_segment, &fpc.count, 0);
	}
	target->off += fpc.count * sizeof(struct vn_hdr);
	return (error);
}

/*
 * Try to find the appropriate ABI-note section for checknote,
 * The entire image is searched if necessary, not only the first page.
 */
static boolean_t
__elfN(check_note)(struct image_params *imgp, Elf_Brandnote *checknote,
    int32_t *osrel)
{
	boolean_t valid_note_found;
	const Elf_Phdr *phdr, *pnote;
	const Elf_Ehdr *hdr;
	int i;

	valid_note_found = FALSE;
	hdr = (const Elf_Ehdr *)imgp->image_header;
	phdr = (const Elf_Phdr *)(imgp->image_header + hdr->e_phoff);

	for (i = 0; i < hdr->e_phnum; i++) {
		if (phdr[i].p_type == PT_NOTE) {
			pnote = &phdr[i];
			valid_note_found = check_PT_NOTE (imgp, checknote,
				osrel, pnote);
			if (valid_note_found)
				break;
		}
	}
	return valid_note_found;
}

/*
 * Be careful not to create new overflow conditions when checking
 * for overflow.
 */
static boolean_t
note_overflow(const Elf_Note *note, size_t maxsize)
{
	if (sizeof(*note) > maxsize)
		return TRUE;
	if (note->n_namesz > maxsize - sizeof(*note))
		return TRUE;
	return FALSE;
}

static boolean_t
hdr_overflow(__ElfN(Off) off_beg, __ElfN(Size) size)
{
	__ElfN(Off) off_end;

	off_end = off_beg + size;
	if (off_end < off_beg)
		return TRUE;
	return FALSE;
}

static boolean_t
check_PT_NOTE(struct image_params *imgp, Elf_Brandnote *checknote,
	      int32_t *osrel, const Elf_Phdr * pnote)
{
	boolean_t limited_to_first_page;
	boolean_t found = FALSE;
	const Elf_Note *note, *note0, *note_end;
	const char *note_name;
	__ElfN(Off) noteloc, firstloc;
	__ElfN(Size) notesz, firstlen, endbyte;
	struct lwbuf *lwb;
	struct lwbuf lwb_cache;
	const char *page;
	char *data = NULL;
	int n;

	if (hdr_overflow(pnote->p_offset, pnote->p_filesz))
		return (FALSE);
	notesz = pnote->p_filesz;
	noteloc = pnote->p_offset;
	endbyte = noteloc + notesz;
	limited_to_first_page = noteloc < PAGE_SIZE && endbyte < PAGE_SIZE;

	if (limited_to_first_page) {
		note = (const Elf_Note *)(imgp->image_header + noteloc);
		note_end = (const Elf_Note *)(imgp->image_header + endbyte);
		note0 = note;
	} else {
		firstloc = noteloc & PAGE_MASK;
		firstlen = PAGE_SIZE - firstloc;
		if (notesz < sizeof(Elf_Note) || notesz > PAGE_SIZE)
			return (FALSE);

		lwb = &lwb_cache;
		if (exec_map_page(imgp, noteloc >> PAGE_SHIFT, &lwb, &page))
			return (FALSE);
		if (firstlen < notesz) {         /* crosses page boundary */
			data = kmalloc(notesz, M_TEMP, M_WAITOK);
			bcopy(page + firstloc, data, firstlen);

			exec_unmap_page(lwb);
			lwb = &lwb_cache;
			if (exec_map_page(imgp, (noteloc >> PAGE_SHIFT) + 1,
				&lwb, &page)) {
				kfree(data, M_TEMP);
				return (FALSE);
			}
			bcopy(page, data + firstlen, notesz - firstlen);
			note = note0 = (const Elf_Note *)(data);
			note_end = (const Elf_Note *)(data + notesz);
		} else {
			note = note0 = (const Elf_Note *)(page + firstloc);
			note_end = (const Elf_Note *)(page + firstloc +
				firstlen);
		}
	}

	for (n = 0; n < 100 && note >= note0 && note < note_end; n++) {
		if (!aligned(note, Elf32_Addr))
			break;
		if (note_overflow(note, (const char *)note_end -
					(const char *)note)) {
			break;
		}
		note_name = (const char *)(note + 1);

		if (note->n_namesz == checknote->hdr.n_namesz
		    && note->n_descsz == checknote->hdr.n_descsz
		    && note->n_type == checknote->hdr.n_type
		    && (strncmp(checknote->vendor, note_name,
			checknote->hdr.n_namesz) == 0)) {
			/* Fetch osreldata from ABI.note-tag */
			if ((checknote->flags & BN_TRANSLATE_OSREL) != 0 &&
			    checknote->trans_osrel != NULL)
				checknote->trans_osrel(note, osrel);
			found = TRUE;
			break;
		}
		note = (const Elf_Note *)((const char *)(note + 1) +
		    roundup2(note->n_namesz, sizeof(Elf32_Addr)) +
		    roundup2(note->n_descsz, sizeof(Elf32_Addr)));
	}

	if (!limited_to_first_page) {
		if (data != NULL)
			kfree(data, M_TEMP);
		exec_unmap_page(lwb);
	}
	return (found);
}

/*
 * The interpreter program header may be located beyond the first page, so
 * regardless of its location, a copy of the interpreter path is created so
 * that it may be safely referenced by the calling function in all case.  The
 * memory is allocated by calling function, and the copying is done here.
 */
static boolean_t
extract_interpreter(struct image_params *imgp, const Elf_Phdr *pinterpreter,
		    char *data)
{
	boolean_t limited_to_first_page;
	const boolean_t result_success = FALSE;
	const boolean_t result_failure = TRUE;
	__ElfN(Off) pathloc, firstloc;
	__ElfN(Size) pathsz, firstlen, endbyte;
	struct lwbuf *lwb;
	struct lwbuf lwb_cache;
	const char *page;

	if (hdr_overflow(pinterpreter->p_offset, pinterpreter->p_filesz))
		return (result_failure);
	pathsz  = pinterpreter->p_filesz;
	pathloc = pinterpreter->p_offset;
	endbyte = pathloc + pathsz;

	limited_to_first_page = pathloc < PAGE_SIZE && endbyte < PAGE_SIZE;
	if (limited_to_first_page) {
	        bcopy(imgp->image_header + pathloc, data, pathsz);
	        return (result_success);
	}

	firstloc = pathloc & PAGE_MASK;
	firstlen = PAGE_SIZE - firstloc;

	lwb = &lwb_cache;
	if (exec_map_page(imgp, pathloc >> PAGE_SHIFT, &lwb, &page))
		return (result_failure);

	if (firstlen < pathsz) {         /* crosses page boundary */
		bcopy(page + firstloc, data, firstlen);

		exec_unmap_page(lwb);
		lwb = &lwb_cache;
		if (exec_map_page(imgp, (pathloc >> PAGE_SHIFT) + 1, &lwb,
			&page))
			return (result_failure);
		bcopy(page, data + firstlen, pathsz - firstlen);
	} else
		bcopy(page + firstloc, data, pathsz);

	exec_unmap_page(lwb);
	return (result_success);
}

static boolean_t
__elfN(bsd_trans_osrel)(const Elf_Note *note, int32_t *osrel)
{
	uintptr_t p;

	p = (uintptr_t)(note + 1);
	p += roundup2(note->n_namesz, sizeof(Elf32_Addr));
	*osrel = *(const int32_t *)(p);

	return (TRUE);
}

/*
 * Tell kern_execve.c about it, with a little help from the linker.
 */
#if defined(__x86_64__)
static struct execsw elf_execsw = {exec_elf64_imgact, "ELF64"};
EXEC_SET_ORDERED(elf64, elf_execsw, SI_ORDER_FIRST);
#else /* i386 assumed */
static struct execsw elf_execsw = {exec_elf32_imgact, "ELF32"};
EXEC_SET_ORDERED(elf32, elf_execsw, SI_ORDER_FIRST);
#endif

static vm_prot_t
__elfN(trans_prot)(Elf_Word flags)
{
	vm_prot_t prot;

	prot = 0;
	if (flags & PF_X)
		prot |= VM_PROT_EXECUTE;
	if (flags & PF_W)
		prot |= VM_PROT_WRITE;
	if (flags & PF_R)
		prot |= VM_PROT_READ;
	return (prot);
}

static Elf_Word
__elfN(untrans_prot)(vm_prot_t prot)
{
	Elf_Word flags;

	flags = 0;
	if (prot & VM_PROT_EXECUTE)
		flags |= PF_X;
	if (prot & VM_PROT_READ)
		flags |= PF_R;
	if (prot & VM_PROT_WRITE)
		flags |= PF_W;
	return (flags);
}
