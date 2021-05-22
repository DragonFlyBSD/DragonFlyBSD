/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2003-2019 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vm_map.h	8.9 (Berkeley) 5/17/95
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * Virtual memory map module definitions.  The vm_map houses the pmap
 * structure which controls the mmu context for a process.
 */
#ifndef	_VM_VM_MAP_H_
#define	_VM_VM_MAP_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifdef _KERNEL
#ifndef _SYS_KERNEL_H_
#include <sys/kernel.h>	/* ticks */
#endif
#endif
#ifndef _SYS_TREE_H_
#include <sys/tree.h>
#endif
#ifndef _SYS_LOCK_H_
#include <sys/lock.h>
#endif
#ifndef _SYS_VKERNEL_H_
#include <sys/vkernel.h>
#endif
#ifndef _VM_VM_H_
#include <vm/vm.h>
#endif
#ifndef _MACHINE_PMAP_H_
#include <machine/pmap.h>
#endif
#ifndef _VM_VM_OBJECT_H_
#include <vm/vm_object.h>
#endif
#ifndef _SYS_NULL_H_
#include <sys/_null.h>
#endif

struct vm_map_rb_tree;
RB_PROTOTYPE(vm_map_rb_tree, vm_map_entry, rb_entry, rb_vm_map_compare);

typedef u_int vm_flags_t;
typedef u_int vm_eflags_t;

/*
 * Aux structure depends on map type and/or flags.
 */
union vm_map_aux {
	vm_offset_t avail_ssize;	/* amt can grow if this is a stack */
	vpte_t master_pde;		/* virtual page table root */
	struct cdev *dev;
	void	*map_aux;
};

/*
 * vm_map_entry identifiers, used as a debugging aid
 */
typedef enum {
	VM_SUBSYS_UNKNOWN,
	VM_SUBSYS_KMALLOC,
	VM_SUBSYS_STACK,
	VM_SUBSYS_IMGACT,
	VM_SUBSYS_EFI,
	VM_SUBSYS_RESERVED,
	VM_SUBSYS_INIT,
	VM_SUBSYS_PIPE,
	VM_SUBSYS_PROC,
	VM_SUBSYS_SHMEM,
	VM_SUBSYS_SYSMAP,
	VM_SUBSYS_MMAP,
	VM_SUBSYS_BRK,
	VM_SUBSYS_BOGUS,
	VM_SUBSYS_BUF,
	VM_SUBSYS_BUFDATA,
	VM_SUBSYS_GD,
	VM_SUBSYS_IPIQ,
	VM_SUBSYS_PVENTRY,
	VM_SUBSYS_PML4,
	VM_SUBSYS_MAPDEV,
	VM_SUBSYS_ZALLOC,

	VM_SUBSYS_DM,
	VM_SUBSYS_CONTIG,
	VM_SUBSYS_DRM,
	VM_SUBSYS_DRM_GEM,
	VM_SUBSYS_DRM_SCAT,
	VM_SUBSYS_DRM_VMAP,
	VM_SUBSYS_DRM_TTM,
	VM_SUBSYS_HAMMER,
	VM_SUBSYS_NVMM,

	VM_SUBSYS_VMPGHASH,

	VM_SUBSYS_LIMIT		/* end of list */
} vm_subsys_t;

#define UKSMAPOP_ADD		1
#define UKSMAPOP_REM		2
#define UKSMAPOP_FAULT		3

/*
 * vm_map backing structure for specifying multiple backings.  This
 * structure is NOT shared across pmaps but may be shared within a pmap.
 * The offset is cumulatively added from its parent, allowing easy splits
 * and merges.
 */
struct vm_map_backing {
	vm_offset_t	start;		/* start address in pmap */
	vm_offset_t	end;		/* end address in pmap */
	struct pmap	*pmap;		/* for vm_object extents */

	struct vm_map_backing	*backing_ba;	/* backing store */

	/*
	 * Keep track of extents, typically via a vm_object but for uksmaps
	 * this can also be based off of a process or lwp.
	 */
	TAILQ_ENTRY(vm_map_backing) entry;

