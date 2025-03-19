/*-
 * Copyright 1996, 1997, 1998, 1999, 2000 John D. Polstra.
 * Copyright 2003 Alexander Kabaev <kan@FreeBSD.ORG>.
 * Copyright 2009-2012 Konstantin Belousov <kib@FreeBSD.ORG>.
 * Copyright 2012 John Marino <draco@marino.st>.
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
 * $FreeBSD$
 */

/*
 * Dynamic linker for ELF.
 *
 * John Polstra <jdp@polstra.com>.
 */

#ifndef __GNUC__
#error "GCC is needed to compile this file"
#endif

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/ktrace.h>
#include <sys/resident.h>
#include <sys/tls.h>

#include <machine/tls.h>

#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "rtld.h"
#include "libmap.h"
#include "rtld_printf.h"
#include "notes.h"

#define cpu_sfence()    __asm __volatile("" : : : "memory");

#define PATH_RTLD	"/usr/libexec/ld-elf.so.2"
#define LD_ARY_CACHE	16

/* Types. */
typedef void (*func_ptr_type)();
typedef void * (*path_enum_proc) (const char *path, size_t len, void *arg);

/*
 * Function declarations.
 */
static int __getstatictlsextra(void);
static const char *_getenv_ld(const char *id);
static void die(void) __dead2;
static void digest_dynamic1(Obj_Entry *, int, const Elf_Dyn **,
    const Elf_Dyn **, const Elf_Dyn **);
static void digest_dynamic2(Obj_Entry *, const Elf_Dyn *, const Elf_Dyn *,
    const Elf_Dyn *);
static void digest_dynamic(Obj_Entry *, int);
static Obj_Entry *digest_phdr(const Elf_Phdr *, int, caddr_t, const char *);
static void distribute_static_tls(Objlist *, RtldLockState *);
static Obj_Entry *dlcheck(void *);
static Obj_Entry *dlopen_object(const char *name, int fd, Obj_Entry *refobj,
    int lo_flags, int mode, RtldLockState *lockstate);
static Obj_Entry *do_load_object(int, const char *, char *, struct stat *, int);
static int do_search_info(const Obj_Entry *obj, int, struct dl_serinfo *);
static bool donelist_check(DoneList *, const Obj_Entry *);
static void errmsg_restore(char *);
static char *errmsg_save(void);
static void *fill_search_info(const char *, size_t, void *);
static char *find_library(const char *, const Obj_Entry *, int *);
static const char *gethints(bool);
static void init_dag(Obj_Entry *);
static void init_rtld(caddr_t, Elf_Auxinfo **);
static void initlist_add_neededs(Needed_Entry *, Objlist *);
static void initlist_add_objects(Obj_Entry *, Obj_Entry **, Objlist *);
static void linkmap_add(Obj_Entry *);
static void linkmap_delete(Obj_Entry *);
static void load_filtees(Obj_Entry *, int flags, RtldLockState *);
static void unload_filtees(Obj_Entry *);
static int load_needed_objects(Obj_Entry *, int);
static int load_preload_objects(void);
static Obj_Entry *load_object(const char *, int fd, const Obj_Entry *, int);
static void map_stacks_exec(RtldLockState *);
static Obj_Entry *obj_from_addr(const void *);
static void objlist_call_fini(Objlist *, Obj_Entry *, RtldLockState *);
static void objlist_call_init(Objlist *, RtldLockState *);
static void objlist_clear(Objlist *);
static Objlist_Entry *objlist_find(Objlist *, const Obj_Entry *);
static void objlist_init(Objlist *);
static void objlist_push_head(Objlist *, Obj_Entry *);
static void objlist_push_tail(Objlist *, Obj_Entry *);
static void objlist_put_after(Objlist *, Obj_Entry *, Obj_Entry *);
static void objlist_remove(Objlist *, Obj_Entry *);
static int parse_libdir(const char *);
static void *path_enumerate(const char *, path_enum_proc, void *);
static int relocate_object_dag(Obj_Entry *root, bool bind_now,
    Obj_Entry *rtldobj, int flags, RtldLockState *lockstate);
static int relocate_object(Obj_Entry *obj, bool bind_now, Obj_Entry *rtldobj,
    int flags, RtldLockState *lockstate);
static int relocate_objects(Obj_Entry *, bool, Obj_Entry *, int,
    RtldLockState *);
static int resolve_objects_ifunc(Obj_Entry *first, bool bind_now,
    int flags, RtldLockState *lockstate);
static int rtld_dirname(const char *, char *);
static int rtld_dirname_abs(const char *, char *);
static void *rtld_dlopen(const char *name, int fd, int mode);
static void rtld_exit(void);
static char *search_library_path(const char *, const char *);
static char *search_library_pathfds(const char *, const char *, int *);
static const void **get_program_var_addr(const char *, RtldLockState *);
static void set_program_var(const char *, const void *);
static int symlook_default(SymLook *, const Obj_Entry *refobj);
static int symlook_global(SymLook *, DoneList *);
static void symlook_init_from_req(SymLook *, const SymLook *);
static int symlook_list(SymLook *, const Objlist *, DoneList *);
static int symlook_needed(SymLook *, const Needed_Entry *, DoneList *);
static int symlook_obj1_sysv(SymLook *, const Obj_Entry *);
static int symlook_obj1_gnu(SymLook *, const Obj_Entry *);
static void trace_loaded_objects(Obj_Entry *);
static void unlink_object(Obj_Entry *);
static void unload_object(Obj_Entry *);
static void unref_dag(Obj_Entry *);
static void ref_dag(Obj_Entry *);
static char *origin_subst_one(char *, const char *, const char *, bool);
static char *origin_subst(char *, const char *);
static void preinit_main(void);
static int  rtld_verify_versions(const Objlist *);
static int  rtld_verify_object_versions(Obj_Entry *);
static void object_add_name(Obj_Entry *, const char *);
static int  object_match_name(const Obj_Entry *, const char *);
static void ld_utrace_log(int, void *, void *, size_t, int, const char *);
static void rtld_fill_dl_phdr_info(const Obj_Entry *obj,
    struct dl_phdr_info *phdr_info);
static uint_fast32_t gnu_hash (const char *);
static bool matched_symbol(SymLook *, const Obj_Entry *, Sym_Match_Result *,
    const unsigned long);

void r_debug_state(struct r_debug *, struct link_map *) __noinline;
void _r_debug_postinit(struct link_map *) __noinline;

/*
 * Data declarations.
 */
static char *error_message;	/* Message for dlerror(), or NULL */
struct r_debug r_debug;		/* for GDB; */
static bool libmap_disable;	/* Disable libmap */
static bool ld_loadfltr;	/* Immediate filters processing */
static char *libmap_override;	/* Maps to use in addition to libmap.conf */
static bool trust;		/* False for setuid and setgid programs */
static bool dangerous_ld_env;	/* True if environment variables have been
				   used to affect the libraries loaded */
static const char *ld_bind_now;	/* Environment variable for immediate binding */
static const char *ld_debug;	/* Environment variable for debugging */
static const char *ld_library_path;	/* Environment variable for search path */
static const char *ld_library_dirs;	/* Env variable for library descriptors */
static char *ld_preload;	/* Environment variable for libraries to
				   load first */
static const char *ld_elf_hints_path;	/* Env var. for alternative hints path */
static const char *ld_tracing;	/* Called from ldd to print libs */
static const char *ld_utrace;	/* Use utrace() to log events. */
static int (*rtld_functrace)(   /* Optional function call tracing hook */
	const char *caller_obj,
	const char *callee_obj,
	const char *callee_func,
	void *stack);
static const Obj_Entry *rtld_functrace_obj;	/* Object thereof */
static Obj_Entry *obj_list;	/* Head of linked list of shared objects */
static Obj_Entry **obj_tail;	/* Link field of last object in list */
static Obj_Entry **preload_tail;
static Obj_Entry *obj_main;	/* The main program shared object */
static Obj_Entry obj_rtld;	/* The dynamic linker shared object */
static unsigned int obj_count;	/* Number of objects in obj_list */
static unsigned int obj_loads;	/* Number of objects in obj_list */

static int	ld_resident;	/* Non-zero if resident */
static const char *ld_ary[LD_ARY_CACHE];
static int	ld_index;
static Objlist initlist;

static Objlist list_global =	/* Objects dlopened with RTLD_GLOBAL */
  STAILQ_HEAD_INITIALIZER(list_global);
static Objlist list_main =	/* Objects loaded at program startup */
  STAILQ_HEAD_INITIALIZER(list_main);
static Objlist list_fini =	/* Objects needing fini() calls */
  STAILQ_HEAD_INITIALIZER(list_fini);

static Elf_Sym sym_zero;	/* For resolving undefined weak refs. */
const char *__ld_sharedlib_base;

#define GDB_STATE(s,m)	r_debug.r_state = s; r_debug_state(&r_debug,m);

extern Elf_Dyn _DYNAMIC;
#pragma weak _DYNAMIC
#ifndef RTLD_IS_DYNAMIC
#define	RTLD_IS_DYNAMIC()	(&_DYNAMIC != NULL)
#endif

#ifdef ENABLE_OSRELDATE
int osreldate;
#endif

static int stack_prot = PROT_READ | PROT_WRITE | RTLD_DEFAULT_STACK_EXEC;
#if 0
static int max_stack_flags;
#endif

/*
 * Global declarations normally provided by crt1.  The dynamic linker is
 * not built with crt1, so we have to provide them ourselves.
 */
char *__progname;
char **environ;

/*
 * Used to pass argc, argv to init functions.
 */
int main_argc;
char **main_argv;

/*
 * Globals to control TLS allocation.
 */
size_t tls_last_offset;		/* Static TLS offset of last module */
size_t tls_last_size;		/* Static TLS size of last module */
size_t tls_static_space;	/* Static TLS space allocated */
int tls_dtv_generation = 1;	/* Used to detect when dtv size changes  */
int tls_max_index = 1;		/* Largest module index allocated */

/*
 * Fill in a DoneList with an allocation large enough to hold all of
 * the currently-loaded objects.  Keep this as a macro since it calls
 * alloca and we want that to occur within the scope of the caller.
 */
#define donelist_init(dlp)					\
    ((dlp)->objs = alloca(obj_count * sizeof (dlp)->objs[0]),	\
    assert((dlp)->objs != NULL),				\
    (dlp)->num_alloc = obj_count,				\
    (dlp)->num_used = 0)

#define	UTRACE_DLOPEN_START		1
#define	UTRACE_DLOPEN_STOP		2
#define	UTRACE_DLCLOSE_START		3
#define	UTRACE_DLCLOSE_STOP		4
#define	UTRACE_LOAD_OBJECT		5
#define	UTRACE_UNLOAD_OBJECT		6
#define	UTRACE_ADD_RUNDEP		7
#define	UTRACE_PRELOAD_FINISHED		8
#define	UTRACE_INIT_CALL		9
#define	UTRACE_FINI_CALL		10

struct utrace_rtld {
	char sig[4];			/* 'RTLD' */
	int event;
	void *handle;
	void *mapbase;			/* Used for 'parent' and 'init/fini' */
	size_t mapsize;
	int refcnt;			/* Used for 'mode' */
	char name[MAXPATHLEN];
};

#define	LD_UTRACE(e, h, mb, ms, r, n) do {			\
	if (ld_utrace != NULL)					\
		ld_utrace_log(e, h, mb, ms, r, n);		\
} while (0)

static void
ld_utrace_log(int event, void *handle, void *mapbase, size_t mapsize,
    int refcnt, const char *name)
{
	struct utrace_rtld ut;

	ut.sig[0] = 'R';
	ut.sig[1] = 'T';
	ut.sig[2] = 'L';
	ut.sig[3] = 'D';
	ut.event = event;
	ut.handle = handle;
	ut.mapbase = mapbase;
	ut.mapsize = mapsize;
	ut.refcnt = refcnt;
	bzero(ut.name, sizeof(ut.name));
	if (name)
		strlcpy(ut.name, name, sizeof(ut.name));
	utrace(&ut, sizeof(ut));
}

/*
 * Main entry point for dynamic linking.  The first argument is the
 * stack pointer.  The stack is expected to be laid out as described
 * in the SVR4 ABI specification, Intel 386 Processor Supplement.
 * Specifically, the stack pointer points to a word containing
 * ARGC.  Following that in the stack is a null-terminated sequence
 * of pointers to argument strings.  Then comes a null-terminated
 * sequence of pointers to environment strings.  Finally, there is a
 * sequence of "auxiliary vector" entries.
 *
 * The second argument points to a place to store the dynamic linker's
 * exit procedure pointer and the third to a place to store the main
 * program's object.
 *
 * The return value is the main program's entry point.
 */
func_ptr_type
_rtld(Elf_Addr *sp, func_ptr_type *exit_proc, Obj_Entry **objp)
{
    Elf_Auxinfo *aux_info[AT_COUNT];
    int i;
    int argc;
    char **argv;
    char **env;
    Elf_Auxinfo *aux;
    Elf_Auxinfo *auxp;
    const char *argv0;
    Objlist_Entry *entry;
    Obj_Entry *obj;
    Obj_Entry *last_interposer;

    /* marino: DO NOT MOVE THESE VARIABLES TO _rtld
             Obj_Entry **preload_tail;
             Objlist initlist;
       from global to here.  It will break the DWARF2 unwind scheme.
    */

    /*
     * On entry, the dynamic linker itself has not been relocated yet.
     * Be very careful not to reference any global data until after
     * init_rtld has returned.  It is OK to reference file-scope statics
     * and string constants, and to call static and global functions.
     */

    /* Find the auxiliary vector on the stack. */
    argc = *sp++;
    argv = (char **) sp;
    sp += argc + 1;	/* Skip over arguments and NULL terminator */
    env = (char **) sp;

    /*
     * If we aren't already resident we have to dig out some more info.
     * Note that auxinfo does not exist when we are resident.
     *
     * I'm not sure about the ld_resident check.  It seems to read zero
     * prior to relocation, which is what we want.  When running from a
     * resident copy everything will be relocated so we are definitely
     * good there.
     */
    if (ld_resident == 0)  {
	while (*sp++ != 0)	/* Skip over environment, and NULL terminator */
	    ;
	aux = (Elf_Auxinfo *) sp;

	/* Digest the auxiliary vector. */
	for (i = 0;  i < AT_COUNT;  i++)
	    aux_info[i] = NULL;
	for (auxp = aux;  auxp->a_type != AT_NULL;  auxp++) {
	    if (auxp->a_type < AT_COUNT)
		aux_info[auxp->a_type] = auxp;
	}

	/* Initialize and relocate ourselves. */
	assert(aux_info[AT_BASE] != NULL);
	init_rtld((caddr_t) aux_info[AT_BASE]->a_un.a_ptr, aux_info);
    }

    ld_index = 0;	/* don't use old env cache in case we are resident */
    __progname = obj_rtld.path;
    argv0 = argv[0] != NULL ? argv[0] : "(null)";
    environ = env;
    main_argc = argc;
    main_argv = argv;

    trust = !issetugid();

    ld_bind_now = _getenv_ld("LD_BIND_NOW");
    /*
     * If the process is tainted, then we un-set the dangerous environment
     * variables.  The process will be marked as tainted until setuid(2)
     * is called.  If any child process calls setuid(2) we do not want any
     * future processes to honor the potentially un-safe variables.
     */
    if (!trust) {
	if (   unsetenv("LD_DEBUG")
	    || unsetenv("LD_PRELOAD")
	    || unsetenv("LD_LIBRARY_PATH")
	    || unsetenv("LD_LIBRARY_PATH_FDS")
	    || unsetenv("LD_ELF_HINTS_PATH")
	    || unsetenv("LD_LIBMAP")
	    || unsetenv("LD_LIBMAP_DISABLE")
	    || unsetenv("LD_LOADFLTR")
	    || unsetenv("LD_SHAREDLIB_BASE")
	) {
		_rtld_error("environment corrupt; aborting");
		die();
	}
    }
    __ld_sharedlib_base = _getenv_ld("LD_SHAREDLIB_BASE");
    ld_debug = _getenv_ld("LD_DEBUG");
    libmap_disable = _getenv_ld("LD_LIBMAP_DISABLE") != NULL;
    libmap_override = (char *)_getenv_ld("LD_LIBMAP");
    ld_library_path = _getenv_ld("LD_LIBRARY_PATH");
    ld_library_dirs = _getenv_ld("LD_LIBRARY_PATH_FDS");
    ld_preload = (char *)_getenv_ld("LD_PRELOAD");
    ld_elf_hints_path = _getenv_ld("LD_ELF_HINTS_PATH");
    ld_loadfltr = _getenv_ld("LD_LOADFLTR") != NULL;
    dangerous_ld_env = (ld_library_path != NULL)
			|| (ld_preload != NULL)
			|| (ld_elf_hints_path != NULL)
			|| ld_loadfltr
			|| (libmap_override != NULL)
			|| libmap_disable
			;
    ld_tracing = _getenv_ld("LD_TRACE_LOADED_OBJECTS");
    ld_utrace = _getenv_ld("LD_UTRACE");

    if ((ld_elf_hints_path == NULL) || strlen(ld_elf_hints_path) == 0)
	ld_elf_hints_path = _PATH_ELF_HINTS;

    if (ld_debug != NULL && *ld_debug != '\0')
	debug = 1;
    dbg("%s is initialized, base address = %p", __progname,
	(caddr_t) aux_info[AT_BASE]->a_un.a_ptr);
    dbg("RTLD dynamic = %p", obj_rtld.dynamic);
    dbg("RTLD pltgot  = %p", obj_rtld.pltgot);

    dbg("initializing thread locks");
    lockdflt_init();

    /*
     * If we are resident we can skip work that we have already done.
     * Note that the stack is reset and there is no Elf_Auxinfo
     * when running from a resident image, and the static globals setup
     * between here and resident_skip will have already been setup.
     */
    if (ld_resident)
	goto resident_skip1;

    /*
     * Load the main program, or process its program header if it is
     * already loaded.
     */
    if (aux_info[AT_EXECFD] != NULL) {	/* Load the main program. */
	int fd = aux_info[AT_EXECFD]->a_un.a_val;
	dbg("loading main program");
	obj_main = map_object(fd, argv0, NULL);
	close(fd);
	if (obj_main == NULL)
	    die();
#if 0
	max_stack_flags = obj_main->stack_flags;
#endif
    } else {				/* Main program already loaded. */
	const Elf_Phdr *phdr;
	int phnum;
	caddr_t entry;

	dbg("processing main program's program header");
	assert(aux_info[AT_PHDR] != NULL);
	phdr = (const Elf_Phdr *) aux_info[AT_PHDR]->a_un.a_ptr;
	assert(aux_info[AT_PHNUM] != NULL);
	phnum = aux_info[AT_PHNUM]->a_un.a_val;
	assert(aux_info[AT_PHENT] != NULL);
	assert(aux_info[AT_PHENT]->a_un.a_val == sizeof(Elf_Phdr));
	assert(aux_info[AT_ENTRY] != NULL);
	entry = (caddr_t) aux_info[AT_ENTRY]->a_un.a_ptr;
	if ((obj_main = digest_phdr(phdr, phnum, entry, argv0)) == NULL)
	    die();
    }

    char buf[MAXPATHLEN];
    if (aux_info[AT_EXECPATH] != NULL) {
	char *kexecpath;

	kexecpath = aux_info[AT_EXECPATH]->a_un.a_ptr;
	dbg("AT_EXECPATH %p %s", kexecpath, kexecpath);
	if (kexecpath[0] == '/')
		obj_main->path = kexecpath;
	else if (getcwd(buf, sizeof(buf)) == NULL ||
		strlcat(buf, "/", sizeof(buf)) >= sizeof(buf) ||
		strlcat(buf, kexecpath, sizeof(buf)) >= sizeof(buf))
		obj_main->path = xstrdup(argv0);
	else
		obj_main->path = xstrdup(buf);
    } else {
	char resolved[MAXPATHLEN];
	dbg("No AT_EXECPATH");
	if (argv0[0] == '/') {
		if (realpath(argv0, resolved) != NULL)
			obj_main->path = xstrdup(resolved);
		else
			obj_main->path = xstrdup(argv0);
	} else {
		if (getcwd(buf, sizeof(buf)) != NULL
		    && strlcat(buf, "/", sizeof(buf)) < sizeof(buf)
		    && strlcat(buf, argv0, sizeof (buf)) < sizeof(buf)
		    && access(buf, R_OK) == 0
		    && realpath(buf, resolved) != NULL)
			obj_main->path = xstrdup(resolved);
		else
			obj_main->path = xstrdup(argv0);
	}
    }
    dbg("obj_main path %s", obj_main->path);
    obj_main->mainprog = true;

    if (aux_info[AT_STACKPROT] != NULL &&
      aux_info[AT_STACKPROT]->a_un.a_val != 0)
	    stack_prot = aux_info[AT_STACKPROT]->a_un.a_val;

    /*
     * Get the actual dynamic linker pathname from the executable if
     * possible.  (It should always be possible.)  That ensures that
     * gdb will find the right dynamic linker even if a non-standard
     * one is being used.
     */
    if (obj_main->interp != NULL &&
      strcmp(obj_main->interp, obj_rtld.path) != 0) {
	free(obj_rtld.path);
	obj_rtld.path = xstrdup(obj_main->interp);
        __progname = obj_rtld.path;
    }

    digest_dynamic(obj_main, 0);
    dbg("%s valid_hash_sysv %d valid_hash_gnu %d dynsymcount %d",
	obj_main->path, obj_main->valid_hash_sysv, obj_main->valid_hash_gnu,
	obj_main->dynsymcount);

    linkmap_add(obj_main);
    linkmap_add(&obj_rtld);

    /* Link the main program into the list of objects. */
    *obj_tail = obj_main;
    obj_tail = &obj_main->next;
    obj_count++;
    obj_loads++;

    /* Initialize a fake symbol for resolving undefined weak references. */
    sym_zero.st_info = ELF_ST_INFO(STB_GLOBAL, STT_NOTYPE);
    sym_zero.st_shndx = SHN_UNDEF;
    sym_zero.st_value = -(uintptr_t)obj_main->relocbase;

    if (!libmap_disable)
        libmap_disable = (bool)lm_init(libmap_override);

    dbg("loading LD_PRELOAD libraries");
    if (load_preload_objects() == -1)
	die();
    preload_tail = obj_tail;

    dbg("loading needed objects");
    if (load_needed_objects(obj_main, 0) == -1)
	die();

    /* Make a list of all objects loaded at startup. */
    last_interposer = obj_main;
    for (obj = obj_list;  obj != NULL;  obj = obj->next) {
	if (obj->z_interpose && obj != obj_main) {
	    objlist_put_after(&list_main, last_interposer, obj);
	    last_interposer = obj;
	} else {
	    objlist_push_tail(&list_main, obj);
	}
	obj->refcount++;
    }

    dbg("checking for required versions");
    if (rtld_verify_versions(&list_main) == -1 && !ld_tracing)
	die();

resident_skip1:

    if (ld_tracing) {		/* We're done */
	trace_loaded_objects(obj_main);
	exit(0);
    }

    if (ld_resident)		/* XXX clean this up! */
	goto resident_skip2;

    if (_getenv_ld("LD_DUMP_REL_PRE") != NULL) {
       dump_relocations(obj_main);
       exit (0);
    }

    /* setup TLS for main thread */
    dbg("initializing initial thread local storage");
    STAILQ_FOREACH(entry, &list_main, link) {
	/*
	 * Allocate all the initial objects out of the static TLS
	 * block even if they didn't ask for it.
	 */
	allocate_tls_offset(entry->obj);
    }

    /*
     * Calculate the size of the TLS static segment.  This is allocated
     * for every thread.  Generally make it page-aligned for efficiency,
     * but take into account the fact that the actual allocation also
     * includes room for the struct tls_tcb header.
     */
    {
	ssize_t space;
	ssize_t extra;

	extra = __getstatictlsextra();
	space = tls_last_offset + extra + sizeof(struct tls_tcb);
	space = (space + PAGE_SIZE - 1) & ~((ssize_t)PAGE_SIZE - 1);

	tls_static_space = (size_t)space - sizeof(struct tls_tcb);
    }

    /*
     * Do not try to allocate the TLS here, let libc do it itself.
     * (crt1 for the program will call _init_tls())
     */

    if (relocate_objects(obj_main,
      ld_bind_now != NULL && *ld_bind_now != '\0',
      &obj_rtld, SYMLOOK_EARLY, NULL) == -1)
	die();

    dbg("doing copy relocations");
    if (do_copy_relocations(obj_main) == -1)
	die();

resident_skip2:

    if (_getenv_ld("LD_RESIDENT_UNREGISTER_NOW")) {
	if (exec_sys_unregister(-1) < 0) {
	    dbg("exec_sys_unregister failed %d\n", errno);
	    exit(errno);
	}
	dbg("exec_sys_unregister success\n");
	exit(0);
    }

    if (_getenv_ld("LD_DUMP_REL_POST") != NULL) {
       dump_relocations(obj_main);
       exit (0);
    }

    dbg("initializing key program variables");
    set_program_var("__progname", argv[0] != NULL ? basename(argv[0]) : "");
    set_program_var("environ", env);
    set_program_var("__elf_aux_vector", aux);

    if (_getenv_ld("LD_RESIDENT_REGISTER_NOW")) {
	extern void resident_start(void);
	ld_resident = 1;
	if (exec_sys_register(resident_start) < 0) {
	    dbg("exec_sys_register failed %d\n", errno);
	    exit(errno);
	}
	dbg("exec_sys_register success\n");
	exit(0);
    }

    /* Make a list of init functions to call. */
    objlist_init(&initlist);
    initlist_add_objects(obj_list, preload_tail, &initlist);

    r_debug_state(NULL, &obj_main->linkmap); /* say hello to gdb! */

    map_stacks_exec(NULL);

    dbg("resolving ifuncs");
    {
	    RtldLockState lockstate;

	    wlock_acquire(rtld_bind_lock, &lockstate);
	    if (resolve_objects_ifunc(
		    obj_main,
		    (ld_bind_now != NULL && *ld_bind_now != '\0'),
		    SYMLOOK_EARLY,
		    &lockstate) == -1) {
		    die();
	    }
	    lock_release(rtld_bind_lock, &lockstate);
    }

    /*
     * Do NOT call the initlist here, give libc a chance to set up
     * the initial TLS segment.  crt1 will then call _rtld_call_init().
     */

    dbg("transferring control to program entry point = %p", obj_main->entry);

    /* Return the exit procedure and the program entry point. */
    *exit_proc = rtld_exit;
    *objp = obj_main;
    return (func_ptr_type) obj_main->entry;
}

