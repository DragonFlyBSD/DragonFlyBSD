/*-
 * Copyright (c) 1997 Doug Rabson
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
 * $FreeBSD: src/sys/kern/kern_linker.c,v 1.41.2.3 2001/11/21 17:50:35 luigi Exp $
 */

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysproto.h>
#include <sys/sysent.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/linker.h>
#include <sys/fcntl.h>
#include <sys/libkern.h>
#include <sys/nlookup.h>
#include <sys/vnode.h>
#include <sys/sysctl.h>

#include <vm/vm_zone.h>

#include <sys/mplock2.h>

#ifdef _KERNEL_VIRTUAL
#include <dlfcn.h>
#endif

#ifdef KLD_DEBUG
int kld_debug = 1;
#endif

/* Metadata from the static kernel */
SET_DECLARE(modmetadata_set, struct mod_metadata);
MALLOC_DEFINE(M_LINKER, "kld", "kernel linker");

linker_file_t linker_current_file;
linker_file_t linker_kernel_file;

static struct lock lock;	/* lock for the file list */
static linker_class_list_t classes;
static linker_file_list_t linker_files;
static int next_file_id = 1;

/* XXX wrong name; we're looking at version provision tags here, not modules */
typedef TAILQ_HEAD(, modlist) modlisthead_t;
struct modlist {
	TAILQ_ENTRY(modlist) link;	/* chain together all modules */
	linker_file_t   container;
	const char 	*name;
	int             version;
};
typedef struct modlist *modlist_t;
static modlisthead_t found_modules;


static int linker_load_module(const char *kldname, const char *modname,
			      struct linker_file *parent, struct mod_depend *verinfo,
			      struct linker_file **lfpp);

static char *
linker_strdup(const char *str)
{
    char	*result;

    result = kmalloc(strlen(str) + 1, M_LINKER, M_WAITOK);
    strcpy(result, str);
    return(result);
}

static void
linker_init(void* arg)
{
    lockinit(&lock, "klink", 0, 0);
    TAILQ_INIT(&classes);
    TAILQ_INIT(&linker_files);
}

SYSINIT(linker, SI_BOOT2_KLD, SI_ORDER_FIRST, linker_init, 0);

int
linker_add_class(const char* desc, void* priv,
		 struct linker_class_ops* ops)
{
    linker_class_t lc;

    lc = kmalloc(sizeof(struct linker_class), M_LINKER, M_NOWAIT | M_ZERO);
    if (!lc)
	return ENOMEM;

    lc->desc = desc;
    lc->priv = priv;
    lc->ops = ops;
    TAILQ_INSERT_HEAD(&classes, lc, link);

    return 0;
}

static void
linker_file_sysinit(linker_file_t lf)
{
    struct sysinit** start, ** stop;
    struct sysinit** sipp;
    struct sysinit** xipp;
    struct sysinit* save;

    KLD_DPF(FILE, ("linker_file_sysinit: calling SYSINITs for %s\n",
		   lf->filename));

    if (linker_file_lookup_set(lf, "sysinit_set", &start, &stop, NULL) != 0)
	return;

    /*
     * Perform a bubble sort of the system initialization objects by
     * their subsystem (primary key) and order (secondary key).
     *
     * Since some things care about execution order, this is the
     * operation which ensures continued function.
     */
    for (sipp = start; sipp < stop; sipp++) {
	for (xipp = sipp + 1; xipp < stop; xipp++) {
	    if ((*sipp)->subsystem < (*xipp)->subsystem ||
		 ((*sipp)->subsystem == (*xipp)->subsystem &&
		  (*sipp)->order <= (*xipp)->order))
		continue;	/* skip*/
	    save = *sipp;
	    *sipp = *xipp;
	    *xipp = save;
	}
    }


    /*
     * Traverse the (now) ordered list of system initialization tasks.
     * Perform each task, and continue on to the next task.
     */
    for (sipp = start; sipp < stop; sipp++) {
	if ((*sipp)->subsystem == SI_SPECIAL_DUMMY)
	    continue;	/* skip dummy task(s)*/

	/* Call function */
	(*((*sipp)->func))((*sipp)->udata);
    }
}

static void
linker_file_sysuninit(linker_file_t lf)
{
    struct sysinit** start, ** stop;
    struct sysinit** sipp;
    struct sysinit** xipp;
    struct sysinit* save;

    KLD_DPF(FILE, ("linker_file_sysuninit: calling SYSUNINITs for %s\n",
		   lf->filename));

    if (linker_file_lookup_set(lf, "sysuninit_set", &start, &stop, NULL) != 0)
	return;

    /*
     * Perform a reverse bubble sort of the system initialization objects
     * by their subsystem (primary key) and order (secondary key).
     *
     * Since some things care about execution order, this is the
     * operation which ensures continued function.
     */
    for (sipp = start; sipp < stop; sipp++) {
	for (xipp = sipp + 1; xipp < stop; xipp++) {
	    if ((*sipp)->subsystem > (*xipp)->subsystem ||
		 ((*sipp)->subsystem == (*xipp)->subsystem &&
		  (*sipp)->order >= (*xipp)->order))
		continue;	/* skip*/
	    save = *sipp;
	    *sipp = *xipp;
	    *xipp = save;
	}
    }


    /*
     * Traverse the (now) ordered list of system initialization tasks.
     * Perform each task, and continue on to the next task.
     */
    for (sipp = start; sipp < stop; sipp++) {
	if ((*sipp)->subsystem == SI_SPECIAL_DUMMY)
	    continue;	/* skip dummy task(s)*/

	/* Call function */
	(*((*sipp)->func))((*sipp)->udata);
    }
}

