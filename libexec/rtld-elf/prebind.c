/*-
 * Copyright (c) 2003 Matthew N. Dodd <winter@jurai.net>
 * All rights reserved.
 *
 * Redistribution and use in soupe and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of soupe code must retain the above copyright
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
 * $DragonFly: src/libexec/rtld-elf/Attic/prebind.c,v 1.1 2003/09/18 21:22:56 dillon Exp $
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>

#include "debug.h"
#include "rtld.h"

typedef struct {
    Elf_Word	r_info;		/* Relocation type and symbol index. */
    Elf_Addr	r_offset;	/* Offset from object Relocation constant */
    int		index;		/* Definition Object index */
    Elf_Addr	st_value;	/* Definition Symbol value. */
    Elf_Size	st_size;	/* Copy relocation size */
} Prebind_Entry;

typedef struct {
    int		index;		/* Object index */
    int		type;		/* Type of Prebind_Entry objects */
    int		count;		/* Number of Prebind_Entry objects */
} Prebind_Object_Index;
#define	TYPE_NONE	0	/* None */
#define	TYPE_NON_PLT	1	/* Non-PLT relocation. */
#define	TYPE_PLT	2	/* PLT relocation. */
#define	TYPE_COPY	3	/* Non-PLT COPY relocation */

typedef struct {
    char	name[MAXPATHLEN];/* (obj->path) */
    u_int32_t	uniqid;		/* Unique executable ID */
} Prebind_Object;

typedef struct {
    int		version;	/* Version number */
    char	name[MAXPATHLEN];/* basename */
    u_int32_t	uniqid;		/* Unique executable ID */
    int		nobjects;	/* number of Prebind_Object elements */
} Prebind_Header;

#define	PREBIND_PATH		"/var/db/prebind"
#define	PREBIND_VERSION	2003052500
#define	PREBIND_MINSIZE		(sizeof(Prebind_Header) + \
				 sizeof(Prebind_Object))

static char	name[MAXPATHLEN];
static int	valid_args	(Obj_Entry *, Obj_Entry *);
static int	set_filename	(Obj_Entry *);
static int	check_object	(Obj_Entry *, Prebind_Object *);

/* Non-PLT relocations */
static int	count_non_plt	(Obj_Entry *);
static int	write_non_plt	(int, Obj_Entry *, Obj_Entry **);
static int	read_non_plt	(Obj_Entry *, Obj_Entry **,
				 Prebind_Object_Index *,
				 caddr_t, off_t *, off_t);
/* PLT relocations */
static int	count_plt	(Obj_Entry *);
static int	write_plt	(int, Obj_Entry *, Obj_Entry **);
static int	read_plt	(Obj_Entry *, Obj_Entry **,
				 Prebind_Object_Index *,
				 caddr_t, off_t *, off_t);
/* Non-PLT COPY relocations */
static int	count_copy	(Obj_Entry *);
static int	write_copy	(int, Obj_Entry *, Obj_Entry **);
static int	read_copy	(Obj_Entry *, Obj_Entry **,
				 Prebind_Object_Index *,
				 caddr_t, off_t *, off_t);

#if 0
static void	dump_Elf_Rel	(Obj_Entry *, const Elf_Rel *, u_long);
#endif
static int	obj2index	(Obj_Entry **, const Obj_Entry *);

static inline int
prebind_cklen(off_t off, off_t size, off_t len)
{
    if (off + len > size) {
	warnx("Prebind file appears truncated.");
	return -1;
    }
    
    return 0;
}