/*
 * Call the initialization list for dynamically loaded libraries.
 * (called from crt1.c).
 */
void
_rtld_call_init(void)
{
    RtldLockState lockstate;
    Obj_Entry *obj;

    if (!obj_main->note_present && obj_main->valid_hash_gnu) {
	/*
	 * The use of a linker script with a PHDRS directive that does not include
	 * PT_NOTE will block the crt_no_init note.  In this case we'll look for the
	 * recently added GNU hash dynamic tag which gets built by default.  It is
	 * extremely unlikely to find a pre-3.1 binary without a PT_NOTE header and
	 * a gnu hash tag.  If gnu hash found, consider binary to use new crt code.
	 */
	obj_main->crt_no_init = true;
	dbg("Setting crt_no_init without presence of PT_NOTE header");
    }

    wlock_acquire(rtld_bind_lock, &lockstate);
    if (obj_main->crt_no_init)
	preinit_main();
    else {
	/*
	 * Make sure we don't call the main program's init and fini functions
	 * for binaries linked with old crt1 which calls _init itself.
	 */
	obj_main->init = obj_main->fini = (Elf_Addr)NULL;
	obj_main->init_array = obj_main->fini_array = (Elf_Addr)NULL;
    }
    objlist_call_init(&initlist, &lockstate);
    _r_debug_postinit(&obj_main->linkmap);
    objlist_clear(&initlist);
    dbg("loading filtees");
    for (obj = obj_list->next; obj != NULL; obj = obj->next) {
	if (ld_loadfltr || obj->z_loadfltr)
	    load_filtees(obj, 0, &lockstate);
    }
    lock_release(rtld_bind_lock, &lockstate);
}

void *
rtld_resolve_ifunc(const Obj_Entry *obj, const Elf_Sym *def)
{
	void *ptr;
	Elf_Addr target;

	ptr = (void *)make_function_pointer(def, obj);
	target = ((Elf_Addr (*)(void))ptr)();
	return ((void *)target);
}

Elf_Addr
_rtld_bind(Obj_Entry *obj, Elf_Size reloff, void *stack)
{
    const Elf_Rel *rel;
    const Elf_Sym *def;
    const Obj_Entry *defobj;
    Elf_Addr *where;
    Elf_Addr target;
    RtldLockState lockstate;

    rlock_acquire(rtld_bind_lock, &lockstate);
    if (sigsetjmp(lockstate.env, 0) != 0)
	    lock_upgrade(rtld_bind_lock, &lockstate);
    if (obj->pltrel)
	rel = (const Elf_Rel *) ((caddr_t) obj->pltrel + reloff);
    else
	rel = (const Elf_Rel *) ((caddr_t) obj->pltrela + reloff);

    where = (Elf_Addr *) (obj->relocbase + rel->r_offset);
    def = find_symdef(ELF_R_SYM(rel->r_info), obj, &defobj, true, NULL,
	&lockstate);
    if (def == NULL)
	die();
    if (ELF_ST_TYPE(def->st_info) == STT_GNU_IFUNC)
	target = (Elf_Addr)rtld_resolve_ifunc(defobj, def);
    else
	target = (Elf_Addr)(defobj->relocbase + def->st_value);

    dbg("\"%s\" in \"%s\" ==> %p in \"%s\"",
      defobj->strtab + def->st_name, basename(obj->path),
      (void *)target, basename(defobj->path));

    /*
     * If we have a function call tracing hook, and the
     * hook would like to keep tracing this one function,
     * prevent the relocation so we will wind up here
     * the next time again.
     *
     * We don't want to functrace calls from the functracer
     * to avoid recursive loops.
     */
    if (rtld_functrace != NULL && obj != rtld_functrace_obj) {
	if (rtld_functrace(obj->path,
			   defobj->path,
			   defobj->strtab + def->st_name,
			   stack)) {
	    lock_release(rtld_bind_lock, &lockstate);
	    return target;
	}
    }

    /*
     * Write the new contents for the jmpslot. Note that depending on
     * architecture, the value which we need to return back to the
     * lazy binding trampoline may or may not be the target
     * address. The value returned from reloc_jmpslot() is the value
     * that the trampoline needs.
     */
    target = reloc_jmpslot(where, target, defobj, obj, rel);
    lock_release(rtld_bind_lock, &lockstate);
    return target;
}

/*
 * Error reporting function.  Use it like printf.  If formats the message
 * into a buffer, and sets things up so that the next call to dlerror()
 * will return the message.
 */
void
_rtld_error(const char *fmt, ...)
{
    static char buf[512];
    va_list ap;

    va_start(ap, fmt);
    rtld_vsnprintf(buf, sizeof buf, fmt, ap);
    error_message = buf;
    va_end(ap);
}

/*
 * Return a dynamically-allocated copy of the current error message, if any.
 */
static char *
errmsg_save(void)
{
    return error_message == NULL ? NULL : xstrdup(error_message);
}

/*
 * Restore the current error message from a copy which was previously saved
 * by errmsg_save().  The copy is freed.
 */
static void
errmsg_restore(char *saved_msg)
{
    if (saved_msg == NULL)
	error_message = NULL;
    else {
	_rtld_error("%s", saved_msg);
	free(saved_msg);
    }
}

const char *
basename(const char *name)
{
    const char *p = strrchr(name, '/');
    return p != NULL ? p + 1 : name;
}

static struct utsname uts;

static char *
origin_subst_one(char *real, const char *kw, const char *subst,
    bool may_free)
{
	char *p, *p1, *res, *resp;
	int subst_len, kw_len, subst_count, old_len, new_len;

	kw_len = strlen(kw);

	/*
	 * First, count the number of the keyword occurrences, to
	 * preallocate the final string.
	 */
	for (p = real, subst_count = 0;; p = p1 + kw_len, subst_count++) {
		p1 = strstr(p, kw);
		if (p1 == NULL)
			break;
	}

	/*
	 * If the keyword is not found, just return.
	 */
	if (subst_count == 0)
		return (may_free ? real : xstrdup(real));

	/*
	 * There is indeed something to substitute.  Calculate the
	 * length of the resulting string, and allocate it.
	 */
	subst_len = strlen(subst);
	old_len = strlen(real);
	new_len = old_len + (subst_len - kw_len) * subst_count;
	res = xmalloc(new_len + 1);

	/*
	 * Now, execute the substitution loop.
	 */
	for (p = real, resp = res, *resp = '\0';;) {
		p1 = strstr(p, kw);
		if (p1 != NULL) {
			/* Copy the prefix before keyword. */
			memcpy(resp, p, p1 - p);
			resp += p1 - p;
			/* Keyword replacement. */
			memcpy(resp, subst, subst_len);
			resp += subst_len;
			*resp = '\0';
			p = p1 + kw_len;
		} else
			break;
	}

	/* Copy to the end of string and finish. */
	strcat(resp, p);
	if (may_free)
		free(real);
	return (res);
}

static char *
origin_subst(char *real, const char *origin_path)
{
	char *res1, *res2, *res3, *res4;

	if (uts.sysname[0] == '\0') {
		if (uname(&uts) != 0) {
			_rtld_error("utsname failed: %d", errno);
			return (NULL);
		}
	}
	res1 = origin_subst_one(real, "$ORIGIN", origin_path, false);
	res2 = origin_subst_one(res1, "$OSNAME", uts.sysname, true);
	res3 = origin_subst_one(res2, "$OSREL", uts.release, true);
	res4 = origin_subst_one(res3, "$PLATFORM", uts.machine, true);
	return (res4);
}

static void
die(void)
{
    const char *msg = dlerror();

    if (msg == NULL)
	msg = "Fatal error";
    rtld_fdputstr(STDERR_FILENO, msg);
    rtld_fdputchar(STDERR_FILENO, '\n');
    _exit(1);
}

/*
 * Process a shared object's DYNAMIC section, and save the important
 * information in its Obj_Entry structure.
 */
static void
digest_dynamic1(Obj_Entry *obj, int early, const Elf_Dyn **dyn_rpath,
    const Elf_Dyn **dyn_soname, const Elf_Dyn **dyn_runpath)
{
    const Elf_Dyn *dynp;
    Needed_Entry **needed_tail = &obj->needed;
    Needed_Entry **needed_filtees_tail = &obj->needed_filtees;
    Needed_Entry **needed_aux_filtees_tail = &obj->needed_aux_filtees;
    const Elf_Hashelt *hashtab;
    const Elf32_Word *hashval;
    Elf32_Word bkt, nmaskwords;
    int bloom_size32;
    bool nmw_power2;
    int plttype = DT_REL;

    *dyn_rpath = NULL;
    *dyn_soname = NULL;
    *dyn_runpath = NULL;

    obj->bind_now = false;
    for (dynp = obj->dynamic;  dynp->d_tag != DT_NULL;  dynp++) {
	switch (dynp->d_tag) {

	case DT_REL:
	    obj->rel = (const Elf_Rel *) (obj->relocbase + dynp->d_un.d_ptr);
	    break;

	case DT_RELSZ:
	    obj->relsize = dynp->d_un.d_val;
	    break;

	case DT_RELENT:
	    assert(dynp->d_un.d_val == sizeof(Elf_Rel));
	    break;

	case DT_JMPREL:
	    obj->pltrel = (const Elf_Rel *)
	      (obj->relocbase + dynp->d_un.d_ptr);
	    break;

	case DT_PLTRELSZ:
	    obj->pltrelsize = dynp->d_un.d_val;
	    break;

	case DT_RELA:
	    obj->rela = (const Elf_Rela *) (obj->relocbase + dynp->d_un.d_ptr);
	    break;

	case DT_RELASZ:
	    obj->relasize = dynp->d_un.d_val;
	    break;

	case DT_RELAENT:
	    assert(dynp->d_un.d_val == sizeof(Elf_Rela));
	    break;

	case DT_PLTREL:
	    plttype = dynp->d_un.d_val;
	    assert(dynp->d_un.d_val == DT_REL || plttype == DT_RELA);
	    break;

	case DT_SYMTAB:
	    obj->symtab = (const Elf_Sym *)
	      (obj->relocbase + dynp->d_un.d_ptr);
	    break;

	case DT_SYMENT:
	    assert(dynp->d_un.d_val == sizeof(Elf_Sym));
	    break;

	case DT_STRTAB:
	    obj->strtab = (const char *) (obj->relocbase + dynp->d_un.d_ptr);
	    break;

	case DT_STRSZ:
	    obj->strsize = dynp->d_un.d_val;
	    break;

	case DT_VERNEED:
	    obj->verneed = (const Elf_Verneed *) (obj->relocbase +
		dynp->d_un.d_val);
	    break;

	case DT_VERNEEDNUM:
	    obj->verneednum = dynp->d_un.d_val;
	    break;

	case DT_VERDEF:
	    obj->verdef = (const Elf_Verdef *) (obj->relocbase +
		dynp->d_un.d_val);
	    break;

	case DT_VERDEFNUM:
	    obj->verdefnum = dynp->d_un.d_val;
	    break;

	case DT_VERSYM:
	    obj->versyms = (const Elf_Versym *)(obj->relocbase +
		dynp->d_un.d_val);
	    break;

	case DT_HASH:
	    {
		hashtab = (const Elf_Hashelt *)(obj->relocbase +
		    dynp->d_un.d_ptr);
		obj->nbuckets = hashtab[0];
		obj->nchains = hashtab[1];
		obj->buckets = hashtab + 2;
		obj->chains = obj->buckets + obj->nbuckets;
		obj->valid_hash_sysv = obj->nbuckets > 0 && obj->nchains > 0 &&
		  obj->buckets != NULL;
	    }
	    break;

	case DT_GNU_HASH:
	    {
		hashtab = (const Elf_Hashelt *)(obj->relocbase +
		    dynp->d_un.d_ptr);
		obj->nbuckets_gnu = hashtab[0];
		obj->symndx_gnu = hashtab[1];
		nmaskwords = hashtab[2];
		bloom_size32 = (__ELF_WORD_SIZE / 32) * nmaskwords;
		/* Number of bitmask words is required to be power of 2 */
		nmw_power2 = powerof2(nmaskwords);
		obj->maskwords_bm_gnu = nmaskwords - 1;
		obj->shift2_gnu = hashtab[3];
		obj->bloom_gnu = (Elf_Addr *) (hashtab + 4);
		obj->buckets_gnu = hashtab + 4 + bloom_size32;
		obj->chain_zero_gnu = obj->buckets_gnu + obj->nbuckets_gnu -
		  obj->symndx_gnu;
		obj->valid_hash_gnu = nmw_power2 && obj->nbuckets_gnu > 0 &&
		  obj->buckets_gnu != NULL;
	    }
	    break;

	case DT_NEEDED:
	    if (!obj->rtld) {
		Needed_Entry *nep = NEW(Needed_Entry);
		nep->name = dynp->d_un.d_val;
		nep->obj = NULL;
		nep->next = NULL;

		*needed_tail = nep;
		needed_tail = &nep->next;
	    }
	    break;

	case DT_FILTER:
	    if (!obj->rtld) {
		Needed_Entry *nep = NEW(Needed_Entry);
		nep->name = dynp->d_un.d_val;
		nep->obj = NULL;
		nep->next = NULL;

		*needed_filtees_tail = nep;
		needed_filtees_tail = &nep->next;
	    }
	    break;

	case DT_AUXILIARY:
	    if (!obj->rtld) {
		Needed_Entry *nep = NEW(Needed_Entry);
		nep->name = dynp->d_un.d_val;
		nep->obj = NULL;
		nep->next = NULL;

		*needed_aux_filtees_tail = nep;
		needed_aux_filtees_tail = &nep->next;
	    }
	    break;

	case DT_PLTGOT:
	    obj->pltgot = (Elf_Addr *) (obj->relocbase + dynp->d_un.d_ptr);
	    break;

	case DT_TEXTREL:
	    obj->textrel = true;
	    break;

	case DT_SYMBOLIC:
	    obj->symbolic = true;
	    break;

	case DT_RPATH:
	    /*
	     * We have to wait until later to process this, because we
	     * might not have gotten the address of the string table yet.
	     */
	    *dyn_rpath = dynp;
	    break;

	case DT_SONAME:
	    *dyn_soname = dynp;
	    break;

	case DT_RUNPATH:
	    *dyn_runpath = dynp;
	    break;

	case DT_INIT:
	    obj->init = (Elf_Addr) (obj->relocbase + dynp->d_un.d_ptr);
	    break;

	case DT_PREINIT_ARRAY:
	    obj->preinit_array = (Elf_Addr)(obj->relocbase + dynp->d_un.d_ptr);
	    break;

	case DT_PREINIT_ARRAYSZ:
	    obj->preinit_array_num = dynp->d_un.d_val / sizeof(Elf_Addr);
	    break;

	case DT_INIT_ARRAY:
	    obj->init_array = (Elf_Addr)(obj->relocbase + dynp->d_un.d_ptr);
	    break;

	case DT_INIT_ARRAYSZ:
	    obj->init_array_num = dynp->d_un.d_val / sizeof(Elf_Addr);
	    break;

	case DT_FINI:
	    obj->fini = (Elf_Addr)(obj->relocbase + dynp->d_un.d_ptr);
	    break;

	case DT_FINI_ARRAY:
	    obj->fini_array = (Elf_Addr)(obj->relocbase + dynp->d_un.d_ptr);
	    break;

	case DT_FINI_ARRAYSZ:
	    obj->fini_array_num = dynp->d_un.d_val / sizeof(Elf_Addr);
	    break;

	case DT_DEBUG:
	    /* XXX - not implemented yet */
	    if (!early)
		dbg("Filling in DT_DEBUG entry");
	    ((Elf_Dyn*)dynp)->d_un.d_ptr = (Elf_Addr) &r_debug;
	    break;

	case DT_FLAGS:
		if ((dynp->d_un.d_val & DF_ORIGIN) && trust)
		    obj->z_origin = true;
		if (dynp->d_un.d_val & DF_SYMBOLIC)
		    obj->symbolic = true;
		if (dynp->d_un.d_val & DF_TEXTREL)
		    obj->textrel = true;
		if (dynp->d_un.d_val & DF_BIND_NOW)
		    obj->bind_now = true;
		if (dynp->d_un.d_val & DF_STATIC_TLS)
		    obj->static_tls = true;
	    break;

	case DT_FLAGS_1:
		if (dynp->d_un.d_val & DF_1_NOOPEN)
		    obj->z_noopen = true;
		if ((dynp->d_un.d_val & DF_1_ORIGIN) && trust)
		    obj->z_origin = true;
		/*if (dynp->d_un.d_val & DF_1_GLOBAL)
		    XXX ;*/
		if (dynp->d_un.d_val & DF_1_BIND_NOW)
		    obj->bind_now = true;
		if (dynp->d_un.d_val & DF_1_NODELETE)
		    obj->z_nodelete = true;
		if (dynp->d_un.d_val & DF_1_LOADFLTR)
		    obj->z_loadfltr = true;
		if (dynp->d_un.d_val & DF_1_INTERPOSE)
		    obj->z_interpose = true;
		if (dynp->d_un.d_val & DF_1_NODEFLIB)
		    obj->z_nodeflib = true;
	    break;

	default:
	    if (!early) {
		dbg("Ignoring d_tag %ld = %#lx", (long)dynp->d_tag,
		    (long)dynp->d_tag);
	    }
	    break;
	}
    }

    obj->traced = false;

    if (plttype == DT_RELA) {
	obj->pltrela = (const Elf_Rela *) obj->pltrel;
	obj->pltrel = NULL;
	obj->pltrelasize = obj->pltrelsize;
	obj->pltrelsize = 0;
    }

    /* Determine size of dynsym table (equal to nchains of sysv hash) */
    if (obj->valid_hash_sysv)
	obj->dynsymcount = obj->nchains;
    else if (obj->valid_hash_gnu) {
	obj->dynsymcount = 0;
	for (bkt = 0; bkt < obj->nbuckets_gnu; bkt++) {
	    if (obj->buckets_gnu[bkt] == 0)
		continue;
	    hashval = &obj->chain_zero_gnu[obj->buckets_gnu[bkt]];
	    do
		obj->dynsymcount++;
	    while ((*hashval++ & 1u) == 0);
	}
	obj->dynsymcount += obj->symndx_gnu;
    }
}