	/*
	 * A vm_map_entry may reference an object, a submap, a uksmap, or a
	 * direct user-kernel shared map.
	 */
	union {
		struct vm_object *object;	/* vm_object */
		struct vm_map	*sub_map;	/* belongs to another map */
		int		(*uksmap)(struct vm_map_backing *entry,
					  int op,
					  struct cdev *dev,
					  vm_page_t fake);
		void		*map_object;	/* generic */
	};
	void			*aux_info;

	/*
	 * The offset field typically represents the absolute offset in the
	 * object, but can have other meanings for uksmaps.
	 */
	vm_ooffset_t		offset;
	uint32_t		flags;
	uint32_t		backing_count;	/* #entries backing us */
};

typedef struct vm_map_backing *vm_map_backing_t;

#define VM_MAP_BACK_EXCL_HEUR	0x00000001U

/*
 * Address map entries consist of start and end addresses, a VM object
 * (or sharing map) and offset into that object, and user-exported
 * inheritance and protection information.  Also included is control
 * information for virtual copy operations.
 *
 * The object information is now encapsulated in a vm_map_backing
 * structure which contains the backing store chain, if any.  This
 * structure is NOT shared.
 *
 * When used with MAP_STACK, avail_ssize is used to determine the limits
 * of stack growth.
 */
struct vm_map_entry {
	RB_ENTRY(vm_map_entry) rb_entry;
	union vm_map_aux aux;		/* auxillary data */
	struct vm_map_backing ba;	/* backing object chain */
	vm_eflags_t	eflags;		/* map entry flags */
	vm_maptype_t	maptype;	/* type of VM mapping */
	vm_prot_t	protection;	/* protection code */
	vm_prot_t	max_protection;	/* maximum protection */
	vm_inherit_t	inheritance;	/* inheritance */
	int		wired_count;	/* can be paged if = 0 */
	vm_subsys_t	id;		/* subsystem id */
};

typedef struct vm_map_entry *vm_map_entry_t;

#define MAPENT_FREELIST(ent)		(ent)->rb_entry.rbe_left

#define MAP_ENTRY_NOSYNC		0x0001
#define MAP_ENTRY_STACK			0x0002
#define MAP_ENTRY_COW			0x0004
#define MAP_ENTRY_NEEDS_COPY		0x0008
#define MAP_ENTRY_NOFAULT		0x0010
#define MAP_ENTRY_USER_WIRED		0x0020

#define MAP_ENTRY_BEHAV_NORMAL		0x0000	/* default behavior */
#define MAP_ENTRY_BEHAV_SEQUENTIAL	0x0040	/* expect sequential access */
#define MAP_ENTRY_BEHAV_RANDOM		0x0080	/* expect random access */
#define MAP_ENTRY_BEHAV_RESERVED	0x00C0	/* future use */

#define MAP_ENTRY_BEHAV_MASK		0x00C0

#define MAP_ENTRY_IN_TRANSITION		0x0100	/* entry being changed */
#define MAP_ENTRY_NEEDS_WAKEUP		0x0200	/* waiter's in transition */
#define MAP_ENTRY_NOCOREDUMP		0x0400	/* don't include in a core */
#define MAP_ENTRY_KSTACK		0x0800	/* guarded kernel stack */

/*
 * flags for vm_map_[un]clip_range()
 */
#define MAP_CLIP_NO_HOLES		0x0001

/*
 * This reserve count for vm_map_entry_reserve() should cover all nominal
 * single-insertion operations, including any necessary clipping.
 */
#define MAP_RESERVE_COUNT	4
#define MAP_RESERVE_SLOP	512
#define MAP_RESERVE_HYST	(MAP_RESERVE_SLOP - MAP_RESERVE_SLOP / 8)

/*
 * vm_map_lookup wflags
 */
#define FW_WIRED	0x0001
#define FW_DIDCOW	0x0002