static void
linker_file_register_sysctls(linker_file_t lf)
{
    struct sysctl_oid **start, **stop, **oidp;

    KLD_DPF(FILE, ("linker_file_register_sysctls: registering SYSCTLs for %s\n",
		   lf->filename));

    if (linker_file_lookup_set(lf, "sysctl_set", &start, &stop, NULL) != 0)
	return;
    for (oidp = start; oidp < stop; oidp++)
	sysctl_register_oid(*oidp);
}

static void
linker_file_unregister_sysctls(linker_file_t lf)
{
    struct sysctl_oid **start, **stop, **oidp;

    KLD_DPF(FILE, ("linker_file_unregister_sysctls: registering SYSCTLs for %s\n",
		   lf->filename));

    if (linker_file_lookup_set(lf, "sysctl_set", &start, &stop, NULL) != 0)
	return;
    for (oidp = start; oidp < stop; oidp++)
	sysctl_unregister_oid(*oidp);
}

static int
linker_file_register_modules(linker_file_t lf)
{
    struct mod_metadata **start, **stop, **mdp;
    const moduledata_t *moddata;
    int		    first_error, error;

    KLD_DPF(FILE, ("linker_file_register_modules: registering modules in %s\n",
		   lf->filename));

    if (linker_file_lookup_set(lf, "modmetadata_set", &start, &stop, NULL) != 0) {
	/*
	 * This fallback should be unnecessary, but if we get booted
	 * from boot2 instead of loader and we are missing our
	 * metadata then we have to try the best we can.
	 */
	if (lf == linker_kernel_file) {
	    start = SET_BEGIN(modmetadata_set);
	    stop = SET_LIMIT(modmetadata_set);
	} else
	    return (0);
    }
    first_error = 0;
    for (mdp = start; mdp < stop; mdp++) {
	if ((*mdp)->md_type != MDT_MODULE)
	    continue;
	moddata = (*mdp)->md_data;
	KLD_DPF(FILE, ("Registering module %s in %s\n", moddata->name, lf->filename));
	error = module_register(moddata, lf);
	if (error) {
	    kprintf("Module %s failed to register: %d\n", moddata->name, error);
	    if (first_error == 0)
		first_error = error;
	}
    }
    return (first_error);
}

static void
linker_init_kernel_modules(void)
{

    linker_file_register_modules(linker_kernel_file);
}

SYSINIT(linker_kernel, SI_BOOT2_KLD, SI_ORDER_ANY, linker_init_kernel_modules, 0);

int
linker_load_file(const char *filename, linker_file_t *result)
{
    linker_class_t lc;
    linker_file_t lf;
    int foundfile, error = 0;

    /* Refuse to load modules if securelevel raised */
    if (securelevel > 0 || kernel_mem_readonly)
	return EPERM; 

    lf = linker_find_file_by_name(filename);
    if (lf) {
	KLD_DPF(FILE, ("linker_load_file: file %s is already loaded, incrementing refs\n", filename));
	*result = lf;
	lf->refs++;
	goto out;
    }

    lf = NULL;
    foundfile = 0;
    TAILQ_FOREACH(lc, &classes, link) {
	KLD_DPF(FILE, ("linker_load_file: trying to load %s as %s\n",
		       filename, lc->desc));

	error = lc->ops->load_file(filename, &lf);
	/*
	 * If we got something other than ENOENT, then it exists but we cannot
	 * load it for some other reason.
	 */
	if (error != ENOENT)
	    foundfile = 1;
	if (lf) {
	    error = linker_file_register_modules(lf);
	    if (error == EEXIST) {
		    linker_file_unload(lf /* , LINKER_UNLOAD_FORCE */);
		    return (error);
	    }
	    linker_file_register_sysctls(lf);
	    linker_file_sysinit(lf);
	    lf->flags |= LINKER_FILE_LINKED;
	    *result = lf;
	    return (0);
	}
    }
    /*
     * Less than ideal, but tells the user whether it failed to load or
     * the module was not found.
     */
    if (foundfile) {
	    /*
	     * If the file type has not been recognized by the last try
	     * printout a message before to fail.
	     */
	    if (error == ENOSYS)
		    kprintf("linker_load_file: Unsupported file type\n");

	    /*
	     * Format not recognized or otherwise unloadable.
	     * When loading a module that is statically built into
	     * the kernel EEXIST percolates back up as the return
	     * value.  Preserve this so that apps can recognize this
	     * special case.
	     */
	    if (error != EEXIST)
		    error = ENOEXEC;
    } else {
	error = ENOENT;		/* Nothing found */
    }

out:
    return error;
}


linker_file_t
linker_find_file_by_name(const char* filename)
{
    linker_file_t lf = NULL;
    char *koname;
    int i;

    for (i = strlen(filename); i > 0 && filename[i-1] != '/'; --i)
	;
    filename += i;

    koname = kmalloc(strlen(filename) + 4, M_LINKER, M_WAITOK);
    ksprintf(koname, "%s.ko", filename);

    lockmgr(&lock, LK_SHARED);
    TAILQ_FOREACH(lf, &linker_files, link) {
	if (!strcmp(lf->filename, koname))
	    break;
	if (!strcmp(lf->filename, filename))
	    break;
    }
    lockmgr(&lock, LK_RELEASE);

    if (koname)
	kfree(koname, M_LINKER);
    return lf;
}

linker_file_t
linker_find_file_by_id(int fileid)
{
    linker_file_t lf = NULL;

    lockmgr(&lock, LK_SHARED);
    TAILQ_FOREACH(lf, &linker_files, link)
	if (lf->id == fileid)
	    break;
    lockmgr(&lock, LK_RELEASE);

    return lf;
}

int
linker_file_foreach(linker_predicate_t *predicate, void *context)
{
    linker_file_t lf;
    int retval = 0;

    lockmgr(&lock, LK_SHARED);
    TAILQ_FOREACH(lf, &linker_files, link) {
	retval = predicate(lf, context);
	if (retval != 0)
	    break;
    }
    lockmgr(&lock, LK_RELEASE);
    return (retval);
}