static void
digest_dynamic2(Obj_Entry *obj, const Elf_Dyn *dyn_rpath,
    const Elf_Dyn *dyn_soname, const Elf_Dyn *dyn_runpath)
{

    if (obj->z_origin && obj->origin_path == NULL) {
	obj->origin_path = xmalloc(PATH_MAX);
	if (rtld_dirname_abs(obj->path, obj->origin_path) == -1)
	    die();
    }

    if (dyn_runpath != NULL) {
	obj->runpath = (char *)obj->strtab + dyn_runpath->d_un.d_val;
	if (obj->z_origin)
	    obj->runpath = origin_subst(obj->runpath, obj->origin_path);
    }
    else if (dyn_rpath != NULL) {
	obj->rpath = (char *)obj->strtab + dyn_rpath->d_un.d_val;
	if (obj->z_origin)
	    obj->rpath = origin_subst(obj->rpath, obj->origin_path);
    }

    if (dyn_soname != NULL)
	object_add_name(obj, obj->strtab + dyn_soname->d_un.d_val);
}

static void
digest_dynamic(Obj_Entry *obj, int early)
{
	const Elf_Dyn *dyn_rpath;
	const Elf_Dyn *dyn_soname;
	const Elf_Dyn *dyn_runpath;

	digest_dynamic1(obj, early, &dyn_rpath, &dyn_soname, &dyn_runpath);
	digest_dynamic2(obj, dyn_rpath, dyn_soname, dyn_runpath);
}

/*
 * Process a shared object's program header.  This is used only for the
 * main program, when the kernel has already loaded the main program
 * into memory before calling the dynamic linker.  It creates and
 * returns an Obj_Entry structure.
 */
static Obj_Entry *
digest_phdr(const Elf_Phdr *phdr, int phnum, caddr_t entry, const char *path)
{
    Obj_Entry *obj;
    const Elf_Phdr *phlimit = phdr + phnum;
    const Elf_Phdr *ph;
    Elf_Addr note_start, note_end;
    int nsegs = 0;

    obj = obj_new();
    for (ph = phdr;  ph < phlimit;  ph++) {
	if (ph->p_type != PT_PHDR)
	    continue;

	obj->phdr = phdr;
	obj->phsize = ph->p_memsz;
	obj->relocbase = (caddr_t)phdr - ph->p_vaddr;
	break;
    }

    obj->stack_flags = PF_X | PF_R | PF_W;

    for (ph = phdr;  ph < phlimit;  ph++) {
	switch (ph->p_type) {

	case PT_INTERP:
	    obj->interp = (const char *)(ph->p_vaddr + obj->relocbase);
	    break;

	case PT_LOAD:
	    if (nsegs == 0) {	/* First load segment */
		obj->vaddrbase = trunc_page(ph->p_vaddr);
		obj->mapbase = obj->vaddrbase + obj->relocbase;
		obj->textsize = round_page(ph->p_vaddr + ph->p_memsz) -
		  obj->vaddrbase;
	    } else {		/* Last load segment */
		obj->mapsize = round_page(ph->p_vaddr + ph->p_memsz) -
		  obj->vaddrbase;
	    }
	    nsegs++;
	    break;

	case PT_DYNAMIC:
	    obj->dynamic = (const Elf_Dyn *)(ph->p_vaddr + obj->relocbase);
	    break;

	case PT_TLS:
	    obj->tlsindex = 1;
	    obj->tlssize = ph->p_memsz;
	    obj->tlsalign = ph->p_align;
	    obj->tlsinitsize = ph->p_filesz;
	    obj->tlsinit = (void*)(ph->p_vaddr + obj->relocbase);
	    break;

	case PT_GNU_STACK:
	    obj->stack_flags = ph->p_flags;
	    break;

	case PT_GNU_RELRO:
	    obj->relro_page = obj->relocbase + trunc_page(ph->p_vaddr);
	    obj->relro_size = round_page(ph->p_memsz);
	    break;

	case PT_NOTE:
	    obj->note_present = true;
	    note_start = (Elf_Addr)obj->relocbase + ph->p_vaddr;
	    note_end = note_start + ph->p_filesz;
	    digest_notes(obj, note_start, note_end);
	    break;
	}
    }
    if (nsegs < 1) {
	_rtld_error("%s: too few PT_LOAD segments", path);
	return NULL;
    }

    obj->entry = entry;
    return obj;
}

void
digest_notes(Obj_Entry *obj, Elf_Addr note_start, Elf_Addr note_end)
{
	const Elf_Note *note;
	const char *note_name;
	uintptr_t p;

	for (note = (const Elf_Note *)note_start; (Elf_Addr)note < note_end;
	    note = (const Elf_Note *)((const char *)(note + 1) +
	      roundup2(note->n_namesz, sizeof(Elf32_Addr)) +
	      roundup2(note->n_descsz, sizeof(Elf32_Addr)))) {
		if (note->n_namesz != sizeof(NOTE_VENDOR) ||
		    note->n_descsz != sizeof(int32_t))
			continue;
		if (note->n_type != ABI_NOTETYPE &&
		    note->n_type != CRT_NOINIT_NOTETYPE)
			continue;
		note_name = (const char *)(note + 1);
		if (strncmp(NOTE_VENDOR, note_name, sizeof(NOTE_VENDOR)) != 0)
			continue;
		switch (note->n_type) {
		case ABI_NOTETYPE:
			/* DragonFly osrel note */
			p = (uintptr_t)(note + 1);
			p += roundup2(note->n_namesz, sizeof(Elf32_Addr));
			obj->osrel = *(const int32_t *)(p);
			dbg("note osrel %d", obj->osrel);
			break;
		case CRT_NOINIT_NOTETYPE:
			/* DragonFly 'crt does not call init' note */
			obj->crt_no_init = true;
			dbg("note crt_no_init");
			break;
		}
	}
}

static Obj_Entry *
dlcheck(void *handle)
{
    Obj_Entry *obj;

    for (obj = obj_list;  obj != NULL;  obj = obj->next)
	if (obj == (Obj_Entry *) handle)
	    break;

    if (obj == NULL || obj->refcount == 0 || obj->dl_refcount == 0) {
	_rtld_error("Invalid shared object handle %p", handle);
	return NULL;
    }
    return obj;
}

/*
 * If the given object is already in the donelist, return true.  Otherwise
 * add the object to the list and return false.
 */
static bool
donelist_check(DoneList *dlp, const Obj_Entry *obj)
{
    unsigned int i;

    for (i = 0;  i < dlp->num_used;  i++)
	if (dlp->objs[i] == obj)
	    return true;
    /*
     * Our donelist allocation should always be sufficient.  But if
     * our threads locking isn't working properly, more shared objects
     * could have been loaded since we allocated the list.  That should
     * never happen, but we'll handle it properly just in case it does.
     */
    if (dlp->num_used < dlp->num_alloc)
	dlp->objs[dlp->num_used++] = obj;
    return false;
}

/*
 * Hash function for symbol table lookup.  Don't even think about changing
 * this.  It is specified by the System V ABI.
 */
unsigned long
elf_hash(const char *name)
{
    const unsigned char *p = (const unsigned char *) name;
    unsigned long h = 0;
    unsigned long g;

    while (*p != '\0') {
	h = (h << 4) + *p++;
	if ((g = h & 0xf0000000) != 0)
	    h ^= g >> 24;
	h &= ~g;
    }
    return h;
}

/*
 * The GNU hash function is the Daniel J. Bernstein hash clipped to 32 bits
 * unsigned in case it's implemented with a wider type.
 */
static uint_fast32_t
gnu_hash(const char *s)
{
	uint_fast32_t h;
	unsigned char c;

	h = 5381;
	for (c = *s; c != '\0'; c = *++s)
		h = h * 33 + c;
	return (h & 0xffffffff);
}


/*
 * Find the library with the given name, and return its full pathname.
 * The returned string is dynamically allocated.  Generates an error
 * message and returns NULL if the library cannot be found.
 *
 * If the second argument is non-NULL, then it refers to an already-
 * loaded shared object, whose library search path will be searched.
 *
 * If a library is successfully located via LD_LIBRARY_PATH_FDS, its
 * descriptor (which is close-on-exec) will be passed out via the third
 * argument.
 *
 * The search order is:
 *   DT_RPATH in the referencing file _unless_ DT_RUNPATH is present (1)
 *   DT_RPATH of the main object if DSO without defined DT_RUNPATH (1)
 *   LD_LIBRARY_PATH
 *   DT_RUNPATH in the referencing file
 *   ldconfig hints (if -z nodefaultlib, filter out default library directories
 *	 from list)
 *   /lib:/usr/lib _unless_ the referencing file is linked with -z nodefaultlib
 *
 * (1) Handled in digest_dynamic2 - rpath left NULL if runpath defined.
 */
static char *
find_library(const char *xname, const Obj_Entry *refobj, int *fdp)
{
    char *pathname;
    char *name;
    bool nodeflib, objgiven;

    objgiven = refobj != NULL;
    if (strchr(xname, '/') != NULL) {	/* Hard coded pathname */
	if (xname[0] != '/' && !trust) {
	    _rtld_error("Absolute pathname required for shared object \"%s\"",
	      xname);
	    return NULL;
	}
	if (objgiven && refobj->z_origin) {
		return (origin_subst(__DECONST(char *, xname),
		    refobj->origin_path));
	} else {
		return (xstrdup(xname));
	}
    }

    if (libmap_disable || !objgiven ||
	(name = lm_find(refobj->path, xname)) == NULL)
	name = (char *)xname;

    dbg(" Searching for \"%s\"", name);

    nodeflib = objgiven ? refobj->z_nodeflib : false;
    if ((objgiven &&
      (pathname = search_library_path(name, refobj->rpath)) != NULL) ||
      (objgiven && refobj->runpath == NULL && refobj != obj_main &&
      (pathname = search_library_path(name, obj_main->rpath)) != NULL) ||
      (pathname = search_library_path(name, ld_library_path)) != NULL ||
      (objgiven &&
      (pathname = search_library_path(name, refobj->runpath)) != NULL) ||
      (pathname = search_library_pathfds(name, ld_library_dirs, fdp)) != NULL ||
      (pathname = search_library_path(name, gethints(nodeflib))) != NULL ||
      (objgiven && !nodeflib &&
      (pathname = search_library_path(name, STANDARD_LIBRARY_PATH)) != NULL))
	return (pathname);

    if (objgiven && refobj->path != NULL) {
	_rtld_error("Shared object \"%s\" not found, required by \"%s\"",
	  name, basename(refobj->path));
    } else {
	_rtld_error("Shared object \"%s\" not found", name);
    }
    return NULL;
}

/*
 * Given a symbol number in a referencing object, find the corresponding
 * definition of the symbol.  Returns a pointer to the symbol, or NULL if
 * no definition was found.  Returns a pointer to the Obj_Entry of the
 * defining object via the reference parameter DEFOBJ_OUT.
 */
const Elf_Sym *
find_symdef(unsigned long symnum, const Obj_Entry *refobj,
    const Obj_Entry **defobj_out, int flags, SymCache *cache,
    RtldLockState *lockstate)
{
    const Elf_Sym *ref;
    const Elf_Sym *def;
    const Obj_Entry *defobj;
    SymLook req;
    const char *name;
    int res;

    /*
     * If we have already found this symbol, get the information from
     * the cache.
     */
    if (symnum >= refobj->dynsymcount)
	return NULL;	/* Bad object */
    if (cache != NULL && cache[symnum].sym != NULL) {
	*defobj_out = cache[symnum].obj;
	return cache[symnum].sym;
    }

    ref = refobj->symtab + symnum;
    name = refobj->strtab + ref->st_name;
    def = NULL;
    defobj = NULL;

    /*
     * We don't have to do a full scale lookup if the symbol is local.
     * We know it will bind to the instance in this load module; to
     * which we already have a pointer (ie ref). By not doing a lookup,
     * we not only improve performance, but it also avoids unresolvable
     * symbols when local symbols are not in the hash table.
     *
     * This might occur for TLS module relocations, which simply use
     * symbol 0.
     */
    if (ELF_ST_BIND(ref->st_info) != STB_LOCAL) {
	if (ELF_ST_TYPE(ref->st_info) == STT_SECTION) {
	    _rtld_error("%s: Bogus symbol table entry %lu", refobj->path,
		symnum);
	}
	symlook_init(&req, name);
	req.flags = flags;
	req.ventry = fetch_ventry(refobj, symnum);
	req.lockstate = lockstate;
	res = symlook_default(&req, refobj);
	if (res == 0) {
	    def = req.sym_out;
	    defobj = req.defobj_out;
	}
    } else {
	def = ref;
	defobj = refobj;
    }

    /*
     * If we found no definition and the reference is weak, treat the
     * symbol as having the value zero.
     */
    if (def == NULL && ELF_ST_BIND(ref->st_info) == STB_WEAK) {
	def = &sym_zero;
	defobj = obj_main;
    }

    if (def != NULL) {
	*defobj_out = defobj;
	/* Record the information in the cache to avoid subsequent lookups. */
	if (cache != NULL) {
	    cache[symnum].sym = def;
	    cache[symnum].obj = defobj;
	}
    } else {
	if (refobj != &obj_rtld)
	    _rtld_error("%s: Undefined symbol \"%s\"", refobj->path, name);
    }
    return def;
}

/*
 * Return the search path from the ldconfig hints file, reading it if
 * necessary.  If nostdlib is true, then the default search paths are
 * not added to result.
 *
 * Returns NULL if there are problems with the hints file,
 * or if the search path there is empty.
 */
static const char *
gethints(bool nostdlib)
{
	static char *hints, *filtered_path;
	struct elfhints_hdr hdr;
	struct fill_search_info_args sargs, hargs;
	struct dl_serinfo smeta, hmeta, *SLPinfo, *hintinfo;
	struct dl_serpath *SLPpath, *hintpath;
	char *p;
	unsigned int SLPndx, hintndx, fndx, fcount;
	int fd;
	size_t flen;
	bool skip;

	/* First call, read the hints file */
	if (hints == NULL) {
		/* Keep from trying again in case the hints file is bad. */
		hints = "";

		if ((fd = open(ld_elf_hints_path, O_RDONLY | O_CLOEXEC)) == -1)
			return (NULL);
		if (read(fd, &hdr, sizeof hdr) != sizeof hdr ||
		    hdr.magic != ELFHINTS_MAGIC ||
		    hdr.version != 1) {
			close(fd);
			return (NULL);
		}
		p = xmalloc(hdr.dirlistlen + 1);
		if (lseek(fd, hdr.strtab + hdr.dirlist, SEEK_SET) == -1 ||
		    read(fd, p, hdr.dirlistlen + 1) !=
		    (ssize_t)hdr.dirlistlen + 1) {
			free(p);
			close(fd);
			return (NULL);
		}
		hints = p;
		close(fd);
	}

	/*
	 * If caller agreed to receive list which includes the default
	 * paths, we are done. Otherwise, if we still have not
	 * calculated filtered result, do it now.
	 */
	if (!nostdlib)
		return (hints[0] != '\0' ? hints : NULL);
	if (filtered_path != NULL)
		goto filt_ret;

	/*
	 * Obtain the list of all configured search paths, and the
	 * list of the default paths.
	 *
	 * First estimate the size of the results.
	 */
	smeta.dls_size = __offsetof(struct dl_serinfo, dls_serpath);
	smeta.dls_cnt = 0;
	hmeta.dls_size = __offsetof(struct dl_serinfo, dls_serpath);
	hmeta.dls_cnt = 0;

	sargs.request = RTLD_DI_SERINFOSIZE;
	sargs.serinfo = &smeta;
	hargs.request = RTLD_DI_SERINFOSIZE;
	hargs.serinfo = &hmeta;

	path_enumerate(STANDARD_LIBRARY_PATH, fill_search_info, &sargs);
	path_enumerate(p, fill_search_info, &hargs);

	SLPinfo = xmalloc(smeta.dls_size);
	hintinfo = xmalloc(hmeta.dls_size);

	/*
	 * Next fetch both sets of paths.
	 */
	sargs.request = RTLD_DI_SERINFO;
	sargs.serinfo = SLPinfo;
	sargs.serpath = &SLPinfo->dls_serpath[0];
	sargs.strspace = (char *)&SLPinfo->dls_serpath[smeta.dls_cnt];

	hargs.request = RTLD_DI_SERINFO;
	hargs.serinfo = hintinfo;
	hargs.serpath = &hintinfo->dls_serpath[0];
	hargs.strspace = (char *)&hintinfo->dls_serpath[hmeta.dls_cnt];

	path_enumerate(STANDARD_LIBRARY_PATH, fill_search_info, &sargs);
	path_enumerate(p, fill_search_info, &hargs);

	/*
	 * Now calculate the difference between two sets, by excluding
	 * standard paths from the full set.
	 */
	fndx = 0;
	fcount = 0;
	filtered_path = xmalloc(hdr.dirlistlen + 1);
	hintpath = &hintinfo->dls_serpath[0];
	for (hintndx = 0; hintndx < hmeta.dls_cnt; hintndx++, hintpath++) {
		skip = false;
		SLPpath = &SLPinfo->dls_serpath[0];
		/*
		 * Check each standard path against current.
		 */
		for (SLPndx = 0; SLPndx < smeta.dls_cnt; SLPndx++, SLPpath++) {
			/* matched, skip the path */
			if (!strcmp(hintpath->dls_name, SLPpath->dls_name)) {
				skip = true;
				break;
			}
		}
		if (skip)
			continue;
		/*
		 * Not matched against any standard path, add the path
		 * to result. Separate consecutive paths with ':'.
		 */
		if (fcount > 0) {
			filtered_path[fndx] = ':';
			fndx++;
		}
		fcount++;
		flen = strlen(hintpath->dls_name);
		strncpy((filtered_path + fndx),	hintpath->dls_name, flen);
		fndx += flen;
	}
	filtered_path[fndx] = '\0';

	free(SLPinfo);
	free(hintinfo);

filt_ret:
	return (filtered_path[0] != '\0' ? filtered_path : NULL);
}