static __inline u_char   
vm_map_entry_behavior(struct vm_map_entry *entry)
{                  
	return entry->eflags & MAP_ENTRY_BEHAV_MASK;
}

static __inline void
vm_map_entry_set_behavior(struct vm_map_entry *entry, u_char behavior)
{              
	entry->eflags = (entry->eflags & ~MAP_ENTRY_BEHAV_MASK) |
		(behavior & MAP_ENTRY_BEHAV_MASK);
}                       

/*
 * Virtual address range interlock
 *
 * Used by MADV_INVAL in vm_map.c, but it is unclear whether we still
 * need it with the vpagetable support removed.
 */
struct vm_map_ilock {
	struct vm_map_ilock *next;
	int	flags;
	vm_offset_t ran_beg;
	vm_offset_t ran_end;	/* non-inclusive */
};

#define ILOCK_WAITING	0x00000001

/*
 * Hinting mechanism used by vm_map_findspace() to figure out where to start
 * an iteration looking for a hole big enough for the requested allocation.
 * This can be important in situations where large amounts of kernel memory
 * are being managed.  For example, if the system is managing tens of
 * thousands of processes or threads.
 *
 * If a hint is present it guarantees that no compatible hole exists prior
 * to the (start) address.  The (start) address itself is not necessarily
 * a hole.
 */
#define VM_MAP_FFCOUNT	4
#define VM_MAP_FFMASK	(VM_MAP_FFCOUNT - 1)

struct vm_map_freehint {
	vm_offset_t	start;
	vm_offset_t	length;
	vm_offset_t	align;
	int		unused01;
};
typedef struct vm_map_freehint vm_map_freehint_t;

/*
 * A vm_map stores a red-black tree of map entries, indexed by address.
 *
 * NOTE: The vm_map structure can be hard-locked with the lockmgr lock
 *	 or soft-serialized with the token, or both.
 */
RB_HEAD(vm_map_rb_tree, vm_map_entry);

struct vm_map {
	struct		lock lock;	/* Lock for map data */
	struct vm_map_rb_tree rb_root;	/* Organize map entries */
	vm_offset_t	min_addr;	/* min address */
	vm_offset_t	max_addr;	/* max address */
	int		nentries;	/* Number of entries */
	unsigned int	timestamp;	/* Version number */
	vm_size_t	size;		/* virtual size */
	u_char		system_map;	/* Am I a system map? */
	u_char		freehint_newindex;
	u_char		unused02;
	u_char		unused03;
	vm_flags_t	flags;		/* flags for this vm_map */
	vm_map_freehint_t freehint[VM_MAP_FFCOUNT];
	struct pmap	*pmap;		/* Physical map */
	struct vm_map_ilock *ilock_base;/* interlocks */
	struct spinlock	ilock_spin;	/* interlocks (spinlock for) */
	struct lwkt_token token;	/* Soft serializer */
	vm_offset_t pgout_offset;	/* for RLIMIT_RSS scans */
};

typedef struct vm_map *vm_map_t;

/*
 * vm_flags_t values
 */
#define MAP_WIREFUTURE		0x0001	/* wire all future pages */

/* 
 * Shareable process virtual address space.
 *
 * Refd pointers from vmresident, proc
 */
struct vmspace {
	struct vm_map vm_map;	/* VM address map */
	struct pmap vm_pmap;	/* private physical map */
	int vm_flags;
	caddr_t vm_shm;		/* SYS5 shared memory private data XXX */
/* we copy from vm_startcopy on fork */
#define vm_startcopy vm_rssize
	segsz_t vm_rssize;	/* current resident set size in pages */
	segsz_t vm_swrss;	/* resident set size before last swap */
	segsz_t vm_tsize;	/* text size (bytes) */
	segsz_t vm_dsize;	/* data size (bytes) */
	segsz_t vm_ssize;	/* stack size (bytes) */
	caddr_t vm_taddr;	/* user virtual address of text XXX */
	caddr_t vm_daddr;	/* user virtual address of data XXX */
	caddr_t vm_maxsaddr;	/* user VA at max stack growth */
	caddr_t vm_minsaddr;	/* user VA at max stack growth */
#define vm_endcopy	vm_unused01
	int	vm_unused01;
	int	vm_unused02;
	int	vm_pagesupply;
	u_int	vm_holdcnt;	/* temporary hold count and exit sequencing */
	u_int	vm_refcnt;	/* normal ref count */
};