int
prebind_load (Obj_Entry *obj_rtld, Obj_Entry *obj0)
{
    struct stat sb;
    Obj_Entry **obj_list;
    Obj_Entry *obj;
    Prebind_Header *ph;
    Prebind_Object *po;
    Prebind_Object_Index *poi;
    caddr_t cache;
    off_t off, size;
    int fd;
    int nobj;
    int i;
    int error;
    int retval;

    obj_list = NULL;
    retval = -1;

    if ((nobj = valid_args(obj_rtld, obj0)) == -1)
	return (1);
    if (set_filename(obj0))
	return (1);

    off = 0;
    fd = open(name, O_RDONLY, 0644);
    if (fd == -1)
	return (1);

    error = fstat(fd, &sb);
    if (error == -1) {
	warn("stat(\"%s\", ...)", name);
	goto out;
    }
    size = sb.st_size;

    if ((sb.st_uid != 0) || (sb.st_gid != 0) ||
	((sb.st_mode & (S_IWGRP|S_IWOTH)) != 0)) {
	warnx("Prebind file has invalid permissions/ownership.");
	goto out;
    }

    dbg("Prebind: Reading from \"%s\"\n", name);

    cache = mmap(0, sb.st_size, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
    if (cache == MAP_FAILED)
	goto out;

    if (prebind_cklen(off, size, sizeof(Prebind_Header)))
	goto error;

    ph = (Prebind_Header *)cache;
    off += sizeof(Prebind_Header);
    dbg("\tversion %d, name \"%s\", nobjects %d\n",
	ph->version, ph->name, ph->nobjects);

    if (ph->version != PREBIND_VERSION) {
	warnx("Prebind file header version number invalid!");
	goto error;
    }

    if (nobj != ph->nobjects) {
	warnx("Number of objects (%d) different from expected (%d).",
	    nobj, ph->nobjects);
	goto error;
    }

    if (ph->uniqid != obj0->uniqid) {
	warnx("Executable UNIQID does not match cache file header.");
	goto error;
    }

    /* Init our object list */
    obj_list = xmalloc((nobj + 2) * sizeof(obj_list[0]));
    obj_list[nobj+1] = NULL; /* NULL terminate the list */

    /* Check ld-elf.so and add it to the list. */
    if (prebind_cklen(off, size, sizeof(Prebind_Object)))
	goto error;

    po = (Prebind_Object *)(cache + off);
    off += sizeof(Prebind_Object);
    dbg("  object \"%s\", uniqid %d, relocbase %p\n",
	   obj_rtld->path, obj_rtld->uniqid, obj_rtld->relocbase);
    if (check_object(obj_rtld, po))
	goto error;
    obj_list[0] = obj_rtld;

    /* Check each object and add it to the list */
    for (i = 1, obj = obj0; obj != NULL; i++, obj = obj->next) {
	if (prebind_cklen(off, size, sizeof(Prebind_Object)))
	    goto error;

	po = (Prebind_Object *)(cache + off);
	off += sizeof(Prebind_Object);

	dbg("  object \"%s\", uniqid %d, relocbase %p\n",
	    obj->path, obj->uniqid, obj->relocbase);

	if (check_object(obj, po))
	    goto error;

	 obj_list[i] = obj;
    }

    while ((off + sizeof(Prebind_Object_Index)) <= sb.st_size) {
	if (prebind_cklen(off, size, sizeof(Prebind_Object_Index)))
	    goto error;

	poi = (Prebind_Object_Index *)(cache + off);
	off += sizeof(Prebind_Object_Index);
	obj = obj_list[poi->index];

	dbg("index %d, type %d, count %d\n",
	    poi->index, poi->type, poi->count);

	switch (poi->type) {
	case TYPE_NON_PLT:
	    if (read_non_plt(obj, obj_list, poi, cache, &off, size) != 0)
		goto error;
	    break;
	case TYPE_PLT:
	    if (read_plt(obj, obj_list, poi, cache, &off, size) != 0)
		goto error;
	    break;
	case TYPE_COPY:
	    if (read_copy(obj, obj_list, poi, cache, &off, size) != 0)
		goto error;
	    break;
	default:
	    break;
	}
    }

    /* Finish up. */
    for (obj = obj0; obj != NULL; obj = obj->next) {
	/*
	 * Set up the magic number and version in the Obj_Entry.  These
	 * were checked in the crt1.o from the original ElfKit, so we
	 * set them for backward compatibility.
	 */
	obj->magic = RTLD_MAGIC;
	obj->version = RTLD_VERSION;

	/* Set the special PLT or GOT entries. */
	init_pltgot(obj);
    }

    retval = 0;
error:
    munmap(cache, sb.st_size);
out:
    close(fd);
    if (obj_list)
	free(obj_list);
    if (retval == -1)
	warnx("Prebind failed.");
    else
	dbg("Prebind ok.\n");
    return (retval);
}

int
prebind_save (Obj_Entry *obj_rtld, Obj_Entry *obj0)
{
    Obj_Entry **obj_list;
    Obj_Entry *obj;
    Prebind_Header ph;
    Prebind_Object po;
    Prebind_Object_Index poi;
    int nobj;
    int i;
    int fd;
    ssize_t len;

    obj_list = NULL;
    i = 0;

    if ((nobj = valid_args(obj_rtld, obj0)) == -1)
	return (1);
    if (set_filename(obj0))
	return (1);

    fd = open(name, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd == -1) {
	warn("open(\"%s\", ...)", name);
	return (1);
    }

    /* Prebinding Cache Header */
    bzero(&ph, sizeof(Prebind_Header));
    ph.version = PREBIND_VERSION;
    strcpy(ph.name, basename(obj0->path));
    ph.uniqid = obj0->uniqid;
    ph.nobjects = nobj;
    len = write(fd, &ph, sizeof(Prebind_Header));
    if (len == -1) {
	warn("write(Prebind_Header)");
	goto error;
    }
    if (len != sizeof(Prebind_Header)) {
	warnx("short write.");
	goto error;
    }

    /* Setup object list for index lookups. */
    obj_list = xmalloc((nobj + 2) * sizeof(obj_list[0]));
    obj_list[nobj+1] = NULL;

    dbg("  object %-30s uniqid %d\n", obj_rtld->path, obj_rtld->uniqid);

    /* ld-elf.so Prebinding Cache Object */
    bzero(&po, sizeof(Prebind_Object));
    strcpy(po.name, obj_rtld->path);
    po.uniqid = obj_rtld->uniqid;
    len = write(fd, &po, sizeof(Prebind_Object));
    if (len == -1) {
	warn("write(Prebind_Object)");
	goto error;
    }
    if (len != sizeof(Prebind_Object)) {
	warnx("short write.");
	goto error;
    }
    obj_list[0] = obj_rtld;

    /* Add entries for each object */
    for (i = 1, obj = obj0; obj != NULL; i++, obj = obj->next) {
	printf("  object %-30s uniqid %d\n", obj->path, obj->uniqid);
	if (obj->textrel)
	    dbg("\timpure text\n");

	/* Prebinding Cache Object */
	bzero(&po, sizeof(Prebind_Object));
	strcpy(po.name, obj->mainprog ?  basename(obj->path) : obj->path);
	po.uniqid = obj->uniqid;
	
	len = write(fd, &po, sizeof(Prebind_Object));
	if (len == -1) {
	    warn("write(Prebind_Object)");
	    goto error;
	}
	if (len != sizeof(Prebind_Object)) {
	    warnx("short write.");
	    goto error;
	}
	obj_list[i] = obj;
    }

    printf("Non-PLT Prebindings:\n");
    for (i = 1, obj = obj0; obj != NULL; i++, obj = obj->next) {
	poi.index = i;
	poi.type = TYPE_NON_PLT;
	poi.count = count_non_plt(obj);

	if (poi.count == 0)
	    continue;
	printf("  object %-30s count %d\n", obj->path, poi.count);

	len = write(fd, &poi, sizeof(Prebind_Object_Index));
	if (len == -1) {
	    warn("write(Prebind_Object_Index)");
	    goto error;
	}
	if (len != sizeof(Prebind_Object_Index)) {
	    warnx("short write.");
	    goto error;
	}

	len = write_non_plt(fd, obj, obj_list);
	if (len == -1)
	    goto error;
    }

    printf("PLT Prebindings:\n");
    for (i = 1, obj = obj0; obj != NULL; i++, obj = obj->next) {
	poi.index = i;
	poi.type = TYPE_PLT;
	poi.count = count_plt(obj);

	if (poi.count == 0)
	    continue;
	printf("  object %-30s count %d\n", obj->path, poi.count);

	len = write(fd, &poi, sizeof(Prebind_Object_Index));
	if (len == -1) {
	    warn("write(Prebind_Object_Index)");
	    goto error;
	}
	if (len != sizeof(Prebind_Object_Index)) {
	    warnx("short write.");
	    goto error;
	}

	len = write_plt(fd, obj, obj_list);
	if (len == -1)
	    goto error;
    }

    printf("Non-PLT COPY Prebindings:\n");
    for (i = 1, obj = obj0; obj != NULL; i++, obj = obj->next) {
	poi.index = i;
	poi.type = TYPE_COPY;
	poi.count = count_copy(obj);

	if (poi.count == 0)
	    continue;
	printf("  object %-30s count %d\n", obj->path, poi.count);

	len = write(fd, &poi, sizeof(Prebind_Object_Index));
	if (len == -1) {
	    warn("write(Prebind_Object_Index)");
	    goto error;
	}
	if (len != sizeof(Prebind_Object_Index)) {
	    warnx("short write.");
	    goto error;
	}

	len = write_copy(fd, obj, obj_list);
	if (len == -1)
	    goto error;
    }

error:
    if (obj_list)
	free(obj_list);
    close(fd);
    return (0);
}

static int
valid_args (Obj_Entry *obj_rtld, Obj_Entry *obj0)
{
    Obj_Entry *obj;
    int nobj;

    if (obj_rtld->rtld == 0 || obj0->mainprog == 0)
	return (-1);

    for (nobj = 0, obj = obj0; obj != NULL; ++nobj, obj = obj->next)
	;   /* loop */

    return (nobj);
}

static int
set_filename (Obj_Entry *obj0)
{

    bzero(name, MAXPATHLEN);
    snprintf(name, MAXPATHLEN, "%s/%08x.%s",
	PREBIND_PATH, obj0->uniqid, basename(obj0->path));
    if (strlen(name) < (strlen(PREBIND_PATH) + 10)) {
	warnx("Invalid or truncated Prebind file name \"%s\".", name);
	return (1);
    }
    return (0);
}

static int
check_object (Obj_Entry *obj, Prebind_Object *po)
{
    if (po->uniqid != obj->uniqid) {
	warnx("Object UNIQID does not match cache file entry.");
	warnx("\"%s\".", obj->path);
	return (1);
    }

    if (strcmp(po->name, obj->mainprog ?
	   basename(obj->path) : obj->path) != 0) {
	warnx("Object name does not match cache file entry.");
	return (1);
    }

    return (0);
}

static int
count_non_plt (Obj_Entry *obj)
{
    const Elf_Rel *rel;
    const Elf_Rel *rellim;
    int count;

    count = 0;
    rellim = (const Elf_Rel *) ((caddr_t) obj->rel + obj->relsize);
    for (rel = obj->rel; rel < rellim; rel++) {
	switch (ELF_R_TYPE(rel->r_info)) {
	case R_386_32:
	case R_386_PC32:
	case R_386_GLOB_DAT:
		count++;
		break;
	default:
		break;
	}
    }

    return (count);
}

static int
write_non_plt (int fd, Obj_Entry *obj, Obj_Entry **obj_list)
{
    const Elf_Rel *rel;
    const Elf_Rel *rellim;
    SymCache *cache;
    Prebind_Entry pe;
    ssize_t len;
    int bytes;
    int error;

    bytes = obj->nchains * sizeof(SymCache);
    error = -1;

    /*
     * The dynamic loader may be called from a thread, we have
     * limited amounts of stack available so we cannot use alloca().
     */
    cache = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_ANON, -1, 0);
    if (cache == MAP_FAILED)
	cache = NULL;

    rellim = (const Elf_Rel *) ((caddr_t) obj->rel + obj->relsize);
    for (rel = obj->rel; rel < rellim; rel++) {
	pe.r_info = rel->r_info;
	pe.r_offset = rel->r_offset;
	pe.index = -1;
	pe.st_value = 0;
	pe.st_size = 0;

	switch (ELF_R_TYPE(rel->r_info)) {
	case R_386_NONE:
	case R_386_RELATIVE:
	    continue;
	    break;
	case R_386_32:
	case R_386_PC32:
	case R_386_GLOB_DAT: {
	    const Elf_Sym *defsym;
	    const Obj_Entry *defobj;

	    defsym = find_symdef(ELF_R_SYM(rel->r_info), obj, &defobj, false,
				 cache);
	    if (defsym == NULL)
		goto error;

	    pe.index = obj2index(obj_list, defobj);
	    pe.st_value = defsym->st_value;
	    break;
	}
	case R_386_COPY:
	    if (!obj->mainprog) {
		warnx("%s: Unexpected R_386_COPY relocation in shared library",
		      obj->path);
		goto error;
	    }
	    continue;
	    break;
	default:
	    warnx("%s: Unsupported relocation type %d.",
		  obj->path, ELF_R_TYPE(rel->r_info));
	    goto error;
	    break;
	}

	len = write(fd, &pe, sizeof(Prebind_Entry));
	if (len == -1) {
	    warn("write(Prebind_Entry)");
	    goto error;
	}
	if (len != sizeof(Prebind_Entry)) {
	    warnx("short write");
	    goto error;
	}

#if 0
	printf("    \"%s\" r_info %04x, r_offset %#x, idx %d, st_value %#x, st_size %d\n",
	    obj->strtab +
	    ((const Elf_Sym *)(obj->symtab + ELF_R_SYM(rel->r_info)))->st_name,
	    pe.r_info, pe.r_offset, pe.index, pe.st_value, pe.st_size);
#endif
    }

    error = 0;
error:
    if (cache)
	munmap(cache, bytes);
    return (error);
}