static void
init_dag(Obj_Entry *root)
{
    const Needed_Entry *needed;
    const Objlist_Entry *elm;
    DoneList donelist;

    if (root->dag_inited)
	return;
    donelist_init(&donelist);

    /* Root object belongs to own DAG. */
    objlist_push_tail(&root->dldags, root);
    objlist_push_tail(&root->dagmembers, root);
    donelist_check(&donelist, root);

    /*
     * Add dependencies of root object to DAG in breadth order
     * by exploiting the fact that each new object get added
     * to the tail of the dagmembers list.
     */
    STAILQ_FOREACH(elm, &root->dagmembers, link) {
	for (needed = elm->obj->needed; needed != NULL; needed = needed->next) {
	    if (needed->obj == NULL || donelist_check(&donelist, needed->obj))
		continue;
	    objlist_push_tail(&needed->obj->dldags, root);
	    objlist_push_tail(&root->dagmembers, needed->obj);
	}
    }
    root->dag_inited = true;
}

static void
process_nodelete(Obj_Entry *root)
{
	const Objlist_Entry *elm;

	/*
	 * Walk over object DAG and process every dependent object that
	 * is marked as DF_1_NODELETE. They need to grow their own DAG,
	 * which then should have its reference upped separately.
	 */
	STAILQ_FOREACH(elm, &root->dagmembers, link) {
		if (elm->obj != NULL && elm->obj->z_nodelete &&
		    !elm->obj->ref_nodel) {
			dbg("obj %s nodelete", elm->obj->path);
			init_dag(elm->obj);
			ref_dag(elm->obj);
			elm->obj->ref_nodel = true;
		}
	}
}

/*
 * Initialize the dynamic linker.  The argument is the address at which
 * the dynamic linker has been mapped into memory.  The primary task of
 * this function is to relocate the dynamic linker.
 */
static void
init_rtld(caddr_t mapbase, Elf_Auxinfo **aux_info)
{
    Obj_Entry objtmp;	/* Temporary rtld object */
    const Elf_Ehdr *ehdr;
    const Elf_Dyn *dyn_rpath;
    const Elf_Dyn *dyn_soname;
    const Elf_Dyn *dyn_runpath;

    /*
     * Conjure up an Obj_Entry structure for the dynamic linker.
     *
     * The "path" member can't be initialized yet because string constants
     * cannot yet be accessed. Below we will set it correctly.
     */
    memset(&objtmp, 0, sizeof(objtmp));
    objtmp.path = NULL;
    objtmp.rtld = true;
    objtmp.mapbase = mapbase;
#ifdef PIC
    objtmp.relocbase = mapbase;
#endif
    if (RTLD_IS_DYNAMIC()) {
	objtmp.dynamic = rtld_dynamic(&objtmp);
	digest_dynamic1(&objtmp, 1, &dyn_rpath, &dyn_soname, &dyn_runpath);
	assert(objtmp.needed == NULL);
	assert(!objtmp.textrel);

	/*
	 * Temporarily put the dynamic linker entry into the object list, so
	 * that symbols can be found.
	 */

	relocate_objects(&objtmp, true, &objtmp, 0, NULL);
    }
    ehdr = (Elf_Ehdr *)mapbase;
    objtmp.phdr = (Elf_Phdr *)((char *)mapbase + ehdr->e_phoff);
    objtmp.phsize = ehdr->e_phnum * sizeof(objtmp.phdr[0]);

    /* Initialize the object list. */
    obj_tail = &obj_list;

    /* Now that non-local variables can be accesses, copy out obj_rtld. */
    memcpy(&obj_rtld, &objtmp, sizeof(obj_rtld));

#ifdef ENABLE_OSRELDATE
    if (aux_info[AT_OSRELDATE] != NULL)
	    osreldate = aux_info[AT_OSRELDATE]->a_un.a_val;
#endif

    digest_dynamic2(&obj_rtld, dyn_rpath, dyn_soname, dyn_runpath);

    /* Replace the path with a dynamically allocated copy. */
    obj_rtld.path = xstrdup(PATH_RTLD);

    r_debug.r_brk = r_debug_state;
    r_debug.r_state = RT_CONSISTENT;
}

/*
 * Add the init functions from a needed object list (and its recursive
 * needed objects) to "list".  This is not used directly; it is a helper
 * function for initlist_add_objects().  The write lock must be held
 * when this function is called.
 */
static void
initlist_add_neededs(Needed_Entry *needed, Objlist *list)
{
    /* Recursively process the successor needed objects. */
    if (needed->next != NULL)
	initlist_add_neededs(needed->next, list);

    /* Process the current needed object. */
    if (needed->obj != NULL)
	initlist_add_objects(needed->obj, &needed->obj->next, list);
}

/*
 * Scan all of the DAGs rooted in the range of objects from "obj" to
 * "tail" and add their init functions to "list".  This recurses over
 * the DAGs and ensure the proper init ordering such that each object's
 * needed libraries are initialized before the object itself.  At the
 * same time, this function adds the objects to the global finalization
 * list "list_fini" in the opposite order.  The write lock must be
 * held when this function is called.
 */
static void
initlist_add_objects(Obj_Entry *obj, Obj_Entry **tail, Objlist *list)
{

    if (obj->init_scanned || obj->init_done)
	return;
    obj->init_scanned = true;

    /* Recursively process the successor objects. */
    if (&obj->next != tail)
	initlist_add_objects(obj->next, tail, list);

    /* Recursively process the needed objects. */
    if (obj->needed != NULL)
	initlist_add_neededs(obj->needed, list);
    if (obj->needed_filtees != NULL)
	initlist_add_neededs(obj->needed_filtees, list);
    if (obj->needed_aux_filtees != NULL)
	initlist_add_neededs(obj->needed_aux_filtees, list);

    /* Add the object to the init list. */
    if (obj->preinit_array != (Elf_Addr)NULL || obj->init != (Elf_Addr)NULL ||
      obj->init_array != (Elf_Addr)NULL)
	objlist_push_tail(list, obj);

    /* Add the object to the global fini list in the reverse order. */
    if ((obj->fini != (Elf_Addr)NULL || obj->fini_array != (Elf_Addr)NULL)
      && !obj->on_fini_list) {
	objlist_push_head(&list_fini, obj);
	obj->on_fini_list = true;
    }
}

#ifndef FPTR_TARGET
#define FPTR_TARGET(f)	((Elf_Addr) (f))
#endif

static void
free_needed_filtees(Needed_Entry *n)
{
    Needed_Entry *needed, *needed1;

    for (needed = n; needed != NULL; needed = needed->next) {
	if (needed->obj != NULL) {
	    dlclose(needed->obj);
	    needed->obj = NULL;
	}
    }
    for (needed = n; needed != NULL; needed = needed1) {
	needed1 = needed->next;
	free(needed);
    }
}

static void
unload_filtees(Obj_Entry *obj)
{

    free_needed_filtees(obj->needed_filtees);
    obj->needed_filtees = NULL;
    free_needed_filtees(obj->needed_aux_filtees);
    obj->needed_aux_filtees = NULL;
    obj->filtees_loaded = false;
}

static void
load_filtee1(Obj_Entry *obj, Needed_Entry *needed, int flags,
    RtldLockState *lockstate)
{

    for (; needed != NULL; needed = needed->next) {
	needed->obj = dlopen_object(obj->strtab + needed->name, -1, obj,
	  flags, ((ld_loadfltr || obj->z_loadfltr) ? RTLD_NOW : RTLD_LAZY) |
	  RTLD_LOCAL, lockstate);
    }
}

static void
load_filtees(Obj_Entry *obj, int flags, RtldLockState *lockstate)
{

    lock_restart_for_upgrade(lockstate);
    if (!obj->filtees_loaded) {
	load_filtee1(obj, obj->needed_filtees, flags, lockstate);
	load_filtee1(obj, obj->needed_aux_filtees, flags, lockstate);
	obj->filtees_loaded = true;
    }
}

static int
process_needed(Obj_Entry *obj, Needed_Entry *needed, int flags)
{
    Obj_Entry *obj1;

    for (; needed != NULL; needed = needed->next) {
	obj1 = needed->obj = load_object(obj->strtab + needed->name, -1, obj,
	  flags & ~RTLD_LO_NOLOAD);
	if (obj1 == NULL && !ld_tracing && (flags & RTLD_LO_FILTEES) == 0)
	    return (-1);
    }
    return (0);
}

/*
 * Given a shared object, traverse its list of needed objects, and load
 * each of them.  Returns 0 on success.  Generates an error message and
 * returns -1 on failure.
 */
static int
load_needed_objects(Obj_Entry *first, int flags)
{
    Obj_Entry *obj;

    for (obj = first;  obj != NULL;  obj = obj->next) {
	if (process_needed(obj, obj->needed, flags) == -1)
	    return (-1);
    }
    return (0);
}

static int
load_preload_objects(void)
{
    char *p = ld_preload;
    Obj_Entry *obj;
    static const char delim[] = " \t:;";

    if (p == NULL)
	return 0;

    p += strspn(p, delim);
    while (*p != '\0') {
	size_t len = strcspn(p, delim);
	char savech;
	SymLook req;
	int res;

	savech = p[len];
	p[len] = '\0';
	obj = load_object(p, -1, NULL, 0);
	if (obj == NULL)
	    return -1;	/* XXX - cleanup */
	obj->z_interpose = true;
	p[len] = savech;
	p += len;
	p += strspn(p, delim);

	/* Check for the magic tracing function */
	symlook_init(&req, RTLD_FUNCTRACE);
	res = symlook_obj(&req, obj);
	if (res == 0) {
	    rtld_functrace = (void *)(req.defobj_out->relocbase +
				      req.sym_out->st_value);
	    rtld_functrace_obj = req.defobj_out;
	}
    }
    LD_UTRACE(UTRACE_PRELOAD_FINISHED, NULL, NULL, 0, 0, NULL);
    return 0;
}

static const char *
printable_path(const char *path)
{

	return (path == NULL ? "<unknown>" : path);
}

/*
 * Load a shared object into memory, if it is not already loaded.  The
 * object may be specified by name or by user-supplied file descriptor
 * fd_u. In the later case, the fd_u descriptor is not closed, but its
 * duplicate is.
 *
 * Returns a pointer to the Obj_Entry for the object.  Returns NULL
 * on failure.
 */
static Obj_Entry *
load_object(const char *name, int fd_u, const Obj_Entry *refobj, int flags)
{
    Obj_Entry *obj;
    int fd;
    struct stat sb;
    char *path;

    fd = -1;
    if (name != NULL) {
	for (obj = obj_list->next;  obj != NULL;  obj = obj->next) {
	    if (object_match_name(obj, name))
		return (obj);
	}

	path = find_library(name, refobj, &fd);
	if (path == NULL)
	    return (NULL);
    } else
	path = NULL;

    if (fd >= 0) {
	/*
	 * search_library_pathfds() opens a fresh file descriptor for the
	 * library, so there is no need to dup().
	 */
    } else if (fd_u == -1) {
	/*
	 * If we didn't find a match by pathname, or the name is not
	 * supplied, open the file and check again by device and inode.
	 * This avoids false mismatches caused by multiple links or ".."
	 * in pathnames.
	 *
	 * To avoid a race, we open the file and use fstat() rather than
	 * using stat().
	 */
	if ((fd = open(path, O_RDONLY | O_CLOEXEC)) == -1) {
	    _rtld_error("Cannot open \"%s\"", path);
	    free(path);
	    return (NULL);
	}
    } else {
	fd = fcntl(fd_u, F_DUPFD_CLOEXEC, 0);
	if (fd == -1) {
	    _rtld_error("Cannot dup fd");
	    free(path);
	    return (NULL);
	}
    }
    if (fstat(fd, &sb) == -1) {
	_rtld_error("Cannot fstat \"%s\"", printable_path(path));
	close(fd);
	free(path);
	return NULL;
    }
    for (obj = obj_list->next;  obj != NULL;  obj = obj->next)
	if (obj->ino == sb.st_ino && obj->dev == sb.st_dev)
	    break;
    if (obj != NULL && name != NULL) {
	object_add_name(obj, name);
	free(path);
	close(fd);
	return obj;
    }
    if (flags & RTLD_LO_NOLOAD) {
	free(path);
	close(fd);
	return (NULL);
    }

    /* First use of this object, so we must map it in */
    obj = do_load_object(fd, name, path, &sb, flags);
    if (obj == NULL)
	free(path);
    close(fd);

    return obj;
}

static Obj_Entry *
do_load_object(int fd, const char *name, char *path, struct stat *sbp,
  int flags)
{
    Obj_Entry *obj;
    struct statfs fs;

    /*
     * but first, make sure that environment variables haven't been
     * used to circumvent the noexec flag on a filesystem.
     */
    if (dangerous_ld_env) {
	if (fstatfs(fd, &fs) != 0) {
	    _rtld_error("Cannot fstatfs \"%s\"", printable_path(path));
	    return NULL;
	}
	if (fs.f_flags & MNT_NOEXEC) {
	    _rtld_error("Cannot execute objects on %s\n", fs.f_mntonname);
	    return NULL;
	}
    }
    dbg("loading \"%s\"", printable_path(path));
    obj = map_object(fd, printable_path(path), sbp);
    if (obj == NULL)
        return NULL;

    /*
     * If DT_SONAME is present in the object, digest_dynamic2 already
     * added it to the object names.
     */
    if (name != NULL)
	object_add_name(obj, name);
    obj->path = path;
    digest_dynamic(obj, 0);
    dbg("%s valid_hash_sysv %d valid_hash_gnu %d dynsymcount %d", obj->path,
	obj->valid_hash_sysv, obj->valid_hash_gnu, obj->dynsymcount);
    if (obj->z_noopen && (flags & (RTLD_LO_DLOPEN | RTLD_LO_TRACE)) ==
      RTLD_LO_DLOPEN) {
	dbg("refusing to load non-loadable \"%s\"", obj->path);
	_rtld_error("Cannot dlopen non-loadable %s", obj->path);
	munmap(obj->mapbase, obj->mapsize);
	obj_free(obj);
	return (NULL);
    }

    *obj_tail = obj;
    obj_tail = &obj->next;
    obj_count++;
    obj_loads++;
    linkmap_add(obj);	/* for GDB & dlinfo() */
#if 0
    max_stack_flags |= obj->stack_flags;
#endif

    dbg("  %p .. %p: %s", obj->mapbase,
         obj->mapbase + obj->mapsize - 1, obj->path);
    if (obj->textrel)
	dbg("  WARNING: %s has impure text", obj->path);
    LD_UTRACE(UTRACE_LOAD_OBJECT, obj, obj->mapbase, obj->mapsize, 0,
	obj->path);

    return obj;
}

static Obj_Entry *
obj_from_addr(const void *addr)
{
    Obj_Entry *obj;

    for (obj = obj_list;  obj != NULL;  obj = obj->next) {
	if (addr < (void *) obj->mapbase)
	    continue;
	if (addr < (void *) (obj->mapbase + obj->mapsize))
	    return obj;
    }
    return NULL;
}

/*
 * If the main program is defined with a .preinit_array section, call
 * each function in order.  This must occur before the initialization
 * of any shared object or the main program.
 */
static void
preinit_main(void)
{
    Elf_Addr *preinit_addr;
    int index;

    preinit_addr = (Elf_Addr *)obj_main->preinit_array;
    if (preinit_addr == NULL)
	return;

    for (index = 0; index < obj_main->preinit_array_num; index++) {
	if (preinit_addr[index] != 0 && preinit_addr[index] != 1) {
	    dbg("calling preinit function for %s at %p", obj_main->path,
	      (void *)preinit_addr[index]);
	    LD_UTRACE(UTRACE_INIT_CALL, obj_main, (void *)preinit_addr[index],
	      0, 0, obj_main->path);
	    call_init_pointer(obj_main, preinit_addr[index]);
	}
    }
}

/*
 * Call the finalization functions for each of the objects in "list"
 * belonging to the DAG of "root" and referenced once. If NULL "root"
 * is specified, every finalization function will be called regardless
 * of the reference count and the list elements won't be freed. All of
 * the objects are expected to have non-NULL fini functions.
 */
static void
objlist_call_fini(Objlist *list, Obj_Entry *root, RtldLockState *lockstate)
{
    Objlist_Entry *elm;
    char *saved_msg;
    Elf_Addr *fini_addr;
    int index;

    assert(root == NULL || root->refcount == 1);

    /*
     * Preserve the current error message since a fini function might
     * call into the dynamic linker and overwrite it.
     */
    saved_msg = errmsg_save();
    do {
	STAILQ_FOREACH(elm, list, link) {
	    if (root != NULL && (elm->obj->refcount != 1 ||
	      objlist_find(&root->dagmembers, elm->obj) == NULL))
		continue;

	    /* Remove object from fini list to prevent recursive invocation. */
	    STAILQ_REMOVE(list, elm, Struct_Objlist_Entry, link);
	    /*
	     * XXX: If a dlopen() call references an object while the
	     * fini function is in progress, we might end up trying to
	     * unload the referenced object in dlclose() or the object
	     * won't be unloaded although its fini function has been
	     * called.
	     */
	    lock_release(rtld_bind_lock, lockstate);

	    /*
	     * It is legal to have both DT_FINI and DT_FINI_ARRAY defined.
	     * When this happens, DT_FINI_ARRAY is processed first.
	     * It is also processed backwards.  It is possible to encounter
	     * DT_FINI_ARRAY elements with values of 0 or 1, but they need
	     * to be ignored.
	     */
	    fini_addr = (Elf_Addr *)elm->obj->fini_array;
	    if (fini_addr != NULL && elm->obj->fini_array_num > 0) {
		for (index = elm->obj->fini_array_num - 1; index >= 0; index--) {
		    if (fini_addr[index] != 0 && fini_addr[index] != 1) {
			dbg("calling fini array function for %s at %p",
			    elm->obj->path, (void *)fini_addr[index]);
			LD_UTRACE(UTRACE_FINI_CALL, elm->obj,
			    (void *)fini_addr[index], 0, 0, elm->obj->path);
			call_initfini_pointer(elm->obj, fini_addr[index]);
		    }
		}
	    }
	    if (elm->obj->fini != (Elf_Addr)NULL) {
		dbg("calling fini function for %s at %p", elm->obj->path,
		    (void *)elm->obj->fini);
		LD_UTRACE(UTRACE_FINI_CALL, elm->obj, (void *)elm->obj->fini,
		    0, 0, elm->obj->path);
		call_initfini_pointer(elm->obj, elm->obj->fini);
	    }
	    wlock_acquire(rtld_bind_lock, lockstate);
	    /* No need to free anything if process is going down. */
	    if (root != NULL)
		free(elm);
	    /*
	     * We must restart the list traversal after every fini call
	     * because a dlclose() call from the fini function or from
	     * another thread might have modified the reference counts.
	     */
	    break;
	}
    } while (elm != NULL);
    errmsg_restore(saved_msg);
}

/*
 * Call the initialization functions for each of the objects in
 * "list".  All of the objects are expected to have non-NULL init
 * functions.
 */
