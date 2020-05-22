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
 * $FreeBSD: src/sys/sys/linker.h,v 1.17.2.1 2002/03/11 01:13:53 dd Exp $
 */

#ifndef _SYS_LINKER_H_
#define _SYS_LINKER_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif

#ifdef _KERNEL

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#include <machine/elf.h>

#include <sys/module.h>

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_LINKER);
#endif

/*
 * Object representing a file which has been loaded by the linker.
 */
typedef struct linker_file* linker_file_t;
typedef TAILQ_HEAD(, linker_file) linker_file_list_t;

typedef caddr_t linker_sym_t;		/* opaque symbol */
typedef c_caddr_t c_linker_sym_t;	/* const opaque symbol */

/*
 * expanded out linker_sym_t
 */
typedef struct linker_symval {
    const char*		name;
    caddr_t		value;
    size_t		size;
} linker_symval_t;

struct linker_file_ops {
    /*
     * Lookup a symbol in the file's symbol table.  If the symbol is
     * not found then return ENOENT, otherwise zero.  If the symbol
     * found is a common symbol, return with *address set to zero and
     * *size set to the size of the common space required.  Otherwise
     * set *address the value of the symbol.
     */
    int			(*lookup_symbol)(linker_file_t, const char* name,
					 c_linker_sym_t* sym);

    int			(*symbol_values)(linker_file_t, c_linker_sym_t,
					 linker_symval_t*);

    int			(*search_symbol)(linker_file_t, caddr_t value,
					 c_linker_sym_t* sym, long* diffp);

    /*
     * Completes the loading task started in preload_file.
     */
    int			(*preload_finish)(linker_file_t);

    /*
     * Unload a file, releasing dependancies and freeing storage.
     */
    void		(*unload)(linker_file_t);

    int			(*lookup_set)(linker_file_t, const char *name,
					void ***firstp, void ***lastp, int *countp);
};

struct common_symbol {
    STAILQ_ENTRY(common_symbol) link;
    char*		name;
    caddr_t		address;
};

struct linker_file {
    int			refs;		/* reference count */
    int			userrefs;	/* kldload(2) count */
    int			flags;
#define LINKER_FILE_LINKED	0x1	/* file has been fully linked */
    TAILQ_ENTRY(linker_file) link;	/* list of all loaded files */
    char*		filename;	/* file which was loaded */
    int			id;		/* unique id */
    caddr_t		address;	/* load address */
    size_t		size;		/* size of file */
    int			ndeps;		/* number of dependancies */
    linker_file_t*	deps;		/* list of dependancies */
    STAILQ_HEAD(, common_symbol) common; /* list of common symbols */
    TAILQ_HEAD(, module) modules;	/* modules in this file */
    TAILQ_ENTRY(linker_file) loaded;	/* preload dependency support */
    void*		priv;		/* implementation data */

    struct linker_file_ops* ops;
};

/*
 * Object implementing a class of file (a.out, elf, etc.)
 */
typedef struct linker_class *linker_class_t;
typedef TAILQ_HEAD(, linker_class) linker_class_list_t;

struct linker_class_ops {
    /* 
     * Load a file, returning the new linker_file_t in *result.  If
     * the class does not recognise the file type, zero should be
     * returned, without modifying *result.  If the file is
     * recognised, the file should be loaded, *result set to the new
     * file and zero returned.  If some other error is detected an
     * appropriate errno should be returned.
     */
    int		(*load_file)(const char *filename, linker_file_t *result);
    /*
     * Same as load_file, but does not load dependencies.  Necessary
     * for tentatively loading (loader-)preloaded modules.
     */
    int		(*preload_file)(const char *filename, linker_file_t *result);
};

struct linker_class {
    TAILQ_ENTRY(linker_class) link;	/* list of all file classes */
    const char*		desc;		/* description (e.g. "a.out") */
    void*		priv;		/* implementation data */

    struct linker_class_ops *ops;
};

/*
 * Function type used when iterating over the list of linker files.
 */
typedef int linker_predicate_t(linker_file_t, void *);

/*
 * The file which is currently loading.  Used to register modules with
 * the files which contain them.
 */
extern linker_file_t	linker_current_file;

/*
 * The "file" for the kernel.
 */
extern linker_file_t	linker_kernel_file;

/*
 * Add a new file class to the linker.
 */
int linker_add_class(const char* _desc, void* _priv,
		     struct linker_class_ops* _ops);

/*
 * Load a file, trying each file class until one succeeds.
 */
int linker_load_file(const char *_filename, linker_file_t *_result);

/*
 * Find a currently loaded file given its filename.
 */
linker_file_t linker_find_file_by_name(const char* _filename);

/*
 * Find a currently loaded file given its file id.
 */
linker_file_t linker_find_file_by_id(int _fileid);

/*
 * Called from a class handler when a file is laoded.
 */
linker_file_t linker_make_file(const char* _filename, void* _priv,
			       struct linker_file_ops* _ops);

/*
 * Unload a file, freeing up memory.
 */
int linker_file_unload(linker_file_t _file);

/*
 * Add a dependancy to a file.
 */
void linker_file_add_dependancy(linker_file_t _file, linker_file_t _dep);

int linker_load_dependencies(linker_file_t lf);

/*
 * Iterate over all of the currently loaded linker files calling the
 * predicate function while the function returns 0.  Returns the value
 * returned by the last predicate function.
 */
int linker_file_foreach(linker_predicate_t *_predicate, void *_context);

/*
 * Lookup a symbol in a file.  If deps is TRUE, look in dependancies
 * if not found in file.  The symbol's value is returned in the
 * caddr_t.  An error is returned, 0 if no error occured.
 */