static int
read_non_plt (Obj_Entry *obj, Obj_Entry **obj_list, Prebind_Object_Index *poi,
	      caddr_t addr, off_t *poff, off_t size)
{
    Prebind_Entry *pe;
    const Elf_Rel *rel;
    const Elf_Rel *rellim;
    Elf_Addr *where, target;
    int i;
    int retval;
    off_t off; 

    retval = -1;
    off = *poff;

    if (obj->textrel) {
	/* There are relocations to the write-protected text segment. */
	if (mprotect(obj->mapbase, obj->textsize,
		     PROT_READ|PROT_WRITE|PROT_EXEC) == -1) {
	    warn("%s: Cannot write-enable text segment", obj->path);
	    goto error;
	}
    }

    for (i = 0; i < poi->count; i++) {
	if (prebind_cklen(off, size, sizeof(Prebind_Entry)))
	    goto error;

	pe = (Prebind_Entry *)(addr + off);
	off += sizeof(Prebind_Entry);

	where = (Elf_Addr *) (obj->relocbase + pe->r_offset);
	target = (Elf_Addr) (obj_list[pe->index]->relocbase + pe->st_value);

	switch (ELF_R_TYPE(pe->r_info)) {
	case R_386_32:
	     *where += target;
	     break;
	case R_386_PC32:
	     *where += target - (Elf_Addr) where;
	     break;
	case R_386_GLOB_DAT:
	     *where = target;
	     break;
	default:
	     warnx("%s: Enexpected relocation type %d", obj->path,
		   ELF_R_TYPE(pe->r_info));
	     goto error;
	     break;
	}
    }

    /* While we're here, take care of all RELATIVE relocations. */
    rellim = (const Elf_Rel *) ((caddr_t) obj->rel + obj->relsize);
    for (rel = obj->rel; rel < rellim; rel++) {
	if (ELF_R_TYPE(rel->r_info) != R_386_RELATIVE)
	    continue;
	where = (Elf_Addr *) (obj->relocbase + rel->r_offset);
	*where += (Elf_Addr) obj->relocbase;
    }

    if (obj->textrel) {     /* Re-protected the text segment. */
	if (mprotect(obj->mapbase, obj->textsize, PROT_READ|PROT_EXEC) == -1) {
	    warn("%s: Cannot write-protect text segment", obj->path);
	    goto error;
	}
    }

    retval = 0;
error:
    *poff = off;
    return (retval);
}