static void
objlist_call_init(Objlist *list, RtldLockState *lockstate)
{
    Objlist_Entry *elm;
    Obj_Entry *obj;
    char *saved_msg;
    Elf_Addr *init_addr;
    int index;

    /*
     * Clean init_scanned flag so that objects can be rechecked and
     * possibly initialized earlier if any of vectors called below
     * cause the change by using dlopen.
     */
    for (obj = obj_list;  obj != NULL;  obj = obj->next)
	obj->init_scanned = false;

    /*
     * Preserve the current error message since an init function might
     * call into the dynamic linker and overwrite it.
     */
    saved_msg = errmsg_save();
    STAILQ_FOREACH(elm, list, link) {
	if (elm->obj->init_done) /* Initialized early. */
	    continue;

	/*
	 * Race: other thread might try to use this object before current
	 * one completes the initilization. Not much can be done here
	 * without better locking.
	 */
	elm->obj->init_done = true;
	lock_release(rtld_bind_lock, lockstate);

        /*
         * It is legal to have both DT_INIT and DT_INIT_ARRAY defined.
         * When this happens, DT_INIT is processed first.
	 * It is possible to encounter DT_INIT_ARRAY elements with values
	 * of 0 or 1, but they need to be ignored.
         */
	if (elm->obj->init != (Elf_Addr)NULL) {
	    dbg("calling init function for %s at %p", elm->obj->path,
	        (void *)elm->obj->init);
	    LD_UTRACE(UTRACE_INIT_CALL, elm->obj, (void *)elm->obj->init,
	        0, 0, elm->obj->path);
	    call_initfini_pointer(elm->obj, elm->obj->init);
	}
	init_addr = (Elf_Addr *)elm->obj->init_array;
	if (init_addr != NULL) {
	    for (index = 0; index < elm->obj->init_array_num; index++) {
		if (init_addr[index] != 0 && init_addr[index] != 1) {
		    dbg("calling init array function for %s at %p", elm->obj->path,
			(void *)init_addr[index]);
		    LD_UTRACE(UTRACE_INIT_CALL, elm->obj,
			(void *)init_addr[index], 0, 0, elm->obj->path);
		    call_init_pointer(elm->obj, init_addr[index]);
		}
	    }
	}
	wlock_acquire(rtld_bind_lock, lockstate);
    }
    errmsg_restore(saved_msg);
}

static void
objlist_clear(Objlist *list)
{
    Objlist_Entry *elm;

    while (!STAILQ_EMPTY(list)) {
	elm = STAILQ_FIRST(list);
	STAILQ_REMOVE_HEAD(list, link);
	free(elm);
    }
}

static Objlist_Entry *
objlist_find(Objlist *list, const Obj_Entry *obj)
{
    Objlist_Entry *elm;

    STAILQ_FOREACH(elm, list, link)
	if (elm->obj == obj)
	    return elm;
    return NULL;
}

static void
objlist_init(Objlist *list)
{
    STAILQ_INIT(list);
}

static void
objlist_push_head(Objlist *list, Obj_Entry *obj)
{
    Objlist_Entry *elm;

    elm = NEW(Objlist_Entry);
    elm->obj = obj;
    STAILQ_INSERT_HEAD(list, elm, link);
}

static void
objlist_push_tail(Objlist *list, Obj_Entry *obj)
{
    Objlist_Entry *elm;

    elm = NEW(Objlist_Entry);
    elm->obj = obj;
    STAILQ_INSERT_TAIL(list, elm, link);
}

static void
objlist_put_after(Objlist *list, Obj_Entry *listobj, Obj_Entry *obj)
{
	Objlist_Entry *elm, *listelm;

	STAILQ_FOREACH(listelm, list, link) {
		if (listelm->obj == listobj)
			break;
	}
	elm = NEW(Objlist_Entry);
	elm->obj = obj;
	if (listelm != NULL)
		STAILQ_INSERT_AFTER(list, listelm, elm, link);
	else
		STAILQ_INSERT_TAIL(list, elm, link);
}

static void
objlist_remove(Objlist *list, Obj_Entry *obj)
{
    Objlist_Entry *elm;

    if ((elm = objlist_find(list, obj)) != NULL) {
	STAILQ_REMOVE(list, elm, Struct_Objlist_Entry, link);
	free(elm);
    }
}

/*
 * Relocate dag rooted in the specified object.
 * Returns 0 on success, or -1 on failure.
 */

static int
relocate_object_dag(Obj_Entry *root, bool bind_now, Obj_Entry *rtldobj,
    int flags, RtldLockState *lockstate)
{
	Objlist_Entry *elm;
	int error;

	error = 0;
	STAILQ_FOREACH(elm, &root->dagmembers, link) {
		error = relocate_object(elm->obj, bind_now, rtldobj, flags,
		    lockstate);
		if (error == -1)
			break;
	}
	return (error);
}

/*
 * Prepare for, or clean after, relocating an object marked with
 * DT_TEXTREL or DF_TEXTREL.  Before relocating, all read-only
 * segments are remapped read-write.  After relocations are done, the
 * segment's permissions are returned back to the modes specified in
 * the phdrs.  If any relocation happened, or always for wired
 * program, COW is triggered.
 */
static int
reloc_textrel_prot(Obj_Entry *obj, bool before)
{
	const Elf_Phdr *ph;
	void *base;
	size_t l, sz;
	int prot;

	for (l = obj->phsize / sizeof(*ph), ph = obj->phdr; l > 0;
	    l--, ph++) {
		if (ph->p_type != PT_LOAD || (ph->p_flags & PF_W) != 0)
			continue;
		base = obj->relocbase + trunc_page(ph->p_vaddr);
		sz = round_page(ph->p_vaddr + ph->p_filesz) -
		    trunc_page(ph->p_vaddr);
		prot = convert_prot(ph->p_flags) | (before ? PROT_WRITE : 0);
	/*
	 * Make sure modified text segments are included in the
	 * core dump since we modified it.  This unfortunately causes the
	 * entire text segment to core-out but we don't have much of a
	 * choice.  We could try to only reenable core dumps on pages
	 * in which relocations occured but that is likely most of the text
	 * pages anyway, and even that would not work because the rest of
	 * the text pages would wind up as a read-only OBJT_DEFAULT object
	 * (created due to our modifications) backed by the original OBJT_VNODE
	 * object, and the ELF coredump code is currently only able to dump
	 * vnode records for pure vnode-backed mappings, not vnode backings
	 * to memory objects.
	 */
		if (before == false)
			madvise(base, sz, MADV_CORE);
		if (mprotect(base, sz, prot) == -1) {
			_rtld_error("%s: Cannot write-%sable text segment: %s",
			    obj->path, before ? "en" : "dis",
			    rtld_strerror(errno));
			return (-1);
		}
	}
	return (0);
}

/*
 * Relocate single object.
 * Returns 0 on success, or -1 on failure.
 */
static int
relocate_object(Obj_Entry *obj, bool bind_now, Obj_Entry *rtldobj,
    int flags, RtldLockState *lockstate)
{

	if (obj->relocated)
		return (0);
	obj->relocated = true;
	if (obj != rtldobj)
		dbg("relocating \"%s\"", obj->path);

	if (obj->symtab == NULL || obj->strtab == NULL ||
	    !(obj->valid_hash_sysv || obj->valid_hash_gnu)) {
		_rtld_error("%s: Shared object has no run-time symbol table",
			    obj->path);
		return (-1);
	}

	/* There are relocations to the write-protected text segment. */
	if (obj->textrel && reloc_textrel_prot(obj, true) != 0)
		return (-1);

	/* Process the non-PLT non-IFUNC relocations. */
	if (reloc_non_plt(obj, rtldobj, flags, lockstate))
		return (-1);

	/* Re-protected the text segment. */
	if (obj->textrel && reloc_textrel_prot(obj, false) != 0)
		return (-1);

	/* Set the special PLT or GOT entries. */
	init_pltgot(obj);

	/* Process the PLT relocations. */
	if (reloc_plt(obj) == -1)
		return (-1);
	/* Relocate the jump slots if we are doing immediate binding. */
	if (obj->bind_now || bind_now)
		if (reloc_jmpslots(obj, flags, lockstate) == -1)
			return (-1);

	/*
	 * Process the non-PLT IFUNC relocations.  The relocations are
	 * processed in two phases, because IFUNC resolvers may
	 * reference other symbols, which must be readily processed
	 * before resolvers are called.
	 */
	if (obj->non_plt_gnu_ifunc &&
	    reloc_non_plt(obj, rtldobj, flags | SYMLOOK_IFUNC, lockstate))
		return (-1);

	/*
	 * Set up the magic number and version in the Obj_Entry.  These
	 * were checked in the crt1.o from the original ElfKit, so we
	 * set them for backward compatibility.
	 */
	obj->magic = RTLD_MAGIC;
	obj->version = RTLD_VERSION;

	/*
	 * Set relocated data to read-only status if protection specified
	 */

	if (obj->relro_size) {
	    if (mprotect(obj->relro_page, obj->relro_size, PROT_READ) == -1) {
		_rtld_error("%s: Cannot enforce relro relocation: %s",
		  obj->path, rtld_strerror(errno));
		return (-1);
	    }
	    obj->relro_protected = true;
	}
	return (0);
}

/*
 * Relocate newly-loaded shared objects.  The argument is a pointer to
 * the Obj_Entry for the first such object.  All objects from the first
 * to the end of the list of objects are relocated.  Returns 0 on success,
 * or -1 on failure.
 */
static int
relocate_objects(Obj_Entry *first, bool bind_now, Obj_Entry *rtldobj,
    int flags, RtldLockState *lockstate)
{
	Obj_Entry *obj;
	int error;

	for (error = 0, obj = first;  obj != NULL;  obj = obj->next) {
		error = relocate_object(obj, bind_now, rtldobj, flags,
		    lockstate);
		if (error == -1)
			break;
	}
	return (error);
}

/*
 * The handling of R_MACHINE_IRELATIVE relocations and jumpslots
 * referencing STT_GNU_IFUNC symbols is postponed till the other
 * relocations are done.  The indirect functions specified as
 * ifunc are allowed to call other symbols, so we need to have
 * objects relocated before asking for resolution from indirects.
 *
 * The R_MACHINE_IRELATIVE slots are resolved in greedy fashion,
 * instead of the usual lazy handling of PLT slots.  It is
 * consistent with how GNU does it.
 */
static int
resolve_object_ifunc(Obj_Entry *obj, bool bind_now, int flags,
    RtldLockState *lockstate)
{
	if (obj->irelative && reloc_iresolve(obj, lockstate) == -1)
		return (-1);
	if (obj->irelative_nonplt && reloc_iresolve_nonplt(obj,
	    lockstate) == -1)
		return (-1);
	if ((obj->bind_now || bind_now) && obj->gnu_ifunc &&
	    reloc_gnu_ifunc(obj, flags, lockstate) == -1)
		return (-1);
	return (0);
}

static int
resolve_objects_ifunc(Obj_Entry *first, bool bind_now, int flags,
    RtldLockState *lockstate)
{
	Obj_Entry *obj;

	for (obj = first;  obj != NULL;  obj = obj->next) {
		if (resolve_object_ifunc(obj, bind_now, flags, lockstate) == -1)
			return (-1);
	}
	return (0);
}

static int
initlist_objects_ifunc(Objlist *list, bool bind_now, int flags,
    RtldLockState *lockstate)
{
	Objlist_Entry *elm;

	STAILQ_FOREACH(elm, list, link) {
		if (resolve_object_ifunc(elm->obj, bind_now, flags,
		    lockstate) == -1)
			return (-1);
	}
	return (0);
}

/*
 * Cleanup procedure.  It will be called (by the atexit mechanism) just
 * before the process exits.
 */
static void
rtld_exit(void)
{
    RtldLockState lockstate;

    wlock_acquire(rtld_bind_lock, &lockstate);
    dbg("rtld_exit()");
    objlist_call_fini(&list_fini, NULL, &lockstate);
    /* No need to remove the items from the list, since we are exiting. */
    if (!libmap_disable)
        lm_fini();
    lock_release(rtld_bind_lock, &lockstate);
}

/*
 * Iterate over a search path, translate each element, and invoke the
 * callback on the result.
 */
static void *
path_enumerate(const char *path, path_enum_proc callback, void *arg)
{
    const char *trans;
    if (path == NULL)
	return (NULL);

    path += strspn(path, ":;");
    while (*path != '\0') {
	size_t len;
	char  *res;

	len = strcspn(path, ":;");
	trans = lm_findn(NULL, path, len);
	if (trans)
	    res = callback(trans, strlen(trans), arg);
	else
	    res = callback(path, len, arg);

	if (res != NULL)
	    return (res);

	path += len;
	path += strspn(path, ":;");
    }

    return (NULL);
}

struct try_library_args {
    const char	*name;
    size_t	 namelen;
    char	*buffer;
    size_t	 buflen;
};

static void *
try_library_path(const char *dir, size_t dirlen, void *param)
{
    struct try_library_args *arg;

    arg = param;
    if (*dir == '/' || trust) {
	char *pathname;

	if (dirlen + 1 + arg->namelen + 1 > arg->buflen)
		return (NULL);

	pathname = arg->buffer;
	strncpy(pathname, dir, dirlen);
	pathname[dirlen] = '/';
	strcpy(pathname + dirlen + 1, arg->name);

	dbg("  Trying \"%s\"", pathname);
	if (access(pathname, F_OK) == 0) {		/* We found it */
	    pathname = xmalloc(dirlen + 1 + arg->namelen + 1);
	    strcpy(pathname, arg->buffer);
	    return (pathname);
	}
    }
    return (NULL);
}

static char *
search_library_path(const char *name, const char *path)
{
    char *p;
    struct try_library_args arg;

    if (path == NULL)
	return NULL;

    arg.name = name;
    arg.namelen = strlen(name);
    arg.buffer = xmalloc(PATH_MAX);
    arg.buflen = PATH_MAX;

    p = path_enumerate(path, try_library_path, &arg);

    free(arg.buffer);

    return (p);
}


/*
 * Finds the library with the given name using the directory descriptors
 * listed in the LD_LIBRARY_PATH_FDS environment variable.
 *
 * Returns a freshly-opened close-on-exec file descriptor for the library,
 * or -1 if the library cannot be found.
 */
static char *
search_library_pathfds(const char *name, const char *path, int *fdp)
{
	char *envcopy, *fdstr, *found, *last_token;
	size_t len;
	int dirfd, fd;

	dbg("%s('%s', '%s', fdp)", __func__, name, path);

	/* Don't load from user-specified libdirs into setuid binaries. */
	if (!trust)
		return (NULL);

	/* We can't do anything if LD_LIBRARY_PATH_FDS isn't set. */
	if (path == NULL)
		return (NULL);

	/* LD_LIBRARY_PATH_FDS only works with relative paths. */
	if (name[0] == '/') {
		dbg("Absolute path (%s) passed to %s", name, __func__);
		return (NULL);
	}

	/*
	 * Use strtok_r() to walk the FD:FD:FD list.  This requires a local
	 * copy of the path, as strtok_r rewrites separator tokens
	 * with '\0'.
	 *
	 * NOTE: strtok() uses a __thread static and cannot be used by rtld.
	 */
	found = NULL;
	envcopy = xstrdup(path);
	for (fdstr = strtok_r(envcopy, ":", &last_token); fdstr != NULL;
	    fdstr = strtok_r(NULL, ":", &last_token)) {
		dirfd = parse_libdir(fdstr);
		if (dirfd < 0)
			break;
		fd = openat(dirfd, name, O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			*fdp = fd;
			len = strlen(fdstr) + strlen(name) + 3;
			found = xmalloc(len);
			if (rtld_snprintf(found, len, "#%d/%s", dirfd, name) < 0) {
				_rtld_error("error generating '%d/%s'",
				    dirfd, name);
				die();
			}
			dbg("open('%s') => %d", found, fd);
			break;
		}
	}
	free(envcopy);

	return (found);
}


int
dlclose(void *handle)
{
    Obj_Entry *root;
    RtldLockState lockstate;

    wlock_acquire(rtld_bind_lock, &lockstate);
    root = dlcheck(handle);
    if (root == NULL) {
	lock_release(rtld_bind_lock, &lockstate);
	return -1;
    }
    LD_UTRACE(UTRACE_DLCLOSE_START, handle, NULL, 0, root->dl_refcount,
	root->path);

    /* Unreference the object and its dependencies. */
    root->dl_refcount--;

    if (root->refcount == 1) {
	/*
	 * The object will be no longer referenced, so we must unload it.
	 * First, call the fini functions.
	 */
	objlist_call_fini(&list_fini, root, &lockstate);

	unref_dag(root);

	/* Finish cleaning up the newly-unreferenced objects. */
	GDB_STATE(RT_DELETE,&root->linkmap);
	unload_object(root);
	GDB_STATE(RT_CONSISTENT,NULL);
    } else
	unref_dag(root);

    LD_UTRACE(UTRACE_DLCLOSE_STOP, handle, NULL, 0, 0, NULL);
    lock_release(rtld_bind_lock, &lockstate);
    return 0;
}

char *
dlerror(void)
{
    char *msg = error_message;
    error_message = NULL;
    return msg;
}

void *
dlopen(const char *name, int mode)
{

	return (rtld_dlopen(name, -1, mode));
}

void *
fdlopen(int fd, int mode)
{

	return (rtld_dlopen(NULL, fd, mode));
}

static void *
rtld_dlopen(const char *name, int fd, int mode)
{
    RtldLockState lockstate;
    int lo_flags;

    LD_UTRACE(UTRACE_DLOPEN_START, NULL, NULL, 0, mode, name);
    ld_tracing = (mode & RTLD_TRACE) == 0 ? NULL : "1";
    if (ld_tracing != NULL) {
	rlock_acquire(rtld_bind_lock, &lockstate);
	if (sigsetjmp(lockstate.env, 0) != 0)
	    lock_upgrade(rtld_bind_lock, &lockstate);
	environ = (char **)*get_program_var_addr("environ", &lockstate);
	lock_release(rtld_bind_lock, &lockstate);
    }
    lo_flags = RTLD_LO_DLOPEN;
    if (mode & RTLD_NODELETE)
	    lo_flags |= RTLD_LO_NODELETE;
    if (mode & RTLD_NOLOAD)
	    lo_flags |= RTLD_LO_NOLOAD;
    if (ld_tracing != NULL)
	    lo_flags |= RTLD_LO_TRACE;

    return (dlopen_object(name, fd, obj_main, lo_flags,
      mode & (RTLD_MODEMASK | RTLD_GLOBAL), NULL));
}

static void
dlopen_cleanup(Obj_Entry *obj)
{

	obj->dl_refcount--;
	unref_dag(obj);
	if (obj->refcount == 0)
		unload_object(obj);
}