#define VM_REF_DELETED		0x80000000U

#define VMSPACE_EXIT1		0x0001	/* partial exit */
#define VMSPACE_EXIT2		0x0002	/* full exit */

#define VMSPACE_HOLDEXIT	0x80000000

/*
 * Resident executable holding structure.  A user program can take a snapshot
 * of just its VM address space (typically done just after dynamic link
 * libraries have completed loading) and register it as a resident 
 * executable associated with the program binary's vnode, which is also
 * locked into memory.  Future execs of the vnode will start with a copy
 * of the resident vmspace instead of running the binary from scratch,
 * avoiding both the kernel ELF loader *AND* all shared library mapping and
 * relocation code, and will call a different entry point (the stack pointer
 * is reset to the top of the stack) supplied when the vmspace was registered.
 */
struct vmresident {
	struct vnode	*vr_vnode;		/* associated vnode */
	TAILQ_ENTRY(vmresident) vr_link;	/* linked list of res sts */
	struct vmspace	*vr_vmspace;		/* vmspace to fork */
	intptr_t	vr_entry_addr;		/* registered entry point */
	struct sysentvec *vr_sysent;		/* system call vects */
	int		vr_id;			/* registration id */
	int		vr_refs;		/* temporary refs */
};

#ifdef _KERNEL
/*
 *	Macros:		vm_map_lock, etc.
 *	Function:
 *		Perform locking on the data portion of a map.  Note that
 *		these macros mimic procedure calls returning void.  The
 *		semicolon is supplied by the user of these macros, not
 *		by the macros themselves.  The macros can safely be used
 *		as unbraced elements in a higher level statement.
 */

#define ASSERT_VM_MAP_LOCKED(map)	KKASSERT(lockowned(&(map)->lock))

#ifdef DIAGNOSTIC
/* #define MAP_LOCK_DIAGNOSTIC 1 */
#ifdef MAP_LOCK_DIAGNOSTIC
#define	vm_map_lock(map) \
	do { \
		kprintf ("locking map LK_EXCLUSIVE: 0x%x\n", map); \
		if (lockmgr(&(map)->lock, LK_EXCLUSIVE) != 0) { \
			panic("vm_map_lock: failed to get lock"); \
		} \
		(map)->timestamp++; \
	} while(0)
#else
#define	vm_map_lock(map) \
	do { \
		if (lockmgr(&(map)->lock, LK_EXCLUSIVE) != 0) { \
			panic("vm_map_lock: failed to get lock"); \
		} \
		(map)->timestamp++; \
	} while(0)
#endif
#else
#define	vm_map_lock(map) \
	do { \
		lockmgr(&(map)->lock, LK_EXCLUSIVE); \
		(map)->timestamp++; \
	} while(0)
#endif /* DIAGNOSTIC */

#if defined(MAP_LOCK_DIAGNOSTIC)
#define	vm_map_unlock(map) \
	do { \
		kprintf ("locking map LK_RELEASE: 0x%x\n", map); \
		lockmgr(&(map)->lock, LK_RELEASE); \
	} while (0)
#define	vm_map_lock_read(map) \
	do { \
		kprintf ("locking map LK_SHARED: 0x%x\n", map); \
		lockmgr(&(map)->lock, LK_SHARED); \
	} while (0)
#define	vm_map_unlock_read(map) \
	do { \
		kprintf ("locking map LK_RELEASE: 0x%x\n", map); \
		lockmgr(&(map)->lock, LK_RELEASE); \
	} while (0)
#else
#define	vm_map_unlock(map) \
	lockmgr(&(map)->lock, LK_RELEASE)