static int
count_plt (Obj_Entry *obj)
{
    const Elf_Rel *rel;
    const Elf_Rel *rellim;
    int count;

    count = 0;
    rellim = (const Elf_Rel *)((caddr_t)obj->pltrel + obj->pltrelsize);
    for (rel = obj->pltrel; rel < rellim; rel++)
	if (ELF_R_TYPE(rel->r_info) == R_386_JMP_SLOT)
		count++;

    return (count);
}

static int
write_plt (int fd, Obj_Entry *obj, Obj_Entry **obj_list)
{
    const Elf_Rel *rel;
    const Elf_Rel *rellim;
    SymCache *cache;
    Prebind_Entry pe;
    ssize_t len;
    int bytes;
    int error;

    bytes = obj->nchains * sizeof(SymCache);
    error = -1;

    /*
     * The dynamic loader may be called from a thread, we have
     * limited amounts of stack available so we cannot use alloca().
     */
    cache = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_ANON, -1, 0);
    if (cache == MAP_FAILED)
	cache = NULL;

    rellim = (const Elf_Rel *)((caddr_t)obj->pltrel + obj->pltrelsize);
    for (rel = obj->pltrel; rel < rellim; rel++) {
	const Elf_Sym *defsym;
	const Obj_Entry *defobj;

	assert(ELF_R_TYPE(rel->r_info) == R_386_JMP_SLOT);

	pe.r_info = rel->r_info;
	pe.r_offset = rel->r_offset;
	pe.index = -1;
	pe.st_value = 0;
	pe.st_size = 0;

	defsym = find_symdef(ELF_R_SYM(rel->r_info), obj, &defobj, true, cache);
	if (defsym == NULL)
	    goto error;

	pe.index = obj2index(obj_list, defobj);
	pe.st_value = defsym->st_value;

	len = write(fd, &pe, sizeof(Prebind_Entry));
	if (len == -1) {
	    warn("write(Prebind_Entry)");
	    goto error;
	}
	if (len != sizeof(Prebind_Entry)) {
	    warnx("short write");
	    goto error;
	}
#if 0
	printf("    \"%s\" r_info %04x, r_offset %#x, idx %d, st_value %#x, st_size %d\n",
	    obj->strtab +
	    ((const Elf_Sym *)(obj->symtab + ELF_R_SYM(rel->r_info)))->st_name,
	    pe.r_info, pe.r_offset, pe.index, pe.st_value, pe.st_size);
#endif
    }

    error = 0;