static Obj_Entry *
dlopen_object(const char *name, int fd, Obj_Entry *refobj, int lo_flags,
    int mode, RtldLockState *lockstate)
{
    Obj_Entry **old_obj_tail;
    Obj_Entry *obj;
    Objlist initlist;
    RtldLockState mlockstate;
    int result;

    objlist_init(&initlist);

    if (lockstate == NULL && !(lo_flags & RTLD_LO_EARLY)) {
	wlock_acquire(rtld_bind_lock, &mlockstate);
	lockstate = &mlockstate;
    }
    GDB_STATE(RT_ADD,NULL);

    old_obj_tail = obj_tail;
    obj = NULL;
    if (name == NULL && fd == -1) {
	obj = obj_main;
	obj->refcount++;
    } else {
	obj = load_object(name, fd, refobj, lo_flags);
    }

    if (obj) {
	obj->dl_refcount++;
	if (mode & RTLD_GLOBAL && objlist_find(&list_global, obj) == NULL)
	    objlist_push_tail(&list_global, obj);
	if (*old_obj_tail != NULL) {		/* We loaded something new. */
	    assert(*old_obj_tail == obj);
	    if ((lo_flags & RTLD_LO_EARLY) == 0 && obj->static_tls &&
		!allocate_tls_offset(obj)) {
		    _rtld_error("%s: No space available "
				"for static TLS",
				obj->path);
		    result = -1;
	    } else {
		    result = 0;
	    }
	    if (result == 0) {
		result = load_needed_objects(
			    obj,
			    lo_flags & (RTLD_LO_DLOPEN | RTLD_LO_EARLY));
	    }
	    init_dag(obj);
	    ref_dag(obj);
	    if (result != -1)
		result = rtld_verify_versions(&obj->dagmembers);
	    if (result != -1 && ld_tracing)
		goto trace;
	    if (result == -1 || relocate_object_dag(obj,
	      (mode & RTLD_MODEMASK) == RTLD_NOW, &obj_rtld,
	      (lo_flags & RTLD_LO_EARLY) ? SYMLOOK_EARLY : 0,
	      lockstate) == -1) {
		dlopen_cleanup(obj);
		obj = NULL;
	    } else if (lo_flags & RTLD_LO_EARLY) {
		/*
		 * Do not call the init functions for early loaded
		 * filtees.  The image is still not initialized enough
		 * for them to work.
		 *
		 * Our object is found by the global object list and
		 * will be ordered among all init calls done right
		 * before transferring control to main.
		 */
	    } else {
		/* Make list of init functions to call. */
		initlist_add_objects(obj, &obj->next, &initlist);
	    }
	    /*
	     * Process all no_delete objects here, given them own
	     * DAGs to prevent their dependencies from being unloaded.
	     * This has to be done after we have loaded all of the
	     * dependencies, so that we do not miss any.
	     */
	    if (obj != NULL)
		process_nodelete(obj);
	} else {
	    /*
	     * Bump the reference counts for objects on this DAG.  If
	     * this is the first dlopen() call for the object that was
	     * already loaded as a dependency, initialize the dag
	     * starting at it.
	     */
	    init_dag(obj);
	    ref_dag(obj);

	    if ((lo_flags & RTLD_LO_TRACE) != 0)
		goto trace;
	}
	if (obj != NULL && ((lo_flags & RTLD_LO_NODELETE) != 0 ||
	  obj->z_nodelete) && !obj->ref_nodel) {
	    dbg("obj %s nodelete", obj->path);
	    ref_dag(obj);
	    obj->z_nodelete = obj->ref_nodel = true;
	}
    }

    LD_UTRACE(UTRACE_DLOPEN_STOP, obj, NULL, 0, obj ? obj->dl_refcount : 0,
	name);
    GDB_STATE(RT_CONSISTENT,obj ? &obj->linkmap : NULL);

    if ((lo_flags & RTLD_LO_EARLY) == 0) {
	map_stacks_exec(lockstate);
	if (obj)
	    distribute_static_tls(&initlist, lockstate);
    }

    if (initlist_objects_ifunc(&initlist, (mode & RTLD_MODEMASK) == RTLD_NOW,
      (lo_flags & RTLD_LO_EARLY) ? SYMLOOK_EARLY : 0,
      lockstate) == -1) {
	objlist_clear(&initlist);
	dlopen_cleanup(obj);
	if (lockstate == &mlockstate)
	    lock_release(rtld_bind_lock, lockstate);
	return (NULL);
    }

    if (!(lo_flags & RTLD_LO_EARLY)) {
	/* Call the init functions. */
	objlist_call_init(&initlist, lockstate);
    }
    objlist_clear(&initlist);
    if (lockstate == &mlockstate)
	lock_release(rtld_bind_lock, lockstate);
    return obj;
trace:
    trace_loaded_objects(obj);
    if (lockstate == &mlockstate)
	lock_release(rtld_bind_lock, lockstate);
    exit(0);
}

static void *
do_dlsym(void *handle, const char *name, void *retaddr, const Ver_Entry *ve,
    int flags)
{
    DoneList donelist;
    const Obj_Entry *obj, *defobj;
    const Elf_Sym *def;
    SymLook req;
    RtldLockState lockstate;
    tls_index ti;
    int res;

    def = NULL;
    defobj = NULL;
    symlook_init(&req, name);
    req.ventry = ve;
    req.flags = flags | SYMLOOK_IN_PLT;
    req.lockstate = &lockstate;

    rlock_acquire(rtld_bind_lock, &lockstate);
    if (sigsetjmp(lockstate.env, 0) != 0)
	    lock_upgrade(rtld_bind_lock, &lockstate);
    if (handle == NULL || handle == RTLD_NEXT ||
	handle == RTLD_DEFAULT || handle == RTLD_SELF ||
	handle == RTLD_ALL) {

	if (handle != RTLD_ALL) {
		if ((obj = obj_from_addr(retaddr)) == NULL) {
		    _rtld_error("Cannot determine caller's shared object");
		    lock_release(rtld_bind_lock, &lockstate);
		    return NULL;
		}
	} else {
		obj = obj_list;
	}
	if (handle == NULL) {	/* Just the caller's shared object. */
	    res = symlook_obj(&req, obj);
	    if (res == 0) {
		def = req.sym_out;
		defobj = req.defobj_out;
	    }
	} else if (handle == RTLD_NEXT || /* Objects after caller's */
		   handle == RTLD_SELF || /* ... caller included */
		   handle == RTLD_ALL) {  /* All Objects */
	    if (handle == RTLD_NEXT)
		obj = obj->next;
	    for (; obj != NULL; obj = obj->next) {
		res = symlook_obj(&req, obj);
		if (res == 0) {
		    if (def == NULL ||
		      ELF_ST_BIND(req.sym_out->st_info) != STB_WEAK) {
			def = req.sym_out;
			defobj = req.defobj_out;
			if (ELF_ST_BIND(def->st_info) != STB_WEAK)
			    break;
		    }
		}
	    }
	    /*
	     * Search the dynamic linker itself, and possibly resolve the
	     * symbol from there.  This is how the application links to
	     * dynamic linker services such as dlopen.
	     */
	    if (def == NULL || ELF_ST_BIND(def->st_info) == STB_WEAK) {
		res = symlook_obj(&req, &obj_rtld);
		if (res == 0) {
		    def = req.sym_out;
		    defobj = req.defobj_out;
		}
	    }
	} else {
	    assert(handle == RTLD_DEFAULT);
	    res = symlook_default(&req, obj);
	    if (res == 0) {
		defobj = req.defobj_out;
		def = req.sym_out;
	    }
	}
    } else {
	if ((obj = dlcheck(handle)) == NULL) {
	    lock_release(rtld_bind_lock, &lockstate);
	    return NULL;
	}

	donelist_init(&donelist);
	if (obj->mainprog) {
            /* Handle obtained by dlopen(NULL, ...) implies global scope. */
	    res = symlook_global(&req, &donelist);
	    if (res == 0) {
		def = req.sym_out;
		defobj = req.defobj_out;
	    }
	    /*
	     * Search the dynamic linker itself, and possibly resolve the
	     * symbol from there.  This is how the application links to
	     * dynamic linker services such as dlopen.
	     */
	    if (def == NULL || ELF_ST_BIND(def->st_info) == STB_WEAK) {
		res = symlook_obj(&req, &obj_rtld);
		if (res == 0) {
		    def = req.sym_out;
		    defobj = req.defobj_out;
		}
	    }
	}
	else {
	    /* Search the whole DAG rooted at the given object. */
	    res = symlook_list(&req, &obj->dagmembers, &donelist);
	    if (res == 0) {
		def = req.sym_out;
		defobj = req.defobj_out;
	    }
	}
    }

    if (def != NULL) {
	lock_release(rtld_bind_lock, &lockstate);

	/*
	 * The value required by the caller is derived from the value
	 * of the symbol. this is simply the relocated value of the
	 * symbol.
	 */
	if (ELF_ST_TYPE(def->st_info) == STT_FUNC)
	    return (make_function_pointer(def, defobj));
	else if (ELF_ST_TYPE(def->st_info) == STT_GNU_IFUNC)
	    return (rtld_resolve_ifunc(defobj, def));
	else if (ELF_ST_TYPE(def->st_info) == STT_TLS) {
	    ti.ti_module = defobj->tlsindex;
	    ti.ti_offset = def->st_value;
	    return (__tls_get_addr(&ti));
	} else
	    return (defobj->relocbase + def->st_value);
    }

    _rtld_error("Undefined symbol \"%s\"", name);
    lock_release(rtld_bind_lock, &lockstate);
    return NULL;
}

void *
dlsym(void *handle, const char *name)
{
	return do_dlsym(handle, name, __builtin_return_address(0), NULL,
	    SYMLOOK_DLSYM);
}

dlfunc_t
dlfunc(void *handle, const char *name)
{
	union {
		void *d;
		dlfunc_t f;
	} rv;

	rv.d = do_dlsym(handle, name, __builtin_return_address(0), NULL,
	    SYMLOOK_DLSYM);
	return (rv.f);
}

void *
dlvsym(void *handle, const char *name, const char *version)
{
	Ver_Entry ventry;

	ventry.name = version;
	ventry.file = NULL;
	ventry.hash = elf_hash(version);
	ventry.flags= 0;
	return do_dlsym(handle, name, __builtin_return_address(0), &ventry,
	    SYMLOOK_DLSYM);
}

int
_rtld_addr_phdr(const void *addr, struct dl_phdr_info *phdr_info)
{
    const Obj_Entry *obj;
    RtldLockState lockstate;

    rlock_acquire(rtld_bind_lock, &lockstate);
    obj = obj_from_addr(addr);
    if (obj == NULL) {
        _rtld_error("No shared object contains address");
	lock_release(rtld_bind_lock, &lockstate);
        return (0);
    }
    rtld_fill_dl_phdr_info(obj, phdr_info);
    lock_release(rtld_bind_lock, &lockstate);
    return (1);
}

int
dladdr(const void *addr, Dl_info *info)
{
    const Obj_Entry *obj;
    const Elf_Sym *def;
    void *symbol_addr;
    unsigned long symoffset;
    RtldLockState lockstate;

    rlock_acquire(rtld_bind_lock, &lockstate);
    obj = obj_from_addr(addr);
    if (obj == NULL) {
        _rtld_error("No shared object contains address");
	lock_release(rtld_bind_lock, &lockstate);
        return 0;
    }
    info->dli_fname = obj->path;
    info->dli_fbase = obj->mapbase;
    info->dli_saddr = NULL;
    info->dli_sname = NULL;

    /*
     * Walk the symbol list looking for the symbol whose address is
     * closest to the address sent in.
     */
    for (symoffset = 0; symoffset < obj->dynsymcount; symoffset++) {
        def = obj->symtab + symoffset;

        /*
         * For skip the symbol if st_shndx is either SHN_UNDEF or
         * SHN_COMMON.
         */
        if (def->st_shndx == SHN_UNDEF || def->st_shndx == SHN_COMMON)
            continue;

        /*
         * If the symbol is greater than the specified address, or if it
         * is further away from addr than the current nearest symbol,
         * then reject it.
         */
        symbol_addr = obj->relocbase + def->st_value;
        if (symbol_addr > addr || symbol_addr < info->dli_saddr)
            continue;

        /* Update our idea of the nearest symbol. */
        info->dli_sname = obj->strtab + def->st_name;
        info->dli_saddr = symbol_addr;

        /* Exact match? */
        if (info->dli_saddr == addr)
            break;
    }
    lock_release(rtld_bind_lock, &lockstate);
    return 1;
}

int
dlinfo(void *handle, int request, void *p)
{
    const Obj_Entry *obj;
    RtldLockState lockstate;
    int error;

    rlock_acquire(rtld_bind_lock, &lockstate);

    if (handle == NULL || handle == RTLD_SELF) {
	void *retaddr;

	retaddr = __builtin_return_address(0);	/* __GNUC__ only */
	if ((obj = obj_from_addr(retaddr)) == NULL)
	    _rtld_error("Cannot determine caller's shared object");
    } else
	obj = dlcheck(handle);

    if (obj == NULL) {
	lock_release(rtld_bind_lock, &lockstate);
	return (-1);
    }

    error = 0;
    switch (request) {
    case RTLD_DI_LINKMAP:
	*((struct link_map const **)p) = &obj->linkmap;
	break;
    case RTLD_DI_ORIGIN:
	error = rtld_dirname(obj->path, p);
	break;

    case RTLD_DI_SERINFOSIZE:
    case RTLD_DI_SERINFO:
	error = do_search_info(obj, request, (struct dl_serinfo *)p);
	break;

    default:
	_rtld_error("Invalid request %d passed to dlinfo()", request);
	error = -1;
    }

    lock_release(rtld_bind_lock, &lockstate);

    return (error);
}

static void
rtld_fill_dl_phdr_info(const Obj_Entry *obj, struct dl_phdr_info *phdr_info)
{

	phdr_info->dlpi_addr = (Elf_Addr)obj->relocbase;
	phdr_info->dlpi_name = obj->path;
	phdr_info->dlpi_phdr = obj->phdr;
	phdr_info->dlpi_phnum = obj->phsize / sizeof(obj->phdr[0]);
	phdr_info->dlpi_tls_modid = obj->tlsindex;
	phdr_info->dlpi_tls_data = obj->tlsinit;
	phdr_info->dlpi_adds = obj_loads;
	phdr_info->dlpi_subs = obj_loads - obj_count;
}

int
dl_iterate_phdr(__dl_iterate_hdr_callback callback, void *param)
{
    struct dl_phdr_info phdr_info;
    const Obj_Entry *obj;
    RtldLockState bind_lockstate, phdr_lockstate;
    int error;

    wlock_acquire(rtld_phdr_lock, &phdr_lockstate);
    rlock_acquire(rtld_bind_lock, &bind_lockstate);

    error = 0;

    for (obj = obj_list;  obj != NULL;  obj = obj->next) {
	rtld_fill_dl_phdr_info(obj, &phdr_info);
	if ((error = callback(&phdr_info, sizeof phdr_info, param)) != 0)
		break;

    }
    if (error == 0) {
	rtld_fill_dl_phdr_info(&obj_rtld, &phdr_info);
	error = callback(&phdr_info, sizeof(phdr_info), param);
    }

    lock_release(rtld_bind_lock, &bind_lockstate);
    lock_release(rtld_phdr_lock, &phdr_lockstate);

    return (error);
}

static void *
fill_search_info(const char *dir, size_t dirlen, void *param)
{
    struct fill_search_info_args *arg;

    arg = param;

    if (arg->request == RTLD_DI_SERINFOSIZE) {
	arg->serinfo->dls_cnt ++;
	arg->serinfo->dls_size += sizeof(struct dl_serpath) + dirlen + 1;
    } else {
	struct dl_serpath *s_entry;

	s_entry = arg->serpath;
	s_entry->dls_name  = arg->strspace;
	s_entry->dls_flags = arg->flags;

	strncpy(arg->strspace, dir, dirlen);
	arg->strspace[dirlen] = '\0';

	arg->strspace += dirlen + 1;
	arg->serpath++;
    }

    return (NULL);
}

static int
do_search_info(const Obj_Entry *obj, int request, struct dl_serinfo *info)
{
    struct dl_serinfo _info;
    struct fill_search_info_args args;

    args.request = RTLD_DI_SERINFOSIZE;
    args.serinfo = &_info;

    _info.dls_size = __offsetof(struct dl_serinfo, dls_serpath);
    _info.dls_cnt  = 0;

    path_enumerate(obj->rpath, fill_search_info, &args);
    path_enumerate(ld_library_path, fill_search_info, &args);
    path_enumerate(obj->runpath, fill_search_info, &args);
    path_enumerate(gethints(obj->z_nodeflib), fill_search_info, &args);
    if (!obj->z_nodeflib)
      path_enumerate(STANDARD_LIBRARY_PATH, fill_search_info, &args);


    if (request == RTLD_DI_SERINFOSIZE) {
	info->dls_size = _info.dls_size;
	info->dls_cnt = _info.dls_cnt;
	return (0);
    }

    if (info->dls_cnt != _info.dls_cnt || info->dls_size != _info.dls_size) {
	_rtld_error("Uninitialized Dl_serinfo struct passed to dlinfo()");
	return (-1);
    }

    args.request  = RTLD_DI_SERINFO;
    args.serinfo  = info;
    args.serpath  = &info->dls_serpath[0];
    args.strspace = (char *)&info->dls_serpath[_info.dls_cnt];

    args.flags = LA_SER_RUNPATH;
    if (path_enumerate(obj->rpath, fill_search_info, &args) != NULL)
	return (-1);

    args.flags = LA_SER_LIBPATH;
    if (path_enumerate(ld_library_path, fill_search_info, &args) != NULL)
	return (-1);

    args.flags = LA_SER_RUNPATH;
    if (path_enumerate(obj->runpath, fill_search_info, &args) != NULL)
	return (-1);

    args.flags = LA_SER_CONFIG;
    if (path_enumerate(gethints(obj->z_nodeflib), fill_search_info, &args)
      != NULL)
	return (-1);

    args.flags = LA_SER_DEFAULT;
    if (!obj->z_nodeflib &&
      path_enumerate(STANDARD_LIBRARY_PATH, fill_search_info, &args) != NULL)
	return (-1);
    return (0);
}

static int
rtld_dirname(const char *path, char *bname)
{
    const char *endp;

    /* Empty or NULL string gets treated as "." */
    if (path == NULL || *path == '\0') {
	bname[0] = '.';
	bname[1] = '\0';
	return (0);
    }

    /* Strip trailing slashes */
    endp = path + strlen(path) - 1;
    while (endp > path && *endp == '/')
	endp--;

    /* Find the start of the dir */
    while (endp > path && *endp != '/')
	endp--;

    /* Either the dir is "/" or there are no slashes */
    if (endp == path) {
	bname[0] = *endp == '/' ? '/' : '.';
	bname[1] = '\0';
	return (0);
    } else {
	do {
	    endp--;
	} while (endp > path && *endp == '/');
    }

    if (endp - path + 2 > PATH_MAX)
    {
	_rtld_error("Filename is too long: %s", path);
	return(-1);
    }

    strncpy(bname, path, endp - path + 1);
    bname[endp - path + 1] = '\0';
    return (0);
}

static int
rtld_dirname_abs(const char *path, char *base)
{
	char base_rel[PATH_MAX];

	if (rtld_dirname(path, base) == -1)
		return (-1);
	if (base[0] == '/')
		return (0);
	if (getcwd(base_rel, sizeof(base_rel)) == NULL ||
	    strlcat(base_rel, "/", sizeof(base_rel)) >= sizeof(base_rel) ||
	    strlcat(base_rel, base, sizeof(base_rel)) >= sizeof(base_rel))
		return (-1);
	strcpy(base, base_rel);
	return (0);
}

static void
linkmap_add(Obj_Entry *obj)
{
    struct link_map *l = &obj->linkmap;
    struct link_map *prev;

    obj->linkmap.l_name = obj->path;
    obj->linkmap.l_addr = obj->mapbase;
    obj->linkmap.l_ld = obj->dynamic;

    if (r_debug.r_map == NULL) {
	r_debug.r_map = l;
	return;
    }

    /*
     * Scan to the end of the list, but not past the entry for the
     * dynamic linker, which we want to keep at the very end.
     */
    for (prev = r_debug.r_map;
      prev->l_next != NULL && prev->l_next != &obj_rtld.linkmap;
      prev = prev->l_next)
	;

    /* Link in the new entry. */
    l->l_prev = prev;
    l->l_next = prev->l_next;
    if (l->l_next != NULL)
	l->l_next->l_prev = l;
    prev->l_next = l;
}

static void
linkmap_delete(Obj_Entry *obj)
{
    struct link_map *l = &obj->linkmap;

    if (l->l_prev == NULL) {
	if ((r_debug.r_map = l->l_next) != NULL)
	    l->l_next->l_prev = NULL;
	return;
    }

    if ((l->l_prev->l_next = l->l_next) != NULL)
	l->l_next->l_prev = l->l_prev;
}

/*
 * Function for the debugger to set a breakpoint on to gain control.
 *
 * The two parameters allow the debugger to easily find and determine
 * what the runtime loader is doing and to whom it is doing it.
 *
 * When the loadhook trap is hit (r_debug_state, set at program
 * initialization), the arguments can be found on the stack:
 *
 *  +8   struct link_map *m
 *  +4   struct r_debug  *rd
 *  +0   RetAddr
 */
void
r_debug_state(struct r_debug* rd, struct link_map *m)
{
    /*
     * The following is a hack to force the compiler to emit calls to
     * this function, even when optimizing.  If the function is empty,
     * the compiler is not obliged to emit any code for calls to it,
     * even when marked __noinline.  However, gdb depends on those
     * calls being made.
     */
    __asm __volatile("" : : : "memory");
}

/*
 * A function called after init routines have completed. This can be used to
 * break before a program's entry routine is called, and can be used when
 * main is not available in the symbol table.
 */
void
_r_debug_postinit(struct link_map *m)
{

	/* See r_debug_state(). */
	__asm __volatile("" : : : "memory");
}

/*
 * Get address of the pointer variable in the main program.
 * Prefer non-weak symbol over the weak one.
 */
static const void **
get_program_var_addr(const char *name, RtldLockState *lockstate)
{
    SymLook req;
    DoneList donelist;

    symlook_init(&req, name);
    req.lockstate = lockstate;
    donelist_init(&donelist);
    if (symlook_global(&req, &donelist) != 0)
	return (NULL);
    if (ELF_ST_TYPE(req.sym_out->st_info) == STT_FUNC)
	return ((const void **)make_function_pointer(req.sym_out,
	  req.defobj_out));
    else if (ELF_ST_TYPE(req.sym_out->st_info) == STT_GNU_IFUNC)
	return ((const void **)rtld_resolve_ifunc(req.defobj_out, req.sym_out));
    else
	return ((const void **)(req.defobj_out->relocbase +
	  req.sym_out->st_value));
}