#define	vm_map_lock_read(map) \
	lockmgr(&(map)->lock, LK_SHARED) 
#define	vm_map_unlock_read(map) \
	lockmgr(&(map)->lock, LK_RELEASE)
#endif

#define vm_map_lock_read_try(map) \
	lockmgr(&(map)->lock, LK_SHARED | LK_NOWAIT)

static __inline__ int
vm_map_lock_read_to(vm_map_t map)
{
	int error;

#if defined(MAP_LOCK_DIAGNOSTIC)
	kprintf ("locking map LK_SHARED: 0x%x\n", map);
#endif
	error = lockmgr(&(map)->lock, LK_SHARED | LK_TIMELOCK);
	return error;
}

static __inline__ int
vm_map_lock_upgrade(vm_map_t map) {
	int error;
#if defined(MAP_LOCK_DIAGNOSTIC)
	kprintf("locking map LK_EXCLUPGRADE: 0x%x\n", map); 
#endif
	error = lockmgr(&map->lock, LK_EXCLUPGRADE);
	if (error == 0)
		map->timestamp++;
	return error;
}

#if defined(MAP_LOCK_DIAGNOSTIC)
#define vm_map_lock_downgrade(map) \
	do { \
		kprintf ("locking map LK_DOWNGRADE: 0x%x\n", map); \
		lockmgr(&(map)->lock, LK_DOWNGRADE); \
	} while (0)
#else
#define vm_map_lock_downgrade(map) \
	lockmgr(&(map)->lock, LK_DOWNGRADE)
#endif

#endif /* _KERNEL */

/*
 *	Functions implemented as macros
 */
#define		vm_map_min(map)		((map)->min_addr)
#define		vm_map_max(map)		((map)->max_addr)
#define		vm_map_pmap(map)	((map)->pmap)

/*
 * Must not block
 */
static __inline struct pmap *
vmspace_pmap(struct vmspace *vmspace)
{
	return &vmspace->vm_pmap;
}

/*
 * Caller must hold the vmspace->vm_map.token
 */
static __inline long
vmspace_resident_count(struct vmspace *vmspace)
{
	return pmap_resident_count(vmspace_pmap(vmspace));
}

/*
 * Number of kernel maps and entries to statically allocate, required
 * during boot to bootstrap the VM system.
 */
#define MAX_KMAP	10
#define MAX_MAPENT	(SMP_MAXCPU * 32 + 1024)

/*
 * Copy-on-write flags for vm_map operations
 */
#define MAP_UNUSED_01		0x0001
#define MAP_COPY_ON_WRITE	0x0002
#define MAP_NOFAULT		0x0004
#define MAP_PREFAULT		0x0008
#define MAP_PREFAULT_PARTIAL	0x0010
#define MAP_DISABLE_SYNCER	0x0020
#define MAP_IS_STACK		0x0040
#define MAP_IS_KSTACK		0x0080
#define MAP_DISABLE_COREDUMP	0x0100
#define MAP_PREFAULT_MADVISE	0x0200	/* from (user) madvise request */
#define MAP_PREFAULT_RELOCK	0x0200

/*
 * vm_fault option flags
 */
#define VM_FAULT_NORMAL		0x00	/* Nothing special */
#define VM_FAULT_CHANGE_WIRING	0x01	/* Change the wiring as appropriate */
#define VM_FAULT_USER_WIRE	0x02	/* Likewise, but for user purposes */
#define VM_FAULT_BURST		0x04	/* Burst fault can be done */
#define VM_FAULT_DIRTY		0x08	/* Dirty the page */
#define VM_FAULT_UNSWAP		0x10	/* Remove backing store from the page */
#define VM_FAULT_BURST_QUICK	0x20	/* Special case shared vm_object */
#define VM_FAULT_WIRE_MASK	(VM_FAULT_CHANGE_WIRING|VM_FAULT_USER_WIRE)
#define VM_FAULT_USERMODE	0x40

#ifdef _KERNEL

