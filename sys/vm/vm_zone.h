/*
 * Copyright (c) 1997, 1998 John S. Dyson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice immediately at the beginning of the file, without modification,
 *	this list of conditions, and the following disclaimer.
 * 2. Absolutely no warranty of function or purpose is made by the author
 *	John S. Dyson.
 *
 * $FreeBSD: src/sys/vm/vm_zone.h,v 1.13.2.2 2002/10/10 19:50:16 dillon Exp $
 * $DragonFly: src/sys/vm/vm_zone.h,v 1.10 2008/01/21 20:21:19 nth Exp $
 */

#ifndef _VM_VM_ZONE_H_
#define _VM_VM_ZONE_H_

#define ZONE_INTERRUPT 0x0001	/* If you need to allocate at int time */
#define ZONE_PANICFAIL 0x0002	/* panic if the zalloc fails */
#define ZONE_SPECIAL   0x0004	/* special vm_map_entry zone, see zget() */
#define ZONE_BOOT      0x0010	/* Internal flag used by zbootinit */
#define ZONE_USE_RESERVE 0x0020	/* use reserve memory if necessary */
#define ZONE_DESTROYABLE 0x0040 /* can be zdestroy()'ed */

#define ZONE_MAXPGLOAD	8	/* max VM pages burst in zget() */

#include <sys/spinlock.h>
#include <sys/thread.h>

/*
 * Zone allocator.
 * Zones are deprecated, use <sys/objcache.h> instead for new developments.
 * Zones are not thread-safe; the mp lock must be held while calling
 * zone functions.
 */
struct vm_zpcpu {
	void	*zitems;
	long	zfreecnt;
	long	znalloc;		/* allocations from pcpu */
} __cachealign;

typedef struct vm_zpcpu vm_zpcpu_t;

typedef struct vm_zone {
	struct spinlock zspin;		/* lock for global portion */
	vm_zpcpu_t	zpcpu[SMP_MAXCPU];
	void		*zitems;	/* linked list of items */
	long		zfreecnt;	/* free entries */
	long		zfreemin;	/* minimum number of free entries */
	long		znalloc;	/* allocations from global */
	vm_offset_t	zkva;		/* Base kva of zone */
	vm_pindex_t	zpagecount;	/* Total # of allocated pages */
	vm_pindex_t	zpagemax;	/* Max address space */
	long		zmax;		/* Max number of entries allocated */
	long		zmax_pcpu;	/* Max pcpu cache */
	long		ztotal;		/* Total entries allocated now */
	long		zsize;		/* size of each entry */
	long		zalloc;		/* hint for # of pages to alloc */
	long		zflags;		/* flags for zone */
	uint32_t	zallocflag;	/* flag for allocation */
	char		*zname;		/* name for diags */
	LIST_ENTRY(vm_zone) zlink;	/* link in zlist */

 	/*
 	 * The following fields track kmem_alloc()'ed blocks when
 	 * ZONE_DESTROYABLE set and normal zone (i.e. not ZONE_INTERRUPT nor
 	 * ZONE_SPECIAL).
 	 */
 	vm_offset_t	*zkmvec;	/* krealloc()'ed array */
	long		zkmcur;		/* next free slot in zkmvec */
	long		zkmmax;		/* # of slots in zkmvec */
} *vm_zone_t;


void		zerror (int) __dead2;
vm_zone_t	zinit (char *name, size_t size, long nentries, uint32_t flags);
int		zinitna (vm_zone_t z, char *name,
			     size_t size, long nentries, uint32_t flags);
void *		zalloc (vm_zone_t z);
void		zfree (vm_zone_t z, void *item);
void		zbootinit (vm_zone_t z, char *name, size_t size, void *item,
			       long nitems);
void		zdestroy(vm_zone_t z);

#endif	/* _VM_VM_ZONE_H_ */