linker_file_t
linker_make_file(const char* pathname, void* priv, struct linker_file_ops* ops)
{
    linker_file_t lf = NULL;
    const char *filename;

    filename = rindex(pathname, '/');
    if (filename && filename[1])
	filename++;
    else
	filename = pathname;

    KLD_DPF(FILE, ("linker_make_file: new file, filename=%s\n", filename));
    lockmgr(&lock, LK_EXCLUSIVE);
    lf = kmalloc(sizeof(struct linker_file), M_LINKER, M_WAITOK | M_ZERO);
    lf->refs = 1;
    lf->userrefs = 0;
    lf->flags = 0;
    lf->filename = linker_strdup(filename);
    lf->id = next_file_id++;
    lf->ndeps = 0;
    lf->deps = NULL;
    STAILQ_INIT(&lf->common);
    TAILQ_INIT(&lf->modules);

    lf->priv = priv;
    lf->ops = ops;
    TAILQ_INSERT_TAIL(&linker_files, lf, link);

    lockmgr(&lock, LK_RELEASE);
    return lf;
}

int
linker_file_unload(linker_file_t file)
{
    module_t mod, next;
    modlist_t ml, nextml;
    struct common_symbol* cp;
    int error = 0;
    int i;

    /* Refuse to unload modules if securelevel raised */
    if (securelevel > 0 || kernel_mem_readonly)
	return EPERM; 

    KLD_DPF(FILE, ("linker_file_unload: lf->refs=%d\n", file->refs));

    lockmgr(&lock, LK_EXCLUSIVE);

    /* Easy case of just dropping a reference. */
    if (file->refs > 1) {
	    file->refs--;
	    lockmgr(&lock, LK_RELEASE);
	    return (0);
    }

    KLD_DPF(FILE, ("linker_file_unload: file is unloading, informing modules\n"));

    /*
     * Inform any modules associated with this file.
     */
    mod = TAILQ_FIRST(&file->modules);
    for (mod = TAILQ_FIRST(&file->modules); mod; mod = next) {
	next = module_getfnext(mod);

	/*
	 * Give the module a chance to veto the unload.  Note that the
	 * act of unloading the module may cause other modules in the
	 * same file list to be unloaded recursively.
	 */
	if ((error = module_unload(mod)) != 0) {
	    KLD_DPF(FILE, ("linker_file_unload: module %p vetoes unload\n",
			   mod));
	    lockmgr(&lock, LK_RELEASE);
	    file->refs--;
	    goto out;
	}
	module_release(mod);
    }

    TAILQ_FOREACH_MUTABLE(ml, &found_modules, link, nextml) {
	if (ml->container == file) {
	    TAILQ_REMOVE(&found_modules, ml, link);
	    kfree(ml, M_LINKER);
	}
    }

    /* Don't try to run SYSUNINITs if we are unloaded due to a link error */
    if (file->flags & LINKER_FILE_LINKED) {
	file->flags &= ~LINKER_FILE_LINKED;
	lockmgr(&lock, LK_RELEASE);
	linker_file_sysuninit(file);
	linker_file_unregister_sysctls(file);
	lockmgr(&lock, LK_EXCLUSIVE);
    }

    TAILQ_REMOVE(&linker_files, file, link);

    if (file->deps) {
	lockmgr(&lock, LK_RELEASE);
	for (i = 0; i < file->ndeps; i++)
	    linker_file_unload(file->deps[i]);
	lockmgr(&lock, LK_EXCLUSIVE);
	kfree(file->deps, M_LINKER);
	file->deps = NULL;
    }

    while ((cp = STAILQ_FIRST(&file->common)) != NULL) {
	STAILQ_REMOVE_HEAD(&file->common, link);
	kfree(cp, M_LINKER);
    }

    file->ops->unload(file);

    if (file->filename) {
	kfree(file->filename, M_LINKER);
	file->filename = NULL;
    }

    kfree(file, M_LINKER);

    lockmgr(&lock, LK_RELEASE);

out:
    return error;
}

void
linker_file_add_dependancy(linker_file_t file, linker_file_t dep)
{
    linker_file_t* newdeps;

    newdeps = kmalloc((file->ndeps + 1) * sizeof(linker_file_t*),
		     M_LINKER, M_WAITOK | M_ZERO);

    if (file->deps) {
	bcopy(file->deps, newdeps, file->ndeps * sizeof(linker_file_t*));
	kfree(file->deps, M_LINKER);
    }
    file->deps = newdeps;
    file->deps[file->ndeps] = dep;
    file->ndeps++;
}

/*
 * Locate a linker set and its contents.
 * This is a helper function to avoid linker_if.h exposure elsewhere.
 * Note: firstp and lastp are really void ***
 */
int
linker_file_lookup_set(linker_file_t file, const char *name,
                      void *firstp, void *lastp, int *countp)
{
    return file->ops->lookup_set(file, name, firstp, lastp, countp);
}