int linker_file_lookup_symbol(linker_file_t _file, const char* _name, 
			    int _deps, caddr_t *);


/*
 * Lookup a linker set in a file.  Return pointers to the first entry,
 * last + 1, and count of entries.  Use: for (p = start; p < stop; p++) {}
 * void *start is really: "struct yoursetmember ***start;"
 */
int linker_file_lookup_set(linker_file_t _file, const char *_name,
			    void *_start, void *_stop, int *_count);

/*
 * Search the linker path for the module.  Return the full pathname in
 * a malloc'ed buffer.
 */
char *linker_search_path(const char *_filename);

int linker_reference_module(const char *modname, struct mod_depend *verinfo,
    linker_file_t *retult);
int linker_release_module(const char *modname, struct mod_depend *verinfo,
    linker_file_t lf);

/*
 * DDB Helpers, tuned specifically for ddb/db_kld.c
 */
int linker_ddb_lookup(const char *_symstr, c_linker_sym_t *_sym);
int linker_ddb_search_symbol(caddr_t _value, c_linker_sym_t *_sym,
			     long *_diffp);
int linker_ddb_symbol_values(c_linker_sym_t _sym, linker_symval_t *_symval);


#endif	/* _KERNEL */

/*
 * Module information subtypes
 */
#define MODINFO_END		0x0000		/* End of list */
#define MODINFO_NAME		0x0001		/* Name of module (string) */
#define MODINFO_TYPE		0x0002		/* Type of module (string) */
#define MODINFO_ADDR		0x0003		/* Loaded address */
#define MODINFO_SIZE		0x0004		/* Size of module */
#define MODINFO_EMPTY		0x0005		/* Has been deleted */
#define MODINFO_ARGS		0x0006		/* Parameters string */
#define MODINFO_METADATA	0x8000		/* Module-specfic */

#define MODINFOMD_AOUTEXEC	0x0001		/* a.out exec header */
#define MODINFOMD_ELFHDR	0x0002		/* ELF header */
#define MODINFOMD_SSYM		0x0003		/* start of symbols */
#define MODINFOMD_ESYM		0x0004		/* end of symbols */
#define MODINFOMD_DYNAMIC	0x0005		/* _DYNAMIC pointer */
#define MODINFOMD_ENVP          0x0006          /* (from 5.x) envp[] */
#define MODINFOMD_HOWTO         0x0007          /* (from 5.x) boothowto */
#define MODINFOMD_KERNEND       0x0008          /* (from 5.x) kernend */
#define MODINFOMD_SHDR		0x0009		/* section header table */
#define MODINFOMD_FW_HANDLE	0x000a		/* Firmware dependent handle */
#define MODINFOMD_NOCOPY	0x8000		/* don't copy this metadata to the kernel */

#define MODINFOMD_DEPLIST	(0x4001 | MODINFOMD_NOCOPY)	/* depends on */

#ifdef _KERNEL
#define MD_FETCH(mdp, info, type) ({ \
	type *__p; \
	__p = (type *)preload_search_info((mdp), MODINFO_METADATA | (info)); \
	__p ? *__p : 0; \
})
#endif

#define LINKER_HINTS_VERSION	1		/* linker.hints file version */

#ifdef _KERNEL

/*
 * Module lookup
 */
extern caddr_t		preload_metadata;
caddr_t		preload_search_by_name(const char *);
caddr_t		preload_search_by_type(const char *);
caddr_t		preload_search_next_name(caddr_t);
caddr_t		preload_search_info(caddr_t, int);
void		preload_delete_name(const char *);
void		preload_bootstrap_relocate(vm_offset_t);
extern struct mod_metadata *find_mod_metadata(const char *);

#ifdef KLD_DEBUG

extern int kld_debug;
#define KLD_DEBUG_FILE	1	/* file load/unload */
#define KLD_DEBUG_SYM	2	/* symbol lookup */

#define KLD_DPF(cat, args)					\
	do {							\
		if (kld_debug & KLD_DEBUG_##cat) kprintf args;	\
	} while (0)

#else

#define KLD_DPF(cat, args)

#endif

typedef int elf_lookup_fn(linker_file_t, Elf_Size, int, Elf_Addr *);

/* Support functions */
int	elf_reloc(linker_file_t, Elf_Addr, const void *, int, elf_lookup_fn);
int	elf_reloc_local(linker_file_t, Elf_Addr, const void *, int, elf_lookup_fn);
Elf_Addr elf_relocaddr(linker_file_t, Elf_Addr);

/* values for type */
#define ELF_RELOC_REL	1
#define ELF_RELOC_RELA	2

#endif /* _KERNEL */

struct kld_file_stat {
    int		version;	/* set to sizeof(linker_file_stat) */
    char        name[MAXPATHLEN];
    int		refs;
    int		id;
    caddr_t	address;	/* load address */
    size_t	size;		/* size in bytes */
};

struct kld_sym_lookup {
    int		version;	/* set to sizeof(struct kld_sym_lookup) */
    const char	*symname;	/* Symbol name we are looking up */
    u_long	symvalue;
    size_t	symsize;
};

#define KLDSYM_LOOKUP	1

#ifndef _KERNEL

#ifndef _SYS_CDEFS_H_
#include <sys/cdefs.h>
#endif

__BEGIN_DECLS
int	kldload(const char *);
int	kldunload(int);
int	kldfind(const char *);
int	kldnext(int);
int	kldstat(int, struct kld_file_stat *);
int	kldfirstmod(int);
int	kldsym(int, int, void *);
__END_DECLS

#endif

#endif /* !_SYS_LINKER_H_ */