error:
    if (cache)
	munmap(cache, bytes);
    return (error);
}

static int
read_plt (Obj_Entry *obj, Obj_Entry **obj_list, Prebind_Object_Index *poi,
	  caddr_t addr, off_t *poff, off_t size)
{
    Prebind_Entry *pe;
    Elf_Addr *where, target;
    int i;
    int retval;
    off_t off; 

    retval = -1;
    off = *poff;

    for (i = 0; i < poi->count; i++) {
	if (prebind_cklen(off, size, sizeof(Prebind_Entry)))
	    goto error;

	pe = (Prebind_Entry *)(addr + off);
	off += sizeof(Prebind_Entry);

	where = (Elf_Addr *) (obj->relocbase + pe->r_offset);
	target = (Elf_Addr) (obj_list[pe->index]->relocbase + pe->st_value);
	*where += (Elf_Addr) obj->relocbase;
	reloc_jmpslot(where, target);
    }

    retval = 0;
error:
    *poff = off;
    return (retval);
}

static int
count_copy (Obj_Entry *obj)
{
    const Elf_Rel *rel;
    const Elf_Rel *rellim;
    int count;

    count = 0;
    rellim = (const Elf_Rel *)((caddr_t)obj->rel + obj->relsize);
    for (rel = obj->rel; rel < rellim; rel++)
	if (ELF_R_TYPE(rel->r_info) == R_386_COPY)
		count++;

    return (count);
}