int
linker_file_lookup_symbol(linker_file_t file, const char* name, int deps, caddr_t *raddr)
{
    c_linker_sym_t sym;
    linker_symval_t symval;
    linker_file_t lf;
    size_t common_size = 0;
    int i;

    KLD_DPF(SYM, ("linker_file_lookup_symbol: file=%p, name=%s, deps=%d\n",
		  file, name, deps));

    if (file->ops->lookup_symbol(file, name, &sym) == 0) {
	file->ops->symbol_values(file, sym, &symval);

	/*
	 * XXX Assume a common symbol if its value is 0 and it has a non-zero
	 * size, otherwise it could be an absolute symbol with a value of 0.
	 */
	if (symval.value == NULL && symval.size != 0) {
	    /*
	     * For commons, first look them up in the dependancies and
	     * only allocate space if not found there.
	     */
	    common_size = symval.size;
	} else {
	    KLD_DPF(SYM, ("linker_file_lookup_symbol: symbol.value=%p\n", symval.value));
	    *raddr = symval.value;
	    return 0;
	}
    }
    if (deps) {
	for (i = 0; i < file->ndeps; i++) {
	    if (linker_file_lookup_symbol(file->deps[i], name, 0, raddr) == 0) {
		KLD_DPF(SYM, ("linker_file_lookup_symbol: deps value=%p\n", *raddr));
		return 0;
	    }
	}

	/* If we have not found it in the dependencies, search globally */
	TAILQ_FOREACH(lf, &linker_files, link) {
	    /* But skip the current file if it's on the list */
	    if (lf == file)
		continue;
	    /* And skip the files we searched above */
	    for (i = 0; i < file->ndeps; i++)
		if (lf == file->deps[i])
		    break;
	    if (i < file->ndeps)
		continue;
	    if (linker_file_lookup_symbol(lf, name, 0, raddr) == 0) {
		KLD_DPF(SYM, ("linker_file_lookup_symbol: global value=%p\n", *raddr));
		return 0;
	    }
	}
    }

    if (common_size > 0) {
	/*
	 * This is a common symbol which was not found in the
	 * dependancies.  We maintain a simple common symbol table in
	 * the file object.
	 */
	struct common_symbol* cp;

	STAILQ_FOREACH(cp, &file->common, link)
	    if (!strcmp(cp->name, name)) {
		KLD_DPF(SYM, ("linker_file_lookup_symbol: old common value=%p\n", cp->address));
		*raddr = cp->address;
		return 0;
	    }

	/*
	 * Round the symbol size up to align.
	 */
	common_size = (common_size + sizeof(int) - 1) & -sizeof(int);
	cp = kmalloc(sizeof(struct common_symbol)
		    + common_size
		    + strlen(name) + 1,
		    M_LINKER, M_WAITOK | M_ZERO);

	cp->address = (caddr_t) (cp + 1);
	cp->name = cp->address + common_size;
	strcpy(cp->name, name);
	bzero(cp->address, common_size);
	STAILQ_INSERT_TAIL(&file->common, cp, link);

	KLD_DPF(SYM, ("linker_file_lookup_symbol: new common value=%p\n", cp->address));
	*raddr = cp->address;
	return 0;
    }

#ifdef _KERNEL_VIRTUAL
    *raddr = dlsym(RTLD_NEXT, name);
    if (*raddr != NULL) {
	KLD_DPF(SYM, ("linker_file_lookup_symbol: found dlsym=%p\n", *raddr));
	return 0;
    }
#endif

    KLD_DPF(SYM, ("linker_file_lookup_symbol: fail\n"));
    return ENOENT;
}

#ifdef DDB
/*
 * DDB Helpers.  DDB has to look across multiple files with their own
 * symbol tables and string tables.
 *
 * Note that we do not obey list locking protocols here.  We really don't
 * need DDB to hang because somebody's got the lock held.  We'll take the
 * chance that the files list is inconsistant instead.
 */

int
linker_ddb_lookup(const char *symstr, c_linker_sym_t *sym)
{
    linker_file_t lf;

    TAILQ_FOREACH(lf, &linker_files, link) {
	if (lf->ops->lookup_symbol(lf, symstr, sym) == 0)
	    return 0;
    }
    return ENOENT;
}

int
linker_ddb_search_symbol(caddr_t value, c_linker_sym_t *sym, long *diffp)
{
    linker_file_t lf;
    u_long off = (uintptr_t)value;
    u_long diff, bestdiff;
    c_linker_sym_t best;
    c_linker_sym_t es;

    best = NULL;
    bestdiff = off;
    TAILQ_FOREACH(lf, &linker_files, link) {
	if (lf->ops->search_symbol(lf, value, &es, &diff) != 0)
	    continue;
	if (es != NULL && diff < bestdiff) {
	    best = es;
	    bestdiff = diff;
	}
	if (bestdiff == 0)
	    break;
    }
    if (best) {
	*sym = best;
	*diffp = bestdiff;
	return 0;
    } else {
	*sym = NULL;
	*diffp = off;
	return ENOENT;
    }
}

int
linker_ddb_symbol_values(c_linker_sym_t sym, linker_symval_t *symval)
{
    linker_file_t lf;

    TAILQ_FOREACH(lf, &linker_files, link) {
	if (lf->ops->symbol_values(lf, sym, symval) == 0)
	    return 0;
    }
    return ENOENT;
}

#endif

/*
 * Syscalls.
 *
 * MPALMOSTSAFE
 */