/*
 * Set a pointer variable in the main program to the given value.  This
 * is used to set key variables such as "environ" before any of the
 * init functions are called.
 */
static void
set_program_var(const char *name, const void *value)
{
    const void **addr;

    if ((addr = get_program_var_addr(name, NULL)) != NULL) {
	dbg("\"%s\": *%p <-- %p", name, addr, value);
	*addr = value;
    }
}

/*
 * Search the global objects, including dependencies and main object,
 * for the given symbol.
 */
static int
symlook_global(SymLook *req, DoneList *donelist)
{
    SymLook req1;
    const Objlist_Entry *elm;
    int res;

    symlook_init_from_req(&req1, req);

    /* Search all objects loaded at program start up. */
    if (req->defobj_out == NULL ||
      ELF_ST_BIND(req->sym_out->st_info) == STB_WEAK) {
	res = symlook_list(&req1, &list_main, donelist);
	if (res == 0 && (req->defobj_out == NULL ||
	  ELF_ST_BIND(req1.sym_out->st_info) != STB_WEAK)) {
	    req->sym_out = req1.sym_out;
	    req->defobj_out = req1.defobj_out;
	    assert(req->defobj_out != NULL);
	}
    }

    /* Search all DAGs whose roots are RTLD_GLOBAL objects. */
    STAILQ_FOREACH(elm, &list_global, link) {
	if (req->defobj_out != NULL &&
	  ELF_ST_BIND(req->sym_out->st_info) != STB_WEAK)
	    break;
	res = symlook_list(&req1, &elm->obj->dagmembers, donelist);
	if (res == 0 && (req->defobj_out == NULL ||
	  ELF_ST_BIND(req1.sym_out->st_info) != STB_WEAK)) {
	    req->sym_out = req1.sym_out;
	    req->defobj_out = req1.defobj_out;
	    assert(req->defobj_out != NULL);
	}
    }

    return (req->sym_out != NULL ? 0 : ESRCH);
}

/*
 * This is a special version of getenv which is far more efficient
 * at finding LD_ environment vars.
 */
static
const char *
_getenv_ld(const char *id)
{
    const char *envp;
    int i, j;
    int idlen = strlen(id);

    if (ld_index == LD_ARY_CACHE)
	return(getenv(id));
    if (ld_index == 0) {
	for (i = j = 0; (envp = environ[i]) != NULL && j < LD_ARY_CACHE; ++i) {
	    if (envp[0] == 'L' && envp[1] == 'D' && envp[2] == '_')
		ld_ary[j++] = envp;
	}
	if (j == 0)
		ld_ary[j++] = "";
	ld_index = j;
    }
    for (i = ld_index - 1; i >= 0; --i) {
	if (strncmp(ld_ary[i], id, idlen) == 0 && ld_ary[i][idlen] == '=')
	    return(ld_ary[i] + idlen + 1);
    }
    return(NULL);
}

/*
 * Given a symbol name in a referencing object, find the corresponding
 * definition of the symbol.  Returns a pointer to the symbol, or NULL if
 * no definition was found.  Returns a pointer to the Obj_Entry of the
 * defining object via the reference parameter DEFOBJ_OUT.
 */
static int
symlook_default(SymLook *req, const Obj_Entry *refobj)
{
    DoneList donelist;
    const Objlist_Entry *elm;
    SymLook req1;
    int res;

    donelist_init(&donelist);
    symlook_init_from_req(&req1, req);

    /* Look first in the referencing object if linked symbolically. */
    if (refobj->symbolic && !donelist_check(&donelist, refobj)) {
	res = symlook_obj(&req1, refobj);
	if (res == 0) {
	    req->sym_out = req1.sym_out;
	    req->defobj_out = req1.defobj_out;
	    assert(req->defobj_out != NULL);
	}
    }

    symlook_global(req, &donelist);

    /* Search all dlopened DAGs containing the referencing object. */
    STAILQ_FOREACH(elm, &refobj->dldags, link) {
	if (req->sym_out != NULL &&
	  ELF_ST_BIND(req->sym_out->st_info) != STB_WEAK)
	    break;
	res = symlook_list(&req1, &elm->obj->dagmembers, &donelist);
	if (res == 0 && (req->sym_out == NULL ||
	  ELF_ST_BIND(req1.sym_out->st_info) != STB_WEAK)) {
	    req->sym_out = req1.sym_out;
	    req->defobj_out = req1.defobj_out;
	    assert(req->defobj_out != NULL);
	}
    }

    /*
     * Search the dynamic linker itself, and possibly resolve the
     * symbol from there.  This is how the application links to
     * dynamic linker services such as dlopen.
     */
    if (req->sym_out == NULL ||
      ELF_ST_BIND(req->sym_out->st_info) == STB_WEAK) {
	res = symlook_obj(&req1, &obj_rtld);
	if (res == 0) {
	    req->sym_out = req1.sym_out;
	    req->defobj_out = req1.defobj_out;
	    assert(req->defobj_out != NULL);
	}
    }

    return (req->sym_out != NULL ? 0 : ESRCH);
}

static int
symlook_list(SymLook *req, const Objlist *objlist, DoneList *dlp)
{
    const Elf_Sym *def;
    const Obj_Entry *defobj;
    const Objlist_Entry *elm;
    SymLook req1;
    int res;

    def = NULL;
    defobj = NULL;
    STAILQ_FOREACH(elm, objlist, link) {
	if (donelist_check(dlp, elm->obj))
	    continue;
	symlook_init_from_req(&req1, req);
	if ((res = symlook_obj(&req1, elm->obj)) == 0) {
	    if (def == NULL || ELF_ST_BIND(req1.sym_out->st_info) != STB_WEAK) {
		def = req1.sym_out;
		defobj = req1.defobj_out;
		if (ELF_ST_BIND(def->st_info) != STB_WEAK)
		    break;
	    }
	}
    }
    if (def != NULL) {
	req->sym_out = def;
	req->defobj_out = defobj;
	return (0);
    }
    return (ESRCH);
}

/*
 * Search the chain of DAGS cointed to by the given Needed_Entry
 * for a symbol of the given name.  Each DAG is scanned completely
 * before advancing to the next one.  Returns a pointer to the symbol,
 * or NULL if no definition was found.
 */
static int
symlook_needed(SymLook *req, const Needed_Entry *needed, DoneList *dlp)
{
    const Elf_Sym *def;
    const Needed_Entry *n;
    const Obj_Entry *defobj;
    SymLook req1;
    int res;

    def = NULL;
    defobj = NULL;
    symlook_init_from_req(&req1, req);
    for (n = needed; n != NULL; n = n->next) {
	if (n->obj == NULL ||
	    (res = symlook_list(&req1, &n->obj->dagmembers, dlp)) != 0)
	    continue;
	if (def == NULL || ELF_ST_BIND(req1.sym_out->st_info) != STB_WEAK) {
	    def = req1.sym_out;
	    defobj = req1.defobj_out;
	    if (ELF_ST_BIND(def->st_info) != STB_WEAK)
		break;
	}
    }
    if (def != NULL) {
	req->sym_out = def;
	req->defobj_out = defobj;
	return (0);
    }
    return (ESRCH);
}

/*
 * Search the symbol table of a single shared object for a symbol of
 * the given name and version, if requested.  Returns a pointer to the
 * symbol, or NULL if no definition was found.  If the object is
 * filter, return filtered symbol from filtee.
 *
 * The symbol's hash value is passed in for efficiency reasons; that
 * eliminates many recomputations of the hash value.
 */
int
symlook_obj(SymLook *req, const Obj_Entry *obj)
{
    DoneList donelist;
    SymLook req1;
    int flags, res, mres;

    /*
     * If there is at least one valid hash at this point, we prefer to
     * use the faster GNU version if available.
     */
    if (obj->valid_hash_gnu)
	mres = symlook_obj1_gnu(req, obj);
    else if (obj->valid_hash_sysv)
	mres = symlook_obj1_sysv(req, obj);
    else
	return (EINVAL);

    if (mres == 0) {
	if (obj->needed_filtees != NULL) {
	    flags = (req->flags & SYMLOOK_EARLY) ? RTLD_LO_EARLY : 0;
	    load_filtees(__DECONST(Obj_Entry *, obj), flags, req->lockstate);
	    donelist_init(&donelist);
	    symlook_init_from_req(&req1, req);
	    res = symlook_needed(&req1, obj->needed_filtees, &donelist);
	    if (res == 0) {
		req->sym_out = req1.sym_out;
		req->defobj_out = req1.defobj_out;
	    }
	    return (res);
	}
	if (obj->needed_aux_filtees != NULL) {
	    flags = (req->flags & SYMLOOK_EARLY) ? RTLD_LO_EARLY : 0;
	    load_filtees(__DECONST(Obj_Entry *, obj), flags, req->lockstate);
	    donelist_init(&donelist);
	    symlook_init_from_req(&req1, req);
	    res = symlook_needed(&req1, obj->needed_aux_filtees, &donelist);
	    if (res == 0) {
		req->sym_out = req1.sym_out;
		req->defobj_out = req1.defobj_out;
		return (res);
	    }
	}
    }
    return (mres);
}

/* Symbol match routine common to both hash functions */
static bool
matched_symbol(SymLook *req, const Obj_Entry *obj, Sym_Match_Result *result,
    const unsigned long symnum)
{
	Elf_Versym verndx;
	const Elf_Sym *symp;
	const char *strp;

	symp = obj->symtab + symnum;
	strp = obj->strtab + symp->st_name;

	switch (ELF_ST_TYPE(symp->st_info)) {
	case STT_FUNC:
	case STT_NOTYPE:
	case STT_OBJECT:
	case STT_COMMON:
	case STT_GNU_IFUNC:
		if (symp->st_value == 0)
			return (false);
		/* fallthrough */
	case STT_TLS:
		if (symp->st_shndx != SHN_UNDEF)
			break;
		else if (((req->flags & SYMLOOK_IN_PLT) == 0) &&
		    (ELF_ST_TYPE(symp->st_info) == STT_FUNC))
			break;
		/* fallthrough */
	default:
		return (false);
	}
	if (strcmp(req->name, strp) != 0)
		return (false);

	if (req->ventry == NULL) {
		if (obj->versyms != NULL) {
			verndx = VER_NDX(obj->versyms[symnum]);
			if (verndx > obj->vernum) {
				_rtld_error(
				    "%s: symbol %s references wrong version %d",
				    obj->path, obj->strtab + symnum, verndx);
				return (false);
			}
			/*
			 * If we are not called from dlsym (i.e. this
			 * is a normal relocation from unversioned
			 * binary), accept the symbol immediately if
			 * it happens to have first version after this
			 * shared object became versioned.  Otherwise,
			 * if symbol is versioned and not hidden,
			 * remember it. If it is the only symbol with
			 * this name exported by the shared object, it
			 * will be returned as a match by the calling
			 * function. If symbol is global (verndx < 2)
			 * accept it unconditionally.
			 */
			if ((req->flags & SYMLOOK_DLSYM) == 0 &&
			    verndx == VER_NDX_GIVEN) {
				result->sym_out = symp;
				return (true);
			}
			else if (verndx >= VER_NDX_GIVEN) {
				if ((obj->versyms[symnum] & VER_NDX_HIDDEN)
				    == 0) {
					if (result->vsymp == NULL)
						result->vsymp = symp;
					result->vcount++;
				}
				return (false);
			}
		}
		result->sym_out = symp;
		return (true);
	}
	if (obj->versyms == NULL) {
		if (object_match_name(obj, req->ventry->name)) {
			_rtld_error("%s: object %s should provide version %s "
			    "for symbol %s", obj_rtld.path, obj->path,
			    req->ventry->name, obj->strtab + symnum);
			return (false);
		}
	} else {
		verndx = VER_NDX(obj->versyms[symnum]);
		if (verndx > obj->vernum) {
			_rtld_error("%s: symbol %s references wrong version %d",
			    obj->path, obj->strtab + symnum, verndx);
			return (false);
		}
		if (obj->vertab[verndx].hash != req->ventry->hash ||
		    strcmp(obj->vertab[verndx].name, req->ventry->name)) {
			/*
			 * Version does not match. Look if this is a
			 * global symbol and if it is not hidden. If
			 * global symbol (verndx < 2) is available,
			 * use it. Do not return symbol if we are
			 * called by dlvsym, because dlvsym looks for
			 * a specific version and default one is not
			 * what dlvsym wants.
			 */
			if ((req->flags & SYMLOOK_DLSYM) ||
			    (verndx >= VER_NDX_GIVEN) ||
			    (obj->versyms[symnum] & VER_NDX_HIDDEN))
				return (false);
		}
	}
	result->sym_out = symp;
	return (true);
}

/*
 * Search for symbol using SysV hash function.
 * obj->buckets is known not to be NULL at this point; the test for this was
 * performed with the obj->valid_hash_sysv assignment.
 */
static int
symlook_obj1_sysv(SymLook *req, const Obj_Entry *obj)
{
	unsigned long symnum;
	Sym_Match_Result matchres;

	matchres.sym_out = NULL;
	matchres.vsymp = NULL;
	matchres.vcount = 0;

	for (symnum = obj->buckets[req->hash % obj->nbuckets];
	    symnum != STN_UNDEF; symnum = obj->chains[symnum]) {
		if (symnum >= obj->nchains)
			return (ESRCH);	/* Bad object */

		if (matched_symbol(req, obj, &matchres, symnum)) {
			req->sym_out = matchres.sym_out;
			req->defobj_out = obj;
			return (0);
		}
	}
	if (matchres.vcount == 1) {
		req->sym_out = matchres.vsymp;
		req->defobj_out = obj;
		return (0);
	}
	return (ESRCH);
}

/* Search for symbol using GNU hash function */
static int
symlook_obj1_gnu(SymLook *req, const Obj_Entry *obj)
{
	Elf_Addr bloom_word;
	const Elf32_Word *hashval;
	Elf32_Word bucket;
	Sym_Match_Result matchres;
	unsigned int h1, h2;
	unsigned long symnum;

	matchres.sym_out = NULL;
	matchres.vsymp = NULL;
	matchres.vcount = 0;

	/* Pick right bitmask word from Bloom filter array */
	bloom_word = obj->bloom_gnu[(req->hash_gnu / __ELF_WORD_SIZE) &
	    obj->maskwords_bm_gnu];

	/* Calculate modulus word size of gnu hash and its derivative */
	h1 = req->hash_gnu & (__ELF_WORD_SIZE - 1);
	h2 = ((req->hash_gnu >> obj->shift2_gnu) & (__ELF_WORD_SIZE - 1));

	/* Filter out the "definitely not in set" queries */
	if (((bloom_word >> h1) & (bloom_word >> h2) & 1) == 0)
		return (ESRCH);

	/* Locate hash chain and corresponding value element*/
	bucket = obj->buckets_gnu[req->hash_gnu % obj->nbuckets_gnu];
	if (bucket == 0)
		return (ESRCH);
	hashval = &obj->chain_zero_gnu[bucket];
	do {
		if (((*hashval ^ req->hash_gnu) >> 1) == 0) {
			symnum = hashval - obj->chain_zero_gnu;
			if (matched_symbol(req, obj, &matchres, symnum)) {
				req->sym_out = matchres.sym_out;
				req->defobj_out = obj;
				return (0);
			}
		}
	} while ((*hashval++ & 1) == 0);
	if (matchres.vcount == 1) {
		req->sym_out = matchres.vsymp;
		req->defobj_out = obj;
		return (0);
	}
	return (ESRCH);
}

static void
trace_loaded_objects(Obj_Entry *obj)
{
    const char *fmt1, *fmt2, *fmt, *main_local, *list_containers;
    int		c;

    if ((main_local = _getenv_ld("LD_TRACE_LOADED_OBJECTS_PROGNAME")) == NULL)
	main_local = "";

    if ((fmt1 = _getenv_ld("LD_TRACE_LOADED_OBJECTS_FMT1")) == NULL)
	fmt1 = "\t%o => %p (%x)\n";

    if ((fmt2 = _getenv_ld("LD_TRACE_LOADED_OBJECTS_FMT2")) == NULL)
	fmt2 = "\t%o (%x)\n";

    list_containers = _getenv_ld("LD_TRACE_LOADED_OBJECTS_ALL");

    for (; obj; obj = obj->next) {
	Needed_Entry		*needed;
	char			*name, *path;
	bool			is_lib;

	if (list_containers && obj->needed != NULL)
	    rtld_printf("%s:\n", obj->path);
	for (needed = obj->needed; needed; needed = needed->next) {
	    if (needed->obj != NULL) {
		if (needed->obj->traced && !list_containers)
		    continue;
		needed->obj->traced = true;
		path = needed->obj->path;
	    } else
		path = "not found";

	    name = (char *)obj->strtab + needed->name;
	    is_lib = strncmp(name, "lib", 3) == 0;	/* XXX - bogus */

	    fmt = is_lib ? fmt1 : fmt2;
	    while ((c = *fmt++) != '\0') {
		switch (c) {
		default:
		    rtld_putchar(c);
		    continue;
		case '\\':
		    switch (c = *fmt) {
		    case '\0':
			continue;
		    case 'n':
			rtld_putchar('\n');
			break;
		    case 't':
			rtld_putchar('\t');
			break;
		    }
		    break;
		case '%':
		    switch (c = *fmt) {
		    case '\0':
			continue;
		    case '%':
		    default:
			rtld_putchar(c);
			break;
		    case 'A':
			rtld_putstr(main_local);
			break;
		    case 'a':
			rtld_putstr(obj_main->path);
			break;
		    case 'o':
			rtld_putstr(name);
			break;
		    case 'p':
			rtld_putstr(path);
			break;
		    case 'x':
			rtld_printf("%p", needed->obj ? needed->obj->mapbase :
			  0);
			break;
		    }
		    break;
		}
		++fmt;
	    }
	}
    }
}

/*
 * Unload a dlopened object and its dependencies from memory and from
 * our data structures.  It is assumed that the DAG rooted in the
 * object has already been unreferenced, and that the object has a
 * reference count of 0.
 */
static void
unload_object(Obj_Entry *root)
{
    Obj_Entry *obj;
    Obj_Entry **linkp;

    assert(root->refcount == 0);

    /*
     * Pass over the DAG removing unreferenced objects from
     * appropriate lists.
     */
    unlink_object(root);

    /* Unmap all objects that are no longer referenced. */
    linkp = &obj_list->next;
    while ((obj = *linkp) != NULL) {
	if (obj->refcount == 0) {
	    LD_UTRACE(UTRACE_UNLOAD_OBJECT, obj, obj->mapbase, obj->mapsize, 0,
		obj->path);
	    dbg("unloading \"%s\"", obj->path);
	    unload_filtees(root);
	    munmap(obj->mapbase, obj->mapsize);
	    linkmap_delete(obj);
	    *linkp = obj->next;
	    obj_count--;
	    obj_free(obj);
	} else
	    linkp = &obj->next;
    }
    obj_tail = linkp;
}

static void
unlink_object(Obj_Entry *root)
{
    Objlist_Entry *elm;

    if (root->refcount == 0) {
	/* Remove the object from the RTLD_GLOBAL list. */
	objlist_remove(&list_global, root);

    	/* Remove the object from all objects' DAG lists. */
	STAILQ_FOREACH(elm, &root->dagmembers, link) {
	    objlist_remove(&elm->obj->dldags, root);
	    if (elm->obj != root)
		unlink_object(elm->obj);
	}
    }
}

static void
ref_dag(Obj_Entry *root)
{
    Objlist_Entry *elm;

    assert(root->dag_inited);
    STAILQ_FOREACH(elm, &root->dagmembers, link)
	elm->obj->refcount++;
}

static void
unref_dag(Obj_Entry *root)
{
    Objlist_Entry *elm;

    assert(root->dag_inited);
    STAILQ_FOREACH(elm, &root->dagmembers, link)
	elm->obj->refcount--;
}

/*
 * Common code for MD __tls_get_addr().
 */