static int
write_copy (int fd, Obj_Entry *obj, Obj_Entry **obj_list)
{
    const Elf_Rel *rel;
    const Elf_Rel *rellim;
    Prebind_Entry pe;
    ssize_t len;
    int error;

    error = -1;

    rellim = (const Elf_Rel *)((caddr_t)obj->rel + obj->relsize);
    for (rel = obj->rel; rel < rellim; rel++) {
	const Elf_Sym *sym;
	const Elf_Sym *defsym;
	const Obj_Entry *defobj;
	const char *name;
	unsigned long hash;

	if (ELF_R_TYPE(rel->r_info) != R_386_COPY)
		continue;

	pe.r_info = rel->r_info;
	pe.r_offset = rel->r_offset;
	pe.index = -1;

	sym = obj->symtab + ELF_R_SYM(rel->r_info);
	hash = elf_hash(obj->strtab + sym->st_name);
	name = obj->strtab + sym->st_name;

	for (defobj = obj->next; defobj != NULL; defobj = defobj->next)     
	    if ((defsym = symlook_obj(name, hash, defobj, false)) != NULL)
		break;

	if (defobj == NULL) {
	     printf("Undefined symbol \"%s\" referenced from COPY"
		    " relocation in %s", name, obj->path);
	     goto error;
	}

	pe.index = obj2index(obj_list, defobj);
	pe.st_value = defsym->st_value;
	pe.st_size = sym->st_size;

	len = write(fd, &pe, sizeof(Prebind_Entry));
	if (len == -1) {
	    warn("write(Prebind_Entry)");
	    goto error;
	}
	if (len != sizeof(Prebind_Entry)) {
	    warnx("short write");
	    goto error;
	}
#if 0
	printf("    \"%s\" r_info %04x, r_offset %#x, idx %d, st_value %#x, st_size %d\n",
	    obj->strtab +
	    ((const Elf_Sym *)(obj->symtab + ELF_R_SYM(rel->r_info)))->st_name,
	    pe.r_info, pe.r_offset, pe.index, pe.st_value, pe.st_size);
#endif
    }

    error = 0;
error:
    return (error);
}