int
sys_kldload(struct kldload_args *uap)
{
    struct thread *td = curthread;
    char *file;
    char *kldname, *modname;
    linker_file_t lf;
    int error = 0;

    uap->sysmsg_result = -1;

    if (securelevel > 0 || kernel_mem_readonly)	/* redundant, but that's OK */
	return EPERM;

    if ((error = priv_check(td, PRIV_KLD_LOAD)) != 0)
	return error;

    file = kmalloc(MAXPATHLEN, M_TEMP, M_WAITOK);
    if ((error = copyinstr(uap->file, file, MAXPATHLEN, NULL)) != 0)
	goto out;

    /*
     * If file does not contain a qualified name or any dot in it
     * (kldname.ko, or kldname.ver.ko) treat it as an interface
     * name.
     */
    if (index(file, '/') || index(file, '.')) {
	kldname = file;
	modname = NULL;
    } else {
	kldname = NULL;
	modname = file;
    }

    get_mplock();
    error = linker_load_module(kldname, modname, NULL, NULL, &lf);
    rel_mplock();
    if (error)
	goto out;

    lf->userrefs++;
    uap->sysmsg_result = lf->id;

out:
    if (file)
	kfree(file, M_TEMP);
    return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_kldunload(struct kldunload_args *uap)
{
    struct thread *td = curthread;
    linker_file_t lf;
    int error = 0;

    if (securelevel > 0 || kernel_mem_readonly)	/* redundant, but that's OK */
	return EPERM;

    if ((error = priv_check(td, PRIV_KLD_UNLOAD)) != 0)
	return error;

    get_mplock();
    lf = linker_find_file_by_id(uap->fileid);
    if (lf) {
	KLD_DPF(FILE, ("kldunload: lf->userrefs=%d\n", lf->userrefs));
	if (lf->userrefs == 0) {
	    kprintf("linkerunload: attempt to unload file that was loaded by the kernel\n");
	    error = EBUSY;
	    goto out;
	}
	lf->userrefs--;
	error = linker_file_unload(lf);
	if (error)
	    lf->userrefs++;
    } else {
	error = ENOENT;
    }
out:
    rel_mplock();
    return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_kldfind(struct kldfind_args *uap)
{
    char *filename = NULL, *modulename;
    linker_file_t lf;
    int error;

    uap->sysmsg_result = -1;

    filename = kmalloc(MAXPATHLEN, M_TEMP, M_WAITOK);
    if ((error = copyinstr(uap->file, filename, MAXPATHLEN, NULL)) != 0)
	goto out;

    modulename = rindex(filename, '/');
    if (modulename == NULL)
	modulename = filename;

    get_mplock();
    lf = linker_find_file_by_name(modulename);
    if (lf)
	uap->sysmsg_result = lf->id;
    else
	error = ENOENT;
    rel_mplock();

out:
    if (filename)
	kfree(filename, M_TEMP);
    return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_kldnext(struct kldnext_args *uap)
{
    linker_file_t lf;
    int error = 0;

    get_mplock();
    if (uap->fileid == 0) {
	    lf = TAILQ_FIRST(&linker_files);
    } else {
	    lf = linker_find_file_by_id(uap->fileid);
	    if (lf == NULL) {
		    error = ENOENT;
		    goto out;
	    }
	    lf = TAILQ_NEXT(lf, link);
    }

    /* Skip partially loaded files. */
    while (lf != NULL && !(lf->flags & LINKER_FILE_LINKED)) {
	    lf = TAILQ_NEXT(lf, link);
    }

    if (lf)
	uap->sysmsg_result = lf->id;
    else
	uap->sysmsg_result = 0;

out:
    rel_mplock();
    return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_kldstat(struct kldstat_args *uap)
{
    linker_file_t lf;
    int error = 0;
    int version;
    struct kld_file_stat* stat;
    int namelen;

    get_mplock();
    lf = linker_find_file_by_id(uap->fileid);
    if (!lf) {
	error = ENOENT;
	goto out;
    }

    stat = uap->stat;

    /*
     * Check the version of the user's structure.
     */
    if ((error = copyin(&stat->version, &version, sizeof(version))) != 0)
	goto out;
    if (version != sizeof(struct kld_file_stat)) {
	error = EINVAL;
	goto out;
    }

    namelen = strlen(lf->filename) + 1;
    if (namelen > MAXPATHLEN)
	namelen = MAXPATHLEN;
    if ((error = copyout(lf->filename, &stat->name[0], namelen)) != 0)
	goto out;
    if ((error = copyout(&lf->refs, &stat->refs, sizeof(int))) != 0)
	goto out;
    if ((error = copyout(&lf->id, &stat->id, sizeof(int))) != 0)
	goto out;
    if ((error = copyout(&lf->address, &stat->address, sizeof(caddr_t))) != 0)
	goto out;
    if ((error = copyout(&lf->size, &stat->size, sizeof(size_t))) != 0)
	goto out;

    uap->sysmsg_result = 0;

out:
    rel_mplock();
    return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_kldfirstmod(struct kldfirstmod_args *uap)
{
    linker_file_t lf;
    int error = 0;

    get_mplock();
    lf = linker_find_file_by_id(uap->fileid);
    if (lf) {
	if (TAILQ_FIRST(&lf->modules))
	    uap->sysmsg_result = module_getid(TAILQ_FIRST(&lf->modules));
	else
	    uap->sysmsg_result = 0;
    } else {
	error = ENOENT;
    }
    rel_mplock();

    return error;
}

/*
 * MPALMOSTSAFE
 */
int
sys_kldsym(struct kldsym_args *uap)
{
    char *symstr = NULL;
    c_linker_sym_t sym;
    linker_symval_t symval;
    linker_file_t lf;
    struct kld_sym_lookup lookup;
    int error = 0;

    get_mplock();
    if ((error = copyin(uap->data, &lookup, sizeof(lookup))) != 0)
	goto out;
    if (lookup.version != sizeof(lookup) || uap->cmd != KLDSYM_LOOKUP) {
	error = EINVAL;
	goto out;
    }

    symstr = kmalloc(MAXPATHLEN, M_TEMP, M_WAITOK);
    if ((error = copyinstr(lookup.symname, symstr, MAXPATHLEN, NULL)) != 0)
	goto out;

    if (uap->fileid != 0) {
	lf = linker_find_file_by_id(uap->fileid);
	if (lf == NULL) {
	    error = ENOENT;
	    goto out;
	}
	if (lf->ops->lookup_symbol(lf, symstr, &sym) == 0 &&
	    lf->ops->symbol_values(lf, sym, &symval) == 0) {
	    lookup.symvalue = (uintptr_t)symval.value;
	    lookup.symsize = symval.size;
	    error = copyout(&lookup, uap->data, sizeof(lookup));
	} else
	    error = ENOENT;
    } else {
	TAILQ_FOREACH(lf, &linker_files, link) {
	    if (lf->ops->lookup_symbol(lf, symstr, &sym) == 0 &&
		lf->ops->symbol_values(lf, sym, &symval) == 0) {
		lookup.symvalue = (uintptr_t)symval.value;
		lookup.symsize = symval.size;
		error = copyout(&lookup, uap->data, sizeof(lookup));
		break;
	    }
	}
	if (!lf)
	    error = ENOENT;
    }
out:
    rel_mplock();
    if (symstr)
	kfree(symstr, M_TEMP);
    return error;
}

/*
 * Preloaded module support
 */

static modlist_t
modlist_lookup(const char *name, int ver)
{
    modlist_t	    mod;

    TAILQ_FOREACH(mod, &found_modules, link) {
	if (strcmp(mod->name, name) == 0 && (ver == 0 || mod->version == ver))
	    return (mod);
    }
    return (NULL);
}

static modlist_t
modlist_lookup2(const char *name, struct mod_depend *verinfo)
{
    modlist_t	    mod, bestmod;
    int		    ver;

    if (verinfo == NULL)
	return (modlist_lookup(name, 0));
    bestmod = NULL;
    TAILQ_FOREACH(mod, &found_modules, link) {
	if (strcmp(mod->name, name) != 0)
	    continue;
	ver = mod->version;
	if (ver == verinfo->md_ver_preferred)
	    return (mod);
	if (ver >= verinfo->md_ver_minimum &&
		ver <= verinfo->md_ver_maximum &&
		(bestmod == NULL || ver > bestmod->version))
	    bestmod = mod;
    }
    return (bestmod);
}

int
linker_reference_module(const char *modname, struct mod_depend *verinfo,
    linker_file_t *result)
{
    modlist_t mod;
    int error;

    lockmgr(&lock, LK_SHARED);
    if ((mod = modlist_lookup2(modname, verinfo)) != NULL) {
        *result = mod->container;
        (*result)->refs++;
        lockmgr(&lock, LK_RELEASE);
        return (0);
    }

    lockmgr(&lock, LK_RELEASE);
    get_mplock();
    error = linker_load_module(NULL, modname, NULL, verinfo, result);
    rel_mplock();
    return (error);
}

int
linker_release_module(const char *modname, struct mod_depend *verinfo,
    linker_file_t lf)
{
    modlist_t mod;
    int error;

    lockmgr(&lock, LK_SHARED);
    if (lf == NULL) {
        KASSERT(modname != NULL,
            ("linker_release_module: no file or name"));
        mod = modlist_lookup2(modname, verinfo);
        if (mod == NULL) {
            lockmgr(&lock, LK_RELEASE);
            return (ESRCH);
        }
        lf = mod->container;
    } else
        KASSERT(modname == NULL && verinfo == NULL,
            ("linker_release_module: both file and name"));
    lockmgr(&lock, LK_RELEASE);
    get_mplock();
    error = linker_file_unload(lf);
    rel_mplock();
    return (error);
}

static modlist_t
modlist_newmodule(const char *modname, int version, linker_file_t container)
{
    modlist_t	    mod;

    mod = kmalloc(sizeof(struct modlist), M_LINKER, M_NOWAIT | M_ZERO);
    if (mod == NULL)
	panic("no memory for module list");
    mod->container = container;
    mod->name = modname;
    mod->version = version;
    TAILQ_INSERT_TAIL(&found_modules, mod, link);
    return (mod);
}

static void
linker_addmodules(linker_file_t lf, struct mod_metadata **start,
		  struct mod_metadata **stop, int preload)
{
    struct mod_metadata *mp, **mdp;
    const char     *modname;
    int		    ver;

    for (mdp = start; mdp < stop; mdp++) {
	mp = *mdp;
	if (mp->md_type != MDT_VERSION)
	    continue;
	modname = mp->md_cval;
	ver = ((struct mod_version *)mp->md_data)->mv_version;
	if (modlist_lookup(modname, ver) != NULL) {
	    kprintf("module %s already present!\n", modname);
	    /* XXX what can we do? this is a build error. :-( */
	    continue;
	}
	modlist_newmodule(modname, ver, lf);
    }
}

static void
linker_preload(void* arg)
{
    caddr_t		modptr;
    const char		*modname, *nmodname;
    char		*modtype;
    linker_file_t	lf, nlf;
    linker_class_t	lc;
    int			error;
    linker_file_list_t loaded_files;
    linker_file_list_t depended_files;
    struct mod_metadata *mp, *nmp;
    struct mod_metadata **start, **stop, **mdp, **nmdp;
    struct mod_depend *verinfo;
    int nver;
    int resolves;
    modlist_t mod;
    struct sysinit	**si_start, **si_stop;

    TAILQ_INIT(&loaded_files);
    TAILQ_INIT(&depended_files);
    TAILQ_INIT(&found_modules);

    modptr = NULL;
    while ((modptr = preload_search_next_name(modptr)) != NULL) {
	modname = (char *)preload_search_info(modptr, MODINFO_NAME);
	modtype = (char *)preload_search_info(modptr, MODINFO_TYPE);
	if (modname == NULL) {
	    kprintf("Preloaded module at %p does not have a name!\n", modptr);
	    continue;
	}
	if (modtype == NULL) {
	    kprintf("Preloaded module at %p does not have a type!\n", modptr);
	    continue;
	}

	if (bootverbose)
		kprintf("Preloaded %s \"%s\" at %p.\n", modtype, modname, modptr);
	lf = NULL;
	TAILQ_FOREACH(lc, &classes, link) {
	    error = lc->ops->preload_file(modname, &lf);
	    if (!error)
		break;
	    lf = NULL;
	}
	if (lf)
		TAILQ_INSERT_TAIL(&loaded_files, lf, loaded);
    }

    /*
     * First get a list of stuff in the kernel.
     */
    if (linker_file_lookup_set(linker_kernel_file, MDT_SETNAME, &start,
			       &stop, NULL) == 0)
	linker_addmodules(linker_kernel_file, start, stop, 1);

    /*
     * This is a once-off kinky bubble sort to resolve relocation
     * dependency requirements.
     */
restart:
    TAILQ_FOREACH(lf, &loaded_files, loaded) {
	error = linker_file_lookup_set(lf, MDT_SETNAME, &start, &stop, NULL);
	/*
	 * First, look to see if we would successfully link with this
	 * stuff.
	 */
	resolves = 1;		/* unless we know otherwise */
	if (!error) {
	    for (mdp = start; mdp < stop; mdp++) {
		mp = *mdp;
		if (mp->md_type != MDT_DEPEND)
		    continue;
		modname = mp->md_cval;
		verinfo = mp->md_data;
		for (nmdp = start; nmdp < stop; nmdp++) {
		    nmp = *nmdp;
		    if (nmp->md_type != MDT_VERSION)
			continue;
		    nmodname = nmp->md_cval;
		    if (strcmp(modname, nmodname) == 0)
			break;
		}
		if (nmdp < stop)/* it's a self reference */
		    continue;

		/*
		 * ok, the module isn't here yet, we
		 * are not finished
		 */
		if (modlist_lookup2(modname, verinfo) == NULL)
		    resolves = 0;
	    }
	}
	/*
	 * OK, if we found our modules, we can link.  So, "provide"
	 * the modules inside and add it to the end of the link order
	 * list.
	 */
	if (resolves) {
	    if (!error) {
		for (mdp = start; mdp < stop; mdp++) {
		    mp = *mdp;
		    if (mp->md_type != MDT_VERSION)
			continue;
		    modname = mp->md_cval;
		    nver = ((struct mod_version *)mp->md_data)->mv_version;
		    if (modlist_lookup(modname, nver) != NULL) {
			kprintf("module %s already present!\n", modname);
			TAILQ_REMOVE(&loaded_files, lf, loaded);
			linker_file_unload(lf /* , LINKER_UNLOAD_FORCE */ );
			/* we changed tailq next ptr */
			goto restart;
		    }
		    modlist_newmodule(modname, nver, lf);
		}
	    }
	    TAILQ_REMOVE(&loaded_files, lf, loaded);
	    TAILQ_INSERT_TAIL(&depended_files, lf, loaded);
	    /*
	     * Since we provided modules, we need to restart the
	     * sort so that the previous files that depend on us
	     * have a chance. Also, we've busted the tailq next
	     * pointer with the REMOVE.
	     */
	    goto restart;
	}
    }

    /*
     * At this point, we check to see what could not be resolved..
     */
    while ((lf = TAILQ_FIRST(&loaded_files)) != NULL) {
	TAILQ_REMOVE(&loaded_files, lf, loaded);
	kprintf("KLD file %s is missing dependencies\n", lf->filename);
	linker_file_unload(lf /* , LINKER_UNLOAD_FORCE */ );
    }

    /*
     * We made it. Finish off the linking in the order we determined.
     */
    TAILQ_FOREACH_MUTABLE(lf, &depended_files, loaded, nlf) {
	if (linker_kernel_file) {
	    linker_kernel_file->refs++;
	    linker_file_add_dependancy(lf, linker_kernel_file);
	}
	lf->userrefs++;

	error = linker_file_lookup_set(lf, MDT_SETNAME, &start, &stop, NULL);
	if (!error) {
	    for (mdp = start; mdp < stop; mdp++) {
		mp = *mdp;
		if (mp->md_type != MDT_DEPEND)
		    continue;
		modname = mp->md_cval;
		verinfo = mp->md_data;
		mod = modlist_lookup2(modname, verinfo);
		/* Don't count self-dependencies */
		if (lf == mod->container)
		    continue;
		mod->container->refs++;
		linker_file_add_dependancy(lf, mod->container);
	    }
	}
	/*
	 * Now do relocation etc using the symbol search paths
	 * established by the dependencies
	 */
	error = lf->ops->preload_finish(lf);
	if (error) {
	    TAILQ_REMOVE(&depended_files, lf, loaded);
	    kprintf("KLD file %s - could not finalize loading\n",
		    lf->filename);
	    linker_file_unload(lf /* , LINKER_UNLOAD_FORCE */);
	    continue;
	}
	linker_file_register_modules(lf);
	if (linker_file_lookup_set(lf, "sysinit_set", &si_start, &si_stop, NULL) == 0)
	    sysinit_add(si_start, si_stop);
	linker_file_register_sysctls(lf);
	lf->flags |= LINKER_FILE_LINKED;
    }
    /* woohoo! we made it! */
}

SYSINIT(preload, SI_BOOT2_KLD, SI_ORDER_MIDDLE, linker_preload, 0);

/*
 * Search for a not-loaded module by name.
 *
 * Modules may be found in the following locations:
 *
 * - preloaded (result is just the module name)
 * - on disk (result is full path to module)
 *
 * If the module name is qualified in any way (contains path, etc.)
 * the we simply return a copy of it.
 *
 * The search path can be manipulated via sysctl.  Note that we use the ';'
 * character as a separator to be consistent with the bootloader.
 */

static char linker_path[MAXPATHLEN] = "/boot;/boot/modules;/;/modules";

SYSCTL_STRING(_kern, OID_AUTO, module_path, CTLFLAG_RW, linker_path,
	      sizeof(linker_path), "module load search path");
TUNABLE_STR("module_path", linker_path, sizeof(linker_path));

char *
linker_search_path(const char *name)
{
    struct nlookupdata	nd;
    char		*cp, *ep, *result;
    size_t		name_len, prefix_len;
    size_t		result_len;
    int			sep;
    int			error;
    enum vtype		type;
    const char *exts[] = { "", ".ko", NULL };
    const char **ext;

    /* qualified at all? */
    if (index(name, '/'))
	return(linker_strdup(name));

    /* traverse the linker path */
    cp = linker_path;
    name_len = strlen(name);
    for (;;) {

	/* find the end of this component */
	for (ep = cp; (*ep != 0) && (*ep != ';'); ep++)
	    ;
	prefix_len = ep - cp;
	/* if this component doesn't end with a slash, add one */
	if (ep == cp || *(ep - 1) != '/')
	    sep = 1;
	else
	    sep = 0;

	/*
	 * +2+3 : possible separator, plus terminator + possible extension.
	 */
	result = kmalloc(prefix_len + name_len + 2+3, M_LINKER, M_WAITOK);

	strncpy(result, cp, prefix_len);
	if (sep)
	    result[prefix_len++] = '/';
	strcpy(result + prefix_len, name);

	result_len = strlen(result);
	for (ext = exts; *ext != NULL; ext++) {
	    strcpy(result + result_len, *ext);

	    /*
	     * Attempt to open the file, and return the path if we succeed and it's
	     * a regular file.
	     */
	    error = nlookup_init(&nd, result, UIO_SYSSPACE, NLC_FOLLOW|NLC_LOCKVP);
	    if (error == 0)
		error = vn_open(&nd, NULL, FREAD, 0);
	    if (error == 0) {
		type = nd.nl_open_vp->v_type;
		if (type == VREG) {
		    nlookup_done(&nd);
		    return (result);
		}
	    }
	    nlookup_done(&nd);
	}

	kfree(result, M_LINKER);

	if (*ep == 0)
	    break;
	cp = ep + 1;
    }
    return(NULL);
}

/*
 * Find a file which contains given module and load it, if "parent" is not
 * NULL, register a reference to it.
 */
static int
linker_load_module(const char *kldname, const char *modname,
		   struct linker_file *parent, struct mod_depend *verinfo,
		   struct linker_file **lfpp)
{
    linker_file_t   lfdep;
    const char     *filename;
    char           *pathname;
    int		    error;

    if (modname == NULL) {
	/*
	 * We have to load KLD
	 */
	KASSERT(verinfo == NULL, ("linker_load_module: verinfo is not NULL"));
	pathname = linker_search_path(kldname);
    } else {
	if (modlist_lookup2(modname, verinfo) != NULL)
	    return (EEXIST);
	if (kldname != NULL)
	{
	    pathname = linker_strdup(kldname);
	}
	else if (rootvnode == NULL)
	    pathname = NULL;
	else
	{
	    pathname = linker_search_path(modname);
	}
#if 0
	/*
	 * Need to find a KLD with required module
	 */
	pathname = linker_search_module(modname,
					strlen(modname), verinfo);
#endif
    }
    if (pathname == NULL)
	return (ENOENT);

    /*
     * Can't load more than one file with the same basename XXX:
     * Actually it should be possible to have multiple KLDs with
     * the same basename but different path because they can
     * provide different versions of the same modules.
     */
    filename = rindex(pathname, '/');
    if (filename == NULL)
	filename = pathname;
    else
	filename++;
    if (linker_find_file_by_name(filename))
	error = EEXIST;
    else
	do {
	    error = linker_load_file(pathname, &lfdep);
	    if (error)
		break;
	    if (modname && verinfo && modlist_lookup2(modname, verinfo) == NULL) {
		linker_file_unload(lfdep /* , LINKER_UNLOAD_FORCE */ );
		error = ENOENT;
		break;
	    }
	    if (parent) {
		linker_file_add_dependancy(parent, lfdep);
	    }
	    if (lfpp)
		*lfpp = lfdep;
	} while (0);
    kfree(pathname, M_LINKER);
    return (error);
}

/*
 * This routine is responsible for finding dependencies of userland initiated
 * kldload(2)'s of files.
 */
int
linker_load_dependencies(linker_file_t lf)
{
    linker_file_t   lfdep;
    struct mod_metadata **start, **stop, **mdp, **nmdp;
    struct mod_metadata *mp, *nmp;
    struct mod_depend *verinfo;
    modlist_t	    mod;
    const char     *modname, *nmodname;
    int		    ver, error = 0, count;

    /*
     * All files are dependant on /kernel.
     */
    if (linker_kernel_file) {
	linker_kernel_file->refs++;
	linker_file_add_dependancy(lf, linker_kernel_file);
    }
    if (linker_file_lookup_set(lf, MDT_SETNAME, &start, &stop, &count) != 0)
	return (0);
    for (mdp = start; mdp < stop; mdp++) {
	mp = *mdp;
	if (mp->md_type != MDT_VERSION)
	    continue;
	modname = mp->md_cval;
	ver = ((struct mod_version *)mp->md_data)->mv_version;
	mod = modlist_lookup(modname, ver);
	if (mod != NULL) {
	    kprintf("interface %s.%d already present in the KLD '%s'!\n",
		    modname, ver, mod->container->filename);
	    return (EEXIST);
	}
    }

    for (mdp = start; mdp < stop; mdp++) {
	mp = *mdp;
	if (mp->md_type != MDT_DEPEND)
	    continue;
	modname = mp->md_cval;
	verinfo = mp->md_data;
	nmodname = NULL;
	for (nmdp = start; nmdp < stop; nmdp++) {
	    nmp = *nmdp;
	    if (nmp->md_type != MDT_VERSION)
		continue;
	    nmodname = nmp->md_cval;
	    if (strcmp(modname, nmodname) == 0)
		break;
	}
	if (nmdp < stop)	/* early exit, it's a self reference */
	    continue;
	mod = modlist_lookup2(modname, verinfo);
	if (mod) {		/* woohoo, it's loaded already */
	    lfdep = mod->container;
	    lfdep->refs++;
	    linker_file_add_dependancy(lf, lfdep);
	    continue;
	}
	error = linker_load_module(NULL, modname, lf, verinfo, NULL);
	if (error) {
	    kprintf("KLD %s: depends on %s - not available or version mismatch\n",
		    lf->filename, modname);
	    break;
	}
    }

    if (error)
	return (error);
    linker_addmodules(lf, start, stop, 0);
    return (error);
}