void *
tls_get_addr_common(Elf_Addr** dtvp, int index, size_t offset)
{
    Elf_Addr* dtv = *dtvp;
    RtldLockState lockstate;

    /* Check dtv generation in case new modules have arrived */
    if (dtv[0] != tls_dtv_generation) {
	Elf_Addr* newdtv;
	int to_copy;

	wlock_acquire(rtld_bind_lock, &lockstate);
	newdtv = xcalloc(tls_max_index + 2, sizeof(Elf_Addr));
	to_copy = dtv[1];
	if (to_copy > tls_max_index)
	    to_copy = tls_max_index;
	memcpy(&newdtv[2], &dtv[2], to_copy * sizeof(Elf_Addr));
	newdtv[0] = tls_dtv_generation;
	newdtv[1] = tls_max_index;
	free(dtv);
	cpu_sfence();
	dtv = *dtvp = newdtv;
	lock_release(rtld_bind_lock, &lockstate);
    }

    /* Dynamically allocate module TLS if necessary */
    if (!dtv[index + 1]) {
	/* Signal safe, wlock will block out signals. */
	wlock_acquire(rtld_bind_lock, &lockstate);
	dtv = *dtvp;
	if (!dtv[index + 1])
	    dtv[index + 1] = (Elf_Addr)allocate_module_tls(index);
	lock_release(rtld_bind_lock, &lockstate);
    }
    return ((void *)(dtv[index + 1] + offset));
}

#if defined(RTLD_STATIC_TLS_VARIANT_II)

/*
 * Allocate the static TLS area.  Return a pointer to the TCB.  The 
 * static area is based on negative offsets relative to the tcb.
 *
 * The TCB contains an errno pointer for the system call layer, but because
 * we are the RTLD we really have no idea how the caller was compiled so
 * the information has to be passed in.  errno can either be:
 *
 *	type 0	errno is a simple non-TLS global pointer.
 *		(special case for e.g. libc_rtld)
 *	type 1	errno accessed by GOT entry	(dynamically linked programs)
 *	type 2	errno accessed by %gs:OFFSET	(statically linked programs)
 */
struct tls_tcb *
allocate_tls(Obj_Entry *objs)
{
    Obj_Entry *obj;
    size_t data_size;
    size_t dtv_size;
    struct tls_tcb *tcb;
    Elf_Addr *dtv;
    Elf_Addr addr;

    /*
     * Allocate the new TCB.  static TLS storage is placed just before the
     * TCB to support the %gs:OFFSET (negative offset) model.
     */
    data_size = (tls_static_space + RTLD_STATIC_TLS_ALIGN_MASK) &
		~RTLD_STATIC_TLS_ALIGN_MASK;
    tcb = malloc(data_size + sizeof(*tcb));
    tcb = (void *)((char *)tcb + data_size);	/* actual tcb location */

    dtv_size = (tls_max_index + 2) * sizeof(Elf_Addr);
    dtv = malloc(dtv_size);
    bzero(dtv, dtv_size);

#ifdef RTLD_TCB_HAS_SELF_POINTER
    tcb->tcb_self = tcb;
#endif
    tcb->tcb_dtv = dtv;
    tcb->tcb_pthread = NULL;

    dtv[0] = tls_dtv_generation;
    dtv[1] = tls_max_index;

    for (obj = objs; obj; obj = obj->next) {
	if (obj->tlsoffset) {
	    addr = (Elf_Addr)tcb - obj->tlsoffset;
	    memset((void *)(addr + obj->tlsinitsize),
		   0, obj->tlssize - obj->tlsinitsize);
	    if (obj->tlsinit) {
		memcpy((void*) addr, obj->tlsinit, obj->tlsinitsize);
		obj->static_tls_copied = true;
	    }
	    dtv[obj->tlsindex + 1] = addr;
	}
    }
    return(tcb);
}

void
free_tls(struct tls_tcb *tcb)
{
    Elf_Addr *dtv;
    int dtv_size, i;
    Elf_Addr tls_start, tls_end;
    size_t data_size;

    data_size = (tls_static_space + RTLD_STATIC_TLS_ALIGN_MASK) &
		~RTLD_STATIC_TLS_ALIGN_MASK;

    dtv = tcb->tcb_dtv;
    dtv_size = dtv[1];
    tls_end = (Elf_Addr)tcb;
    tls_start = (Elf_Addr)tcb - data_size;
    for (i = 0; i < dtv_size; i++) {
	if (dtv[i+2] != 0 && (dtv[i+2] < tls_start || dtv[i+2] > tls_end)) {
	    free((void *)dtv[i+2]);
	}
    }
    free(dtv);

    free((void*) tls_start);
}

#else
#error "Unsupported TLS layout"
#endif

/*
 * Allocate TLS block for module with given index.
 */
void *
allocate_module_tls(int index)
{
    Obj_Entry* obj;
    char* p;

    for (obj = obj_list; obj; obj = obj->next) {
	if (obj->tlsindex == index)
	    break;
    }
    if (!obj) {
	_rtld_error("Can't find module with TLS index %d", index);
	die();
    }

    if (obj->tls_static) {
#if defined(RTLD_STATIC_TLS_VARIANT_II)
        p = (char *)tls_get_tcb() - obj->tlsoffset;
#else
#error "Unsupported TLS layout"
#endif
        return p;
    }

    p = malloc(obj->tlssize);
    if (p == NULL) {
	_rtld_error("Cannot allocate TLS block for index %d", index);
	die();
    }
    memcpy(p, obj->tlsinit, obj->tlsinitsize);
    memset(p + obj->tlsinitsize, 0, obj->tlssize - obj->tlsinitsize);

    return p;
}

bool
allocate_tls_offset(Obj_Entry *obj)
{
    size_t off;

    if (obj->tls_static)
	return true;

    if (obj->tls_dynamic)
        return false;

    if (obj->tlssize == 0) {
	obj->tls_static = true;
	return true;
    }

    if (obj->tlsindex == 1)
	off = calculate_first_tls_offset(obj->tlssize, obj->tlsalign);
    else
	off = calculate_tls_offset(tls_last_offset, tls_last_size,
				   obj->tlssize, obj->tlsalign);

    /*
     * If we have already fixed the size of the static TLS block, we
     * must stay within that size. When allocating the static TLS, we
     * leave a small amount of space spare to be used for dynamically
     * loading modules which use static TLS.
     */
    if (tls_static_space) {
	if (calculate_tls_end(off, obj->tlssize) > tls_static_space)
	    return false;
    }

    tls_last_offset = obj->tlsoffset = off;
    tls_last_size = obj->tlssize;
    obj->tls_static = true;

    return true;
}

void
free_tls_offset(Obj_Entry *obj)
{
#ifdef RTLD_STATIC_TLS_VARIANT_II
    /*
     * If we were the last thing to allocate out of the static TLS
     * block, we give our space back to the 'allocator'. This is a
     * simplistic workaround to allow libGL.so.1 to be loaded and
     * unloaded multiple times. We only handle the Variant II
     * mechanism for now - this really needs a proper allocator.  
     */
    if (calculate_tls_end(obj->tlsoffset, obj->tlssize)
	== calculate_tls_end(tls_last_offset, tls_last_size)) {
	tls_last_offset -= obj->tlssize;
	tls_last_size = 0;
    }
#endif
}

struct tls_tcb *
_rtld_allocate_tls(void)
{
    struct tls_tcb *new_tcb;
    RtldLockState lockstate;

    wlock_acquire(rtld_bind_lock, &lockstate);
    new_tcb = allocate_tls(obj_list);
    lock_release(rtld_bind_lock, &lockstate);

    return (new_tcb);
}

void
_rtld_free_tls(struct tls_tcb *tcb)
{
    RtldLockState lockstate;

    wlock_acquire(rtld_bind_lock, &lockstate);
    free_tls(tcb);
    lock_release(rtld_bind_lock, &lockstate);
}

static void
object_add_name(Obj_Entry *obj, const char *name)
{
    Name_Entry *entry;
    size_t len;

    len = strlen(name);
    entry = malloc(sizeof(Name_Entry) + len);

    if (entry != NULL) {
	strcpy(entry->name, name);
	STAILQ_INSERT_TAIL(&obj->names, entry, link);
    }
}

static int
object_match_name(const Obj_Entry *obj, const char *name)
{
    Name_Entry *entry;

    STAILQ_FOREACH(entry, &obj->names, link) {
	if (strcmp(name, entry->name) == 0)
	    return (1);
    }
    return (0);
}

static Obj_Entry *
locate_dependency(const Obj_Entry *obj, const char *name)
{
    const Objlist_Entry *entry;
    const Needed_Entry *needed;

    STAILQ_FOREACH(entry, &list_main, link) {
	if (object_match_name(entry->obj, name))
	    return entry->obj;
    }

    for (needed = obj->needed;  needed != NULL;  needed = needed->next) {
	if (strcmp(obj->strtab + needed->name, name) == 0 ||
	  (needed->obj != NULL && object_match_name(needed->obj, name))) {
	    /*
	     * If there is DT_NEEDED for the name we are looking for,
	     * we are all set.  Note that object might not be found if
	     * dependency was not loaded yet, so the function can
	     * return NULL here.  This is expected and handled
	     * properly by the caller.
	     */
	    return (needed->obj);
	}
    }
    _rtld_error("%s: Unexpected inconsistency: dependency %s not found",
	obj->path, name);
    die();
}

static int
check_object_provided_version(Obj_Entry *refobj, const Obj_Entry *depobj,
    const Elf_Vernaux *vna)
{
    const Elf_Verdef *vd;
    const char *vername;

    vername = refobj->strtab + vna->vna_name;
    vd = depobj->verdef;
    if (vd == NULL) {
	_rtld_error("%s: version %s required by %s not defined",
	    depobj->path, vername, refobj->path);
	return (-1);
    }
    for (;;) {
	if (vd->vd_version != VER_DEF_CURRENT) {
	    _rtld_error("%s: Unsupported version %d of Elf_Verdef entry",
		depobj->path, vd->vd_version);
	    return (-1);
	}
	if (vna->vna_hash == vd->vd_hash) {
	    const Elf_Verdaux *aux = (const Elf_Verdaux *)
		((char *)vd + vd->vd_aux);
	    if (strcmp(vername, depobj->strtab + aux->vda_name) == 0)
		return (0);
	}
	if (vd->vd_next == 0)
	    break;
	vd = (const Elf_Verdef *) ((char *)vd + vd->vd_next);
    }
    if (vna->vna_flags & VER_FLG_WEAK)
	return (0);
    _rtld_error("%s: version %s required by %s not found",
	depobj->path, vername, refobj->path);
    return (-1);
}

static int
rtld_verify_object_versions(Obj_Entry *obj)
{
    const Elf_Verneed *vn;
    const Elf_Verdef  *vd;
    const Elf_Verdaux *vda;
    const Elf_Vernaux *vna;
    const Obj_Entry *depobj;
    int maxvernum, vernum;

    if (obj->ver_checked)
	return (0);
    obj->ver_checked = true;

    maxvernum = 0;
    /*
     * Walk over defined and required version records and figure out
     * max index used by any of them. Do very basic sanity checking
     * while there.
     */
    vn = obj->verneed;
    while (vn != NULL) {
	if (vn->vn_version != VER_NEED_CURRENT) {
	    _rtld_error("%s: Unsupported version %d of Elf_Verneed entry",
		obj->path, vn->vn_version);
	    return (-1);
	}
	vna = (const Elf_Vernaux *) ((char *)vn + vn->vn_aux);
	for (;;) {
	    vernum = VER_NEED_IDX(vna->vna_other);
	    if (vernum > maxvernum)
		maxvernum = vernum;
	    if (vna->vna_next == 0)
		 break;
	    vna = (const Elf_Vernaux *) ((char *)vna + vna->vna_next);
	}
	if (vn->vn_next == 0)
	    break;
	vn = (const Elf_Verneed *) ((char *)vn + vn->vn_next);
    }

    vd = obj->verdef;
    while (vd != NULL) {
	if (vd->vd_version != VER_DEF_CURRENT) {
	    _rtld_error("%s: Unsupported version %d of Elf_Verdef entry",
		obj->path, vd->vd_version);
	    return (-1);
	}
	vernum = VER_DEF_IDX(vd->vd_ndx);
	if (vernum > maxvernum)
		maxvernum = vernum;
	if (vd->vd_next == 0)
	    break;
	vd = (const Elf_Verdef *) ((char *)vd + vd->vd_next);
    }

    if (maxvernum == 0)
	return (0);

    /*
     * Store version information in array indexable by version index.
     * Verify that object version requirements are satisfied along the
     * way.
     */
    obj->vernum = maxvernum + 1;
    obj->vertab = xcalloc(obj->vernum, sizeof(Ver_Entry));

    vd = obj->verdef;
    while (vd != NULL) {
	if ((vd->vd_flags & VER_FLG_BASE) == 0) {
	    vernum = VER_DEF_IDX(vd->vd_ndx);
	    assert(vernum <= maxvernum);
	    vda = (const Elf_Verdaux *)((char *)vd + vd->vd_aux);
	    obj->vertab[vernum].hash = vd->vd_hash;
	    obj->vertab[vernum].name = obj->strtab + vda->vda_name;
	    obj->vertab[vernum].file = NULL;
	    obj->vertab[vernum].flags = 0;
	}
	if (vd->vd_next == 0)
	    break;
	vd = (const Elf_Verdef *) ((char *)vd + vd->vd_next);
    }

    vn = obj->verneed;
    while (vn != NULL) {
	depobj = locate_dependency(obj, obj->strtab + vn->vn_file);
	if (depobj == NULL)
	    return (-1);
	vna = (const Elf_Vernaux *) ((char *)vn + vn->vn_aux);
	for (;;) {
	    if (check_object_provided_version(obj, depobj, vna))
		return (-1);
	    vernum = VER_NEED_IDX(vna->vna_other);
	    assert(vernum <= maxvernum);
	    obj->vertab[vernum].hash = vna->vna_hash;
	    obj->vertab[vernum].name = obj->strtab + vna->vna_name;
	    obj->vertab[vernum].file = obj->strtab + vn->vn_file;
	    obj->vertab[vernum].flags = (vna->vna_other & VER_NEED_HIDDEN) ?
		VER_INFO_HIDDEN : 0;
	    if (vna->vna_next == 0)
		 break;
	    vna = (const Elf_Vernaux *) ((char *)vna + vna->vna_next);
	}
	if (vn->vn_next == 0)
	    break;
	vn = (const Elf_Verneed *) ((char *)vn + vn->vn_next);
    }
    return 0;
}

static int
rtld_verify_versions(const Objlist *objlist)
{
    Objlist_Entry *entry;
    int rc;

    rc = 0;
    STAILQ_FOREACH(entry, objlist, link) {
	/*
	 * Skip dummy objects or objects that have their version requirements
	 * already checked.
	 */
	if (entry->obj->strtab == NULL || entry->obj->vertab != NULL)
	    continue;
	if (rtld_verify_object_versions(entry->obj) == -1) {
	    rc = -1;
	    if (ld_tracing == NULL)
		break;
	}
    }
    if (rc == 0 || ld_tracing != NULL)
	rc = rtld_verify_object_versions(&obj_rtld);
    return rc;
}

const Ver_Entry *
fetch_ventry(const Obj_Entry *obj, unsigned long symnum)
{
    Elf_Versym vernum;

    if (obj->vertab) {
	vernum = VER_NDX(obj->versyms[symnum]);
	if (vernum >= obj->vernum) {
	    _rtld_error("%s: symbol %s has wrong verneed value %d",
		obj->path, obj->strtab + symnum, vernum);
	} else if (obj->vertab[vernum].hash != 0) {
	    return &obj->vertab[vernum];
	}
    }
    return NULL;
}

int
_rtld_get_stack_prot(void)
{

	return (stack_prot);
}

static void
map_stacks_exec(RtldLockState *lockstate)
{
	return;
	/*
	 * Stack protection must be implemented in the kernel before the dynamic
	 * linker can handle PT_GNU_STACK sections.
	 * The following is the FreeBSD implementation of map_stacks_exec()
	 * void (*thr_map_stacks_exec)(void);
	 *
	 * if ((max_stack_flags & PF_X) == 0 || (stack_prot & PROT_EXEC) != 0)
	 *     return;
	 * thr_map_stacks_exec = (void (*)(void))(uintptr_t)
	 *     get_program_var_addr("__pthread_map_stacks_exec", lockstate);
	 * if (thr_map_stacks_exec != NULL) {
	 *     stack_prot |= PROT_EXEC;
	 *     thr_map_stacks_exec();
	 * }
	 */
}

/*
 * Only called after all primary shared libraries are loaded (EARLY is
 * not set).  Resolves the static TLS distribution function at first-call.
 * This is typically a weak libc symbol that is overrideen by the threading
 * library.
 */
static void
distribute_static_tls(Objlist *list, RtldLockState *lockstate)
{
	Objlist_Entry *elm;
	Obj_Entry *obj;
	static void (*dtlsfunc)(size_t, void *, size_t, size_t);

	/*
	 * First time, resolve "_pthread_distribute_static_tls".
	 */
	if (dtlsfunc == NULL) {
		dtlsfunc = (void *)dlfunc(RTLD_ALL,
					  "_pthread_distribute_static_tls");
		if (dtlsfunc == NULL)
			return;
	}

	/*
	 * Initialize static TLS data for the object list using the callback
	 * function (to either libc or pthreads).
	 */
	STAILQ_FOREACH(elm, list, link) {
		obj = elm->obj;
		if (/*obj->marker ||*/ !obj->tls_static || obj->static_tls_copied)
			continue;
		dtlsfunc(obj->tlsoffset, obj->tlsinit,
			 obj->tlsinitsize, obj->tlssize);
		obj->static_tls_copied = true;
	}
}

void
symlook_init(SymLook *dst, const char *name)
{

	bzero(dst, sizeof(*dst));
	dst->name = name;
	dst->hash = elf_hash(name);
	dst->hash_gnu = gnu_hash(name);
}

static void
symlook_init_from_req(SymLook *dst, const SymLook *src)
{

	dst->name = src->name;
	dst->hash = src->hash;
	dst->hash_gnu = src->hash_gnu;
	dst->ventry = src->ventry;
	dst->flags = src->flags;
	dst->defobj_out = NULL;
	dst->sym_out = NULL;
	dst->lockstate = src->lockstate;
}


/*
 * Parse a file descriptor number without pulling in more of libc (e.g. atoi).
 */
static int
parse_libdir(const char *str)
{
	static const int RADIX = 10;  /* XXXJA: possibly support hex? */
	const char *orig;
	int fd;
	char c;

	orig = str;
	fd = 0;
	for (c = *str; c != '\0'; c = *++str) {
		if (c < '0' || c > '9')
			return (-1);

		fd *= RADIX;
		fd += c - '0';
	}

	/* Make sure we actually parsed something. */
	if (str == orig) {
		_rtld_error("failed to parse directory FD from '%s'", str);
		return (-1);
	}
	return (fd);
}

#ifdef ENABLE_OSRELDATE
/*
 * Overrides for libc_pic-provided functions.
 */

int
__getosreldate(void)
{
	size_t len;
	int oid[2];
	int error, osrel;

	if (osreldate != 0)
		return (osreldate);

	oid[0] = CTL_KERN;
	oid[1] = KERN_OSRELDATE;
	osrel = 0;
	len = sizeof(osrel);
	error = sysctl(oid, 2, &osrel, &len, NULL, 0);
	if (error == 0 && osrel > 0 && len == sizeof(osrel))
		osreldate = osrel;
	return (osreldate);
}
#endif

/*
 * Ask the kernel for the extra tls space to allocate after calculating
 * base tls requirements in rtld-elf.  5.9 or later.
 */
static int
__getstatictlsextra(void)
{
	size_t len;
	int oid[2];
	int error;
	int tls_extra;

	oid[0] = CTL_KERN;
	oid[1] = KERN_STATIC_TLS_EXTRA;
	len = sizeof(tls_extra);
	error = sysctl(oid, 2, &tls_extra, &len, NULL, 0);
	if (error || len != sizeof(tls_extra))
		tls_extra = RTLD_STATIC_TLS_EXTRA_DEFAULT;
	if (tls_extra < RTLD_STATIC_TLS_EXTRA_MIN)
		tls_extra = RTLD_STATIC_TLS_EXTRA_MIN;
	if (tls_extra > RTLD_STATIC_TLS_EXTRA_MAX)
		tls_extra = RTLD_STATIC_TLS_EXTRA_MAX;
	return tls_extra;
}

/*
 * No unresolved symbols for rtld.
 */
void
__pthread_cxa_finalize(struct dl_phdr_info *a)
{
}

const char *
rtld_strerror(int errnum)
{

	if (errnum < 0 || errnum >= sys_nerr)
		return ("Unknown error");
	return (sys_errlist[errnum]);
}