static int
read_copy (Obj_Entry *obj, Obj_Entry **obj_list, Prebind_Object_Index *poi,
	   caddr_t addr, off_t *poff, off_t size)
{
    Prebind_Entry *pe;
    Elf_Addr *where, target;
    int i;
    int retval;
    off_t off; 

    retval = -1;
    off = *poff;

    for (i = 0; i < poi->count; i++) {
	if (prebind_cklen(off, size, sizeof(Prebind_Entry)))
	    goto error;

	pe = (Prebind_Entry *)(addr + off);
	off += sizeof(Prebind_Entry);

	if (ELF_R_TYPE(pe->r_info) != R_386_COPY) {
	    warnx("Unexpected relocation type %d; expected R_386_COPY.",
		  ELF_R_TYPE(pe->r_info));
	    goto error;
	}

	where = (Elf_Addr *) (obj->relocbase + pe->r_offset);
	target = (Elf_Addr) (obj_list[pe->index]->relocbase + pe->st_value);
	memcpy(where, (void *)target, pe->st_size);
    }

    retval = 0;
error:
    *poff = off;
    return (retval);
}

#if 0
static void
dump_Elf_Rel (Obj_Entry *obj, const Elf_Rel *rel0, u_long relsize)
{
    const Elf_Rel *rel;
    const Elf_Rel *rellim;
    const Elf_Sym *sym;
    Elf_Addr *dstaddr;

    rellim = (const Elf_Rel *)((char *)rel0 + relsize);
    for (rel = rel0; rel < rellim; rel++) {
	dstaddr = (Elf_Addr *)(obj->relocbase + rel->r_offset);
	sym = obj->symtab + ELF_R_SYM(rel->r_info);
	printf("\t\"%s\" offset %p, info %04x, addr %#x, size %d\n",
	    obj->strtab + sym->st_name,
	    dstaddr, rel->r_info, *dstaddr, sym->st_size);
    }
    return;
}
#endif

static int
obj2index (Obj_Entry **obj_list, const Obj_Entry *obj)
{
    int i;

    for (i = 0; obj_list[i] != NULL; i++)
	if (obj == obj_list[i])
	    return (i);

    return (-1);
}