boolean_t vm_map_check_protection (vm_map_t, vm_offset_t, vm_offset_t,
		vm_prot_t, boolean_t);
struct pmap;
struct globaldata;
void vm_map_entry_allocate_object(vm_map_entry_t);
void vm_map_entry_reserve_cpu_init(struct globaldata *gd);
int vm_map_entry_reserve(int);
int vm_map_entry_kreserve(int);
void vm_map_entry_release(int);
void vm_map_entry_krelease(int);
int vm_map_delete (vm_map_t, vm_offset_t, vm_offset_t, int *);
int vm_map_find (vm_map_t, void *, void *,
		 vm_ooffset_t, vm_offset_t *, vm_size_t,
		 vm_size_t, boolean_t,
		 vm_maptype_t, vm_subsys_t id,
		 vm_prot_t, vm_prot_t, int);
int vm_map_findspace (vm_map_t, vm_offset_t, vm_size_t, vm_size_t,
		      int, vm_offset_t *);
vm_offset_t vm_map_hint(struct proc *, vm_offset_t, vm_prot_t);
int vm_map_inherit (vm_map_t, vm_offset_t, vm_offset_t, vm_inherit_t);
void vm_map_init (struct vm_map *, vm_offset_t, vm_offset_t, pmap_t);
int vm_map_insert (vm_map_t, int *,
		   void *, void *,
		   vm_ooffset_t, void *,
		   vm_offset_t, vm_offset_t,
		   vm_maptype_t, vm_subsys_t id,
		   vm_prot_t, vm_prot_t, int);
int vm_map_lookup (vm_map_t *, vm_offset_t, vm_prot_t,
		vm_map_entry_t *, struct vm_map_backing **,
		vm_pindex_t *, vm_pindex_t *, vm_prot_t *, int *);
void vm_map_lookup_done (vm_map_t, vm_map_entry_t, int);
boolean_t vm_map_lookup_entry (vm_map_t, vm_offset_t, vm_map_entry_t *);
int vm_map_wire (vm_map_t, vm_offset_t, vm_offset_t, int);
int vm_map_unwire (vm_map_t, vm_offset_t, vm_offset_t, boolean_t);
int vm_map_clean (vm_map_t, vm_offset_t, vm_offset_t, boolean_t, boolean_t);
int vm_map_protect (vm_map_t, vm_offset_t, vm_offset_t, vm_prot_t, boolean_t);
int vm_map_remove (vm_map_t, vm_offset_t, vm_offset_t);
void vm_map_startup (void);
int vm_map_submap (vm_map_t, vm_offset_t, vm_offset_t, vm_map_t);
int vm_map_madvise (vm_map_t, vm_offset_t, vm_offset_t, int, off_t);
void vm_map_simplify_entry (vm_map_t, vm_map_entry_t, int *);
void vm_init2 (void);
int vm_uiomove (vm_map_t, vm_object_t, off_t, int, vm_offset_t, int *);
int vm_map_stack (vm_map_t, vm_offset_t *, vm_size_t, int,
		  vm_prot_t, vm_prot_t, int);
int vm_map_growstack (vm_map_t map, vm_offset_t addr);
vm_offset_t vmspace_swap_count (struct vmspace *vmspace);
vm_offset_t vmspace_anonymous_count (struct vmspace *vmspace);
void vm_map_set_wired_quick(vm_map_t map, vm_offset_t addr, vm_size_t size, int *);
void vm_map_transition_wait(vm_map_t map, int relock);

void vm_map_interlock(vm_map_t map, struct vm_map_ilock *ilock,
			vm_offset_t ran_beg, vm_offset_t ran_end);
void vm_map_deinterlock(vm_map_t map, struct vm_map_ilock *ilock);


#if defined(__x86_64__) && defined(_KERNEL_VIRTUAL)
int vkernel_module_memory_alloc(vm_offset_t *, size_t);
void vkernel_module_memory_free(vm_offset_t, size_t);
#endif

#endif
#endif				/* _VM_VM_MAP_H_ */
