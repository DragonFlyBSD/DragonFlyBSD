/**
 * \file drmP.h
 * Private header for Direct Rendering Manager
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DRM_P_H_
#define _DRM_P_H_

#if defined(_KERNEL) || defined(__KERNEL__)

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sglist.h>
#include <sys/stat.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/filio.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/queue.h>
#include <sys/signalvar.h>
#include <sys/poll.h>
#include <sys/sbuf.h>
#include <sys/taskqueue.h>
#include <sys/tree.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page2.h>
#include <vm/vm_pager.h>
#include <vm/vm_param.h>
#include <machine/param.h>
#include <machine/pmap.h>
#if defined(__i386__) || defined(__amd64__)
#include <machine/specialreg.h>
#endif
#include <machine/sysarch.h>
#include <sys/endian.h>
#include <sys/mman.h>
#include <sys/rman.h>
#include <sys/memrange.h>
#include <dev/agp/agpvar.h>
#include <sys/agpio.h>
#include <sys/mutex.h>

MALLOC_DECLARE(M_DRM);

#include <uapi_drm/drm.h>
#include <uapi_drm/drm_sarea.h>

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/idr.h>
#include <linux/pci.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/timer.h>
#include <asm/io.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <asm/uaccess.h>

#include <drm/drm_vma_manager.h>

struct drm_file;
struct drm_device;

#include <drm/drm_os_linux.h>
#include <drm/drm_hashtab.h>
#include <drm/drm_mm.h>

#define DRM_UT_CORE 		0x01
#define DRM_UT_DRIVER		0x02
#define DRM_UT_KMS		0x04
#define DRM_UT_PRIME		0x08

#include "opt_drm.h"
#ifdef DRM_DEBUG
#  if DRM_DEBUG>1
#    define DRM_DEBUG_DEFAULT_ON 2
#  else
#    define DRM_DEBUG_DEFAULT_ON 1
#  endif
#undef DRM_DEBUG
#endif /* DRM_DEBUG */

#define	DRM_DEBUGBITS_DEBUG		0x1
#define	DRM_DEBUGBITS_KMS		0x2
#define	DRM_DEBUGBITS_FAILED_IOCTL	0x4
#define	DRM_DEBUGBITS_VERBOSE		0x8

#undef DRM_LINUX
#define DRM_LINUX 0

/***********************************************************************/
/** \name DRM template customization defaults */
/*@{*/

/* driver capabilities and requirements mask */
#define DRIVER_USE_AGP     0x1
#define DRIVER_REQUIRE_AGP 0x2
#define DRIVER_PCI_DMA     0x8
#define DRIVER_SG          0x10
#define DRIVER_HAVE_DMA    0x20
#define DRIVER_HAVE_IRQ    0x40
#define DRIVER_IRQ_SHARED  0x80
#define DRIVER_IRQ_VBL     0x100
#define DRIVER_DMA_QUEUE   0x200
#define DRIVER_FB_DMA      0x400
#define DRIVER_IRQ_VBL2    0x800
#define DRIVER_GEM         0x1000
#define DRIVER_MODESET     0x2000
#define DRIVER_PRIME       0x4000

/***********************************************************************/
/** \name Begin the DRM... */
/*@{*/

#define DRM_MAGIC_HASH_ORDER  4  /**< Size of key hash table. Must be power of 2. */
#define DRM_KERNEL_CONTEXT    0	 /* Change drm_resctx if changed	  */
#define DRM_RESERVED_CONTEXTS 1	 /* Change drm_resctx if changed	  */

#define	DRM_GEM_MAPPING_MASK	(3ULL << 62)
#define	DRM_GEM_MAPPING_KEY	(2ULL << 62) /* Non-canonical address form */
#define	DRM_GEM_MAX_IDX		0x3fffff
#define	DRM_GEM_MAPPING_IDX(o)	(((o) >> 40) & DRM_GEM_MAX_IDX)
#define	DRM_GEM_MAPPING_OFF(i)	(((uint64_t)(i)) << 40)
#define	DRM_GEM_MAPPING_MAPOFF(o) \
    ((o) & ~(DRM_GEM_MAPPING_OFF(DRM_GEM_MAX_IDX) | DRM_GEM_MAPPING_KEY))

SYSCTL_DECL(_hw_drm);

#define DRM_MAX_CTXBITMAP (PAGE_SIZE * 8)

/***********************************************************************/
/** \name Internal types and structures */
/*@{*/

#define DRM_ARRAY_SIZE(x) NELEM(x)
#define DRM_MIN(a,b) ((a)<(b)?(a):(b))
#define DRM_MAX(a,b) ((a)>(b)?(a):(b))

#define DRM_IF_VERSION(maj, min) (maj << 16 | min)

#define __OS_HAS_AGP	1

#define DRM_DEV_MODE	(S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP)
#define DRM_DEV_UID	0
#define DRM_DEV_GID	0

#define DRM_WAKEUP(w)		wakeup((void *)w)
#define DRM_WAKEUP_INT(w)	wakeup(w)
#define DRM_INIT_WAITQUEUE(queue) do {(void)(queue);} while (0)

#define DRM_CURPROC		curthread
#define DRM_STRUCTPROC		struct thread
#define DRM_CURRENTPID		(curproc != NULL ? curproc->p_pid : -1)
#define DRM_LOCK(dev)		lockmgr(&(dev)->struct_mutex, LK_EXCLUSIVE)
#define DRM_UNLOCK(dev)		lockmgr(&(dev)->struct_mutex, LK_RELEASE)
#define	DRM_LOCK_SLEEP(dev, chan, flags, msg, timeout)			\
    (lksleep((chan), &(dev)->struct_mutex, (flags), (msg), (timeout)))
#if defined(INVARIANTS)
#define	DRM_LOCK_ASSERT(dev)	KKASSERT(lockstatus(&(dev)->struct_mutex, curthread) != 0);
#define	DRM_UNLOCK_ASSERT(dev)	KKASSERT(lockstatus(&(dev)->struct_mutex, curthread) == 0);
#else
#define	DRM_LOCK_ASSERT(d)
#define	DRM_UNLOCK_ASSERT(d)
#endif

#define DRM_SYSCTL_HANDLER_ARGS	(SYSCTL_HANDLER_ARGS)

#define DRM_IRQ_ARGS		void *arg
typedef void			irqreturn_t;
#define IRQ_HANDLED		/* nothing */
#define IRQ_NONE		/* nothing */

enum {
	DRM_IS_NOT_AGP,
	DRM_IS_AGP,
	DRM_MIGHT_BE_AGP
};
#define DRM_AGP_MEM		struct agp_memory_info

#define drm_get_device_from_kdev(_kdev) (_kdev->si_drv1)

/* DRM_SUSER returns true if the user is superuser */
#define DRM_SUSER(p)		(priv_check(p, PRIV_DRIVER) == 0)
#define DRM_AGP_FIND_DEVICE()	agp_find_device()
#define DRM_MTRR_WC		MDF_WRITECOMBINE

/* DRM_READMEMORYBARRIER() prevents reordering of reads.
 * DRM_WRITEMEMORYBARRIER() prevents reordering of writes.
 * DRM_MEMORYBARRIER() prevents reordering of reads and writes.
 */
#define DRM_READMEMORYBARRIER()		cpu_lfence()
#define DRM_WRITEMEMORYBARRIER()	cpu_sfence()
#define DRM_MEMORYBARRIER()		cpu_mfence()

#define DRM_VERIFYAREA_READ( uaddr, size )		\
	(!useracc(__DECONST(caddr_t, uaddr), size, VM_PROT_READ))

#define DRM_COPY_TO_USER(user, kern, size) \
	copyout(kern, user, size)
#define DRM_COPY_FROM_USER(kern, user, size) \
	copyin(user, kern, size)
#define DRM_COPY_FROM_USER_UNCHECKED(arg1, arg2, arg3) 	\
	copyin(arg2, arg1, arg3)
#define DRM_COPY_TO_USER_UNCHECKED(arg1, arg2, arg3)	\
	copyout(arg2, arg1, arg3)
#define DRM_GET_USER_UNCHECKED(val, uaddr)		\
	((val) = fuword32(uaddr), 0)

#define	drm_can_sleep()	(DRM_HZ & 1)

#define DRM_GET_PRIV_SAREA(_dev, _ctx, _map) do {	\
	(_map) = (_dev)->context_sareas[_ctx];		\
} while(0)

#define LOCK_TEST_WITH_RETURN(dev, file_priv)				\
do {									\
	if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock) ||		\
	     dev->lock.file_priv != file_priv) {			\
		DRM_ERROR("%s called without lock held\n",		\
			   __FUNCTION__);				\
		return EINVAL;						\
	}								\
} while (0)

/* Returns -errno to shared code */
#define DRM_WAIT_ON( ret, queue, timeout, condition )		\
for ( ret = 0 ; !ret && !(condition) ; ) {			\
	DRM_UNLOCK(dev);					\
	lwkt_serialize_enter(&dev->irq_lock);			\
	if (!(condition)) {					\
            tsleep_interlock(&(queue), PCATCH);			\
            lwkt_serialize_exit(&dev->irq_lock);		\
            ret = -tsleep(&(queue), PCATCH | PINTERLOCKED,	\
			  "drmwtq", (timeout));			\
        } else {                                                \
                lwkt_serialize_exit(&dev->irq_lock);            \
        }                                                 	\
	DRM_LOCK(dev);						\
}

__inline static void
vm_page_unhold_pages(vm_page_t *ma, int count)
{
	int i;

	for (i = 0; i < count; i++)
		vm_page_unhold(ma[i]);
}

int vm_phys_fictitious_reg_range(vm_paddr_t start, vm_paddr_t end,
    vm_memattr_t memattr);
void vm_phys_fictitious_unreg_range(vm_paddr_t start, vm_paddr_t end);
vm_page_t vm_phys_fictitious_to_vm_page(vm_paddr_t pa);

/***********************************************************************/
/** \name Macros to make printk easier */
/*@{*/

/**
 * Error output.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_ERROR(fmt, ...) \
	kprintf("error: [" DRM_NAME ":pid%d:%s] *ERROR* " fmt,		\
	    DRM_CURRENTPID, __func__ , ##__VA_ARGS__)

#define DRM_INFO(fmt, ...)  kprintf("info: [" DRM_NAME "] " fmt , ##__VA_ARGS__)

#define DRM_INFO_ONCE(fmt, ...)				\
	printk_once(KERN_INFO "[" DRM_NAME "] " fmt, ##__VA_ARGS__)

#define DRM_DEBUG(fmt, ...) do {					\
	if ((drm_debug & DRM_DEBUGBITS_DEBUG) != 0)		\
		kprintf("[" DRM_NAME ":pid%d:%s] " fmt, DRM_CURRENTPID,	\
			__func__ , ##__VA_ARGS__);			\
} while (0)

#define DRM_DEBUG_VERBOSE(fmt, ...) do {				\
	if ((drm_debug & DRM_DEBUGBITS_VERBOSE) != 0)		\
		kprintf("[" DRM_NAME ":pid%d:%s] " fmt, DRM_CURRENTPID,	\
			__func__ , ##__VA_ARGS__);			\
} while (0)

#define DRM_DEBUG_KMS(fmt, ...) do {					\
	if ((drm_debug & DRM_DEBUGBITS_KMS) != 0)			\
		kprintf("[" DRM_NAME ":KMS:pid%d:%s] " fmt, DRM_CURRENTPID,\
			__func__ , ##__VA_ARGS__);			\
} while (0)

#define DRM_DEBUG_DRIVER(fmt, ...) do {					\
	if ((drm_debug & DRM_DEBUGBITS_KMS) != 0)			\
		kprintf("[" DRM_NAME ":KMS:pid%d:%s] " fmt, DRM_CURRENTPID,\
			__func__ , ##__VA_ARGS__);			\
} while (0)

#define DRM_LOG_KMS(fmt, ...) do {					\
	kprintf("[" DRM_NAME ":KMS:pid%d:%s] " fmt, DRM_CURRENTPID,	\
			__func__ , ##__VA_ARGS__);			\
} while (0)

#define	dev_dbg(dev, fmt, ...) do {					\
	if ((drm_debug& DRM_DEBUGBITS_KMS) != 0) {			\
		device_printf((dev), "debug: " fmt, ## __VA_ARGS__);	\
	}								\
} while (0)

typedef struct drm_pci_id_list
{
	int vendor;
	int device;
	long driver_private;
	char *name;
} drm_pci_id_list_t;

struct drm_msi_blacklist_entry
{
	int vendor;
	int device;
};

/**
 * Ioctl function type.
 *
 * \param inode device inode.
 * \param file_priv DRM file private pointer.
 * \param cmd command.
 * \param arg argument.
 */
typedef int drm_ioctl_t(struct drm_device *dev, void *data,
			struct drm_file *file_priv);

typedef int drm_ioctl_compat_t(struct file *filp, unsigned int cmd,
			       unsigned long arg);

#define DRM_IOCTL_NR(n)                ((n) & 0xff)

#define DRM_AUTH	0x1
#define DRM_MASTER	0x2
#define DRM_ROOT_ONLY	0x4
#define DRM_CONTROL_ALLOW 0x8
#define DRM_UNLOCKED	0x10

struct drm_ioctl_desc {
	unsigned long cmd;
	int (*func)(struct drm_device *dev, void *data,
		    struct drm_file *file_priv);
	int flags;
	unsigned int cmd_drv;
};

/**
 * Creates a driver or general drm_ioctl_desc array entry for the given
 * ioctl, for use by drm_ioctl().
 */
#define DRM_IOCTL_DEF_DRV(ioctl, _func, _flags)			\
	[DRM_IOCTL_NR(DRM_##ioctl)] = {.cmd = DRM_##ioctl, .func = _func, .flags = _flags, .cmd_drv = DRM_IOCTL_##ioctl}

typedef struct drm_magic_entry {
	struct list_head head;
	struct drm_hash_item hash_item;
	struct drm_file	       *priv;
	struct drm_magic_entry *next;
} drm_magic_entry_t;

typedef struct drm_magic_head {
	struct drm_magic_entry *head;
	struct drm_magic_entry *tail;
} drm_magic_head_t;

typedef struct drm_buf {
	int idx;		       /**< Index into master buflist */
	int total;		       /**< Buffer size */
	int order;		       /**< log-base-2(total) */
	int used;		       /**< Amount of buffer in use (for DMA) */
	unsigned long offset;	       /**< Byte offset (used internally) */
	void *address;		       /**< Address of buffer */
	unsigned long bus_address;     /**< Bus address of buffer */
	struct drm_buf *next;	       /**< Kernel-only: used for free list */
	__volatile__ int waiting;      /**< On kernel DMA queue */
	__volatile__ int pending;      /**< On hardware DMA queue */
	struct drm_file *file_priv;    /**< Private of holding file descr */
	int context;		       /**< Kernel queue for this buffer */
	int while_locked;	       /**< Dispatch this buffer while locked */
	enum {
		DRM_LIST_NONE = 0,
		DRM_LIST_FREE = 1,
		DRM_LIST_WAIT = 2,
		DRM_LIST_PEND = 3,
		DRM_LIST_PRIO = 4,
		DRM_LIST_RECLAIM = 5
	} list;			       /**< Which list we're on */

	int dev_priv_size;		 /**< Size of buffer private storage */
	void *dev_private;		 /**< Per-buffer private storage */
} drm_buf_t;

struct drm_freelist {
	int		  initialized; /* Freelist in use		   */
	atomic_t	  count;       /* Number of free buffers	   */
	drm_buf_t	  *next;       /* End pointer			   */

	wait_queue_head_t waiting;     /**< Processes waiting on free bufs */
	int		  low_mark;    /* Low water mark		   */
	int		  high_mark;   /* High water mark		   */
};

typedef struct drm_dma_handle {
	void *vaddr;
	size_t size;
	bus_addr_t busaddr;
	bus_dma_tag_t tag;
	bus_dmamap_t map;
} drm_dma_handle_t;

typedef struct drm_buf_entry {
	int		  buf_size;
	int		  buf_count;
	drm_buf_t	  *buflist;
	int		  seg_count;
	drm_dma_handle_t  **seglist;
	int		  page_order;

	struct drm_freelist freelist;
} drm_buf_entry_t;

/* Event queued up for userspace to read */
struct drm_pending_event {
	struct drm_event *event;
	struct list_head link;
	struct drm_file *file_priv;
	pid_t pid; /* pid of requester, no guarantee it's valid by the time
		      we deliver the event, for tracing only */
	void (*destroy)(struct drm_pending_event *event);
};

/* initial implementaton using a linked list - todo hashtab */
struct drm_prime_file_private {
	struct list_head head;
#ifdef DUMBBELL_WIP
	struct mutex lock;
#endif /* DUMBBELL_WIP */
};

/** File private data */
struct drm_file {
	int authenticated;
	struct drm_device *dev;
	int		  master;
	pid_t		  pid;
	uid_t		  uid;
	drm_magic_t	  magic;
	unsigned long	  ioctl_count;
	struct list_head lhead;
	struct kqinfo	  dkq;

	/** Mapping of mm object handles to object pointers. */
	struct idr object_idr;
	/** Lock for synchronization of access to object_idr. */
	struct lock table_lock;

	void		 *driver_priv;

	int		  is_master;
	struct drm_master *masterp;

	/* true when the client has asked us to expose stereo 3D mode flags */
	bool stereo_allowed;

	/**
	 * fbs - List of framebuffers associated with this file.
	 *
	 * Protected by fbs_lock. Note that the fbs list holds a reference on
	 * the fb object to prevent it from untimely disappearing.
	 */
	struct list_head fbs;
	struct lock fbs_lock;

	wait_queue_head_t event_wait;
	struct list_head  event_list;
	int		  event_space;

	struct drm_prime_file_private prime;
};

/**
 * Lock data.
 */
struct drm_lock_data {
	struct drm_hw_lock *hw_lock;	/**< Hardware lock */
	/** Private of lock holder's file (NULL=kernel) */
	struct drm_file *file_priv;
	wait_queue_head_t lock_queue;	/**< Queue of blocked processes */
	unsigned long lock_time;	/**< Time of last lock in jiffies */
};

/* This structure, in the struct drm_device, is always initialized while the
 * device
 * is open.  dev->dma_lock protects the incrementing of dev->buf_use, which
 * when set marks that no further bufs may be allocated until device teardown
 * occurs (when the last open of the device has closed).  The high/low
 * watermarks of bufs are only touched by the X Server, and thus not
 * concurrently accessed, so no locking is needed.
 */
typedef struct drm_device_dma {

	struct drm_buf_entry bufs[DRM_MAX_ORDER + 1];	/**< buffers, grouped by their size order */
	int buf_count;			/**< total number of buffers */
	struct drm_buf **buflist;		/**< Vector of pointers into drm_device_dma::bufs */
	int seg_count;
	int page_count;			/**< number of pages */
	unsigned long *pagelist;	/**< page list */
	unsigned long byte_count;
	enum {
		_DRM_DMA_USE_AGP = 0x01,
		_DRM_DMA_USE_SG = 0x02,
		_DRM_DMA_USE_FB = 0x04,
		_DRM_DMA_USE_PCI_RO = 0x08
	} flags;

} drm_device_dma_t;

typedef struct drm_agp_mem {
	void               *handle;
	unsigned long      bound; /* address */
	int                pages;
	struct drm_agp_mem *prev;
	struct drm_agp_mem *next;
} drm_agp_mem_t;

typedef struct drm_agp_head {
	device_t	   agpdev;
	struct agp_info    agp_info;
	const char         *chipset;
	drm_agp_mem_t      *memory;
	unsigned long      mode;
	int                enabled;
	int                acquired;
	unsigned long      base;
	int		   agp_mtrr;
	int		   cant_use_aperture;
	unsigned long	   page_mask;
} drm_agp_head_t;

typedef struct drm_sg_mem {
	vm_offset_t vaddr;
	vm_paddr_t *busaddr;
	vm_pindex_t pages;
} drm_sg_mem_t;

/**
 * Kernel side of a mapping
 */
struct drm_local_map {
	resource_size_t offset;	 /**< Requested physical address (0 for SAREA)*/
	unsigned long size;	 /**< Requested physical size (bytes) */
	enum drm_map_type type;	 /**< Type of memory to map */
	enum drm_map_flags flags;	 /**< Flags */
	void *handle;		 /**< User-space: "Handle" to pass to mmap() */
				 /**< Kernel-space: kernel-virtual address */
	int mtrr;		 /**< MTRR slot used */
};

typedef struct drm_local_map drm_local_map_t;

/**
 * Mappings list
 */
struct drm_map_list {
	struct list_head head;		/**< list head */
	struct drm_hash_item hash;
	struct drm_local_map *map;	/**< mapping */
	uint64_t user_token;
	struct drm_master *master;
	struct drm_mm_node *file_offset_node;	/**< fake offset */
};

/**
 * Context handle list
 */
struct drm_ctx_list {
	struct list_head head;		/**< list head */
	drm_context_t handle;		/**< context handle */
	struct drm_file *tag;		/**< associated fd private data */
};

struct drm_vblank_info {
	wait_queue_head_t queue;	/* vblank wait queue */
	atomic_t count;			/* number of VBLANK interrupts */
					/* (driver must alloc the right number of counters) */
	atomic_t refcount;		/* number of users of vblank interrupts */
	u32 last;			/* protected by dev->vbl_lock, used */
					/* for wraparound handling */
	int enabled;			/* so we don't call enable more than */
					/* once per disable */
	int inmodeset;			/* Display driver is setting mode */
};

/* location of GART table */
#define DRM_ATI_GART_MAIN 1
#define DRM_ATI_GART_FB   2

#define DRM_ATI_GART_PCI 1
#define DRM_ATI_GART_PCIE 2
#define DRM_ATI_GART_IGP 3

struct drm_ati_pcigart_info {
	int gart_table_location;
	int gart_reg_if;
	void *addr;
	dma_addr_t bus_addr;
	dma_addr_t table_mask;
	dma_addr_t member_mask;
	struct drm_dma_handle *table_handle;
	drm_local_map_t mapping;
	int table_size;
	struct drm_dma_handle *dmah; /* handle for ATI PCIGART table */
};

/**
 * GEM specific mm private for tracking GEM objects
 */
struct drm_gem_mm {
	struct drm_vma_offset_manager vma_manager;
	struct drm_mm offset_manager;	/**< Offset mgmt for buffer objects */
	struct drm_open_hash offset_hash; /**< User token hash table for maps */
	struct unrhdr *idxunr;
};

struct drm_gem_object {
	/** Reference count of this object */
	struct kref refcount;

	/** Handle count of this object. Each handle also holds a reference */
	atomic_t handle_count; /* number of handles on this object */

	/** Related drm device */
	struct drm_device *dev;

	/** File representing the shmem storage: filp in Linux parlance */
	vm_object_t vm_obj;

	/* Mapping info for this object */
	struct drm_vma_offset_node vma_node;
	bool on_map;
	struct drm_hash_item map_list;

	/**
	 * Size of the object, in bytes.  Immutable over the object's
	 * lifetime.
	 */
	size_t size;

	/**
	 * Global name for this object, starts at 1. 0 means unnamed.
	 * Access is covered by the object_name_lock in the related drm_device
	 */
	int name;

	/**
	 * Memory domains. These monitor which caches contain read/write data
	 * related to the object. When transitioning from one set of domains
	 * to another, the driver is called to ensure that caches are suitably
	 * flushed and invalidated
	 */
	uint32_t read_domains;
	uint32_t write_domain;

	/**
	 * While validating an exec operation, the
	 * new read/write domain values are computed here.
	 * They will be transferred to the above values
	 * at the point that any cache flushing occurs
	 */
	uint32_t pending_read_domains;
	uint32_t pending_write_domain;

#ifdef DUMBBELL_WIP
	/* dma buf exported from this GEM object */
	struct dma_buf *export_dma_buf;

	/* dma buf attachment backing this object */
	struct dma_buf_attachment *import_attach;
#endif /* DUMBBELL_WIP */
};

#include "drm_crtc.h"

/* per-master structure */
struct drm_master {

	struct kref refcount; /* refcount for this master */

	struct list_head head; /**< each minor contains a list of masters */
	struct drm_minor *minor; /**< link back to minor we are a master for */

	char *unique;			/**< Unique identifier: e.g., busid */
	int unique_len;			/**< Length of unique field */
	int unique_size;		/**< amount allocated */

	int blocked;			/**< Blocked due to VC switch? */

	/** \name Authentication */
	/*@{ */
	struct drm_open_hash magiclist;
	struct list_head magicfree;
	/*@} */

	struct drm_lock_data lock;	/**< Information on hardware lock */

	void *driver_priv; /**< Private structure for driver to use */
};

/* Size of ringbuffer for vblank timestamps. Just double-buffer
 * in initial implementation.
 */
#define DRM_VBLANKTIME_RBSIZE 2

/* Flags and return codes for get_vblank_timestamp() driver function. */
#define DRM_CALLED_FROM_VBLIRQ 1
#define DRM_VBLANKTIME_SCANOUTPOS_METHOD (1 << 0)
#define DRM_VBLANKTIME_INVBL             (1 << 1)

/* get_scanout_position() return flags */
#define DRM_SCANOUTPOS_VALID        (1 << 0)
#define DRM_SCANOUTPOS_INVBL        (1 << 1)
#define DRM_SCANOUTPOS_ACCURATE     (1 << 2)

#ifndef DMA_BIT_MASK
#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : (1ULL<<(n)) - 1)
#endif

#define upper_32_bits(n) ((u32)(((n) >> 16) >> 16))

struct drm_driver {
	int	(*load)(struct drm_device *, unsigned long flags);
	int	(*use_msi)(struct drm_device *, unsigned long flags);
	int	(*firstopen)(struct drm_device *);
	int	(*open)(struct drm_device *, struct drm_file *);
	void	(*preclose)(struct drm_device *, struct drm_file *file_priv);
	void	(*postclose)(struct drm_device *, struct drm_file *);
	void	(*lastclose)(struct drm_device *);
	int	(*unload)(struct drm_device *);
	void	(*reclaim_buffers_locked)(struct drm_device *,
					  struct drm_file *file_priv);
	int	(*dma_ioctl)(struct drm_device *dev, void *data,
			     struct drm_file *file_priv);
	void	(*dma_ready)(struct drm_device *);
	int	(*dma_quiescent)(struct drm_device *);
	int	(*dma_flush_block_and_flush)(struct drm_device *, int context,
					     enum drm_lock_flags flags);
	int	(*dma_flush_unblock)(struct drm_device *, int context,
				     enum drm_lock_flags flags);
	int	(*context_ctor)(struct drm_device *dev, int context);
	int	(*context_dtor)(struct drm_device *dev, int context);
	int	(*kernel_context_switch)(struct drm_device *dev, int old,
					 int new);
	int	(*kernel_context_switch_unlock)(struct drm_device *dev);
	void	(*irq_preinstall)(struct drm_device *dev);
	int	(*irq_postinstall)(struct drm_device *dev);
	void	(*irq_uninstall)(struct drm_device *dev);
	void	(*irq_handler)(DRM_IRQ_ARGS);

	u32	(*get_vblank_counter)(struct drm_device *dev, int crtc);
	int	(*enable_vblank)(struct drm_device *dev, int crtc);
	void	(*disable_vblank)(struct drm_device *dev, int crtc);

	/**
	 * Called by vblank timestamping code.
	 *
	 * Return the current display scanout position from a crtc, and an
	 * optional accurate ktime_get timestamp of when position was measured.
	 *
	 * \param dev  DRM device.
	 * \param crtc Id of the crtc to query.
	 * \param flags Flags from the caller (DRM_CALLED_FROM_VBLIRQ or 0).
	 * \param *vpos Target location for current vertical scanout position.
	 * \param *hpos Target location for current horizontal scanout position.
	 * \param *stime Target location for timestamp taken immediately before
	 *               scanout position query. Can be NULL to skip timestamp.
	 * \param *etime Target location for timestamp taken immediately after
	 *               scanout position query. Can be NULL to skip timestamp.
	 *
	 * Returns vpos as a positive number while in active scanout area.
	 * Returns vpos as a negative number inside vblank, counting the number
	 * of scanlines to go until end of vblank, e.g., -1 means "one scanline
	 * until start of active scanout / end of vblank."
	 *
	 * \return Flags, or'ed together as follows:
	 *
	 * DRM_SCANOUTPOS_VALID = Query successful.
	 * DRM_SCANOUTPOS_INVBL = Inside vblank.
	 * DRM_SCANOUTPOS_ACCURATE = Returned position is accurate. A lack of
	 * this flag means that returned position may be offset by a constant
	 * but unknown small number of scanlines wrt. real scanout position.
	 *
	 */
	int (*get_scanout_position) (struct drm_device *dev, int crtc,
				     unsigned int flags,
				     int *vpos, int *hpos, ktime_t *stime,
				     ktime_t *etime);

	int	(*get_vblank_timestamp)(struct drm_device *dev, int crtc,
		    int *max_error, struct timeval *vblank_time,
		    unsigned flags);

	void	(*gem_free_object)(struct drm_gem_object *obj);
	int	(*gem_open_object)(struct drm_gem_object *, struct drm_file *);
	void	(*gem_close_object)(struct drm_gem_object *, struct drm_file *);

	struct cdev_pager_ops *gem_pager_ops;

	int	(*dumb_create)(struct drm_file *file_priv,
		    struct drm_device *dev, struct drm_mode_create_dumb *args);
	int	(*dumb_map_offset)(struct drm_file *file_priv,
		    struct drm_device *dev, uint32_t handle, uint64_t *offset);
	int	(*dumb_destroy)(struct drm_file *file_priv,
		    struct drm_device *dev, uint32_t handle);

	int	(*sysctl_init)(struct drm_device *dev,
		    struct sysctl_ctx_list *ctx, struct sysctl_oid *top);
	void	(*sysctl_cleanup)(struct drm_device *dev);

	drm_pci_id_list_t *id_entry;	/* PCI ID, name, and chipset private */

	/**
	 * Called by \c drm_device_is_agp.  Typically used to determine if a
	 * card is really attached to AGP or not.
	 *
	 * \param dev  DRM device handle
	 *
	 * \returns
	 * One of three values is returned depending on whether or not the
	 * card is absolutely \b not AGP (return of 0), absolutely \b is AGP
	 * (return of 1), or may or may not be AGP (return of 2).
	 */
	int	(*device_is_agp) (struct drm_device * dev);

	int	buf_priv_size;

	int	major;
	int	minor;
	int	patchlevel;
	const char *name;		/* Simple driver name		   */
	const char *desc;		/* Longer driver name		   */
	const char *date;		/* Date of last major changes.	   */

	u32 driver_features;
	int dev_priv_size;
	struct drm_ioctl_desc *ioctls;
	int num_ioctls;
};

/**
 * DRM minor structure. This structure represents a drm minor number.
 */
struct drm_minor {
	int index;			/**< Minor device number */
	int type;                       /**< Control or render */
	device_t kdev;			/**< OS device */
	struct drm_device *dev;

	struct drm_master *master; /* currently active master for this node */
	struct list_head master_list;
	struct drm_mode_group mode_group;
};

/* mode specified on the command line */
struct drm_cmdline_mode {
	bool specified;
	bool refresh_specified;
	bool bpp_specified;
	int xres, yres;
	int bpp;
	int refresh;
	bool rb;
	bool interlace;
	bool cvt;
	bool margins;
	enum drm_connector_force force;
};


struct drm_pending_vblank_event {
	struct drm_pending_event base;
	int pipe;
	struct drm_event_vblank event;
};

struct drm_sysctl_info {
	struct sysctl_ctx_list ctx;
	char   name[2];
};

/* Length for the array of resource pointers for drm_get_resource_*. */
#define DRM_MAX_PCI_RESOURCE	6

struct drm_vblank_crtc {
	wait_queue_head_t queue;	/**< VBLANK wait queue */
	struct timeval time[DRM_VBLANKTIME_RBSIZE];	/**< timestamp of current count */
	atomic_t count;			/**< number of VBLANK interrupts */
	atomic_t refcount;		/* number of users of vblank interruptsper crtc */
	u32 last;			/* protected by dev->vbl_lock, used */
					/* for wraparound handling */
	u32 last_wait;			/* Last vblank seqno waited per CRTC */
	unsigned int inmodeset;		/* Display driver is setting mode */
	bool enabled;			/* so we don't call enable more than
					   once per disable */
};

/**
 * DRM device structure. This structure represent a complete card that
 * may contain multiple heads.
 */
struct drm_device {
	drm_pci_id_list_t *id_entry;	/* PCI ID, name, and chipset private */

	uint16_t pci_subdevice;		/* PCI subsystem device id */
	uint16_t pci_subvendor;		/* PCI subsystem vendor id */

	char		  *unique;	/* Unique identifier: e.g., busid  */
	int		  unique_len;	/* Length of unique field	   */
	struct cdev	  *devnode;	/* Device number for mknod	   */
	int		  if_version;	/* Highest interface version set */

	int		  flags;	/* Flags to open(2)		   */

				/* Locks */
	struct spinlock	  dma_lock;	/* protects dev->dma */
	struct lwkt_serialize irq_lock;	/* protects irq condition checks */
	struct lock	  dev_lock;	/* protects everything else */

	/** \name Locks */
	/*@{ */
	struct lock struct_mutex;	/**< For others */
	/*@} */

	/** \name Usage Counters */
	/*@{ */
	int		  open_count;	/* Outstanding files open	   */
	int		  buf_use;	/* Buffers in use -- cannot alloc  */
	/*@} */


	/** \name Performance counters */
	/*@{ */
	unsigned long     counters;
	enum drm_stat_type	types[15];
	atomic_t          counts[15];
	/*@} */

				/* Authentication */
	struct drm_open_hash magiclist;	/**< magic hash table */
	struct list_head magicfree;

	struct list_head filelist;

	/** \name Memory management */
	/*@{ */
	struct list_head maplist;	/**< Linked list of regions */
	int map_count;			/**< Number of mappable regions */
	struct drm_open_hash map_hash;	/**< User token hash table for maps */

	/** \name Context handle management */
	/*@{ */
	struct list_head ctxlist;	/**< Linked list of context handles */
	int ctx_count;			/**< Number of context handles */
	struct lock ctxlist_mutex;	/**< For ctxlist */

	struct idr ctx_idr;

	/*@} */

	struct drm_lock_data lock;	/* Information on hardware lock	   */

				/* DMA queues (contexts) */
	drm_device_dma_t  *dma;		/* Optional pointer for DMA support */

	int		  irq;		/* Interrupt used by board	   */
	int		  irq_type;	/* IRQ type (MSI enabled or not) */
	int		  irqrid;	/* Interrupt used by board */
	struct resource   *irqr;	/* Resource for interrupt used by board	   */
	void		  *irqh;	/* Handle from bus_setup_intr      */

	/* Storage of resource pointers for drm_get_resource_* */
	struct resource   *pcir[DRM_MAX_PCI_RESOURCE];
	int		  pcirid[DRM_MAX_PCI_RESOURCE];

	int		  pci_domain;
	int		  pci_bus;
	int		  pci_slot;
	int		  pci_func;

	/** \name Context support */
	/*@{ */
	int irq_enabled;		/**< True if irq handler is enabled */
	__volatile__ long context_flag;	/**< Context swapping flag */
	__volatile__ long interrupt_flag; /**< Interruption handler flag */
	__volatile__ long dma_flag;	/**< DMA dispatch flag */
	wait_queue_head_t context_wait;	/**< Processes waiting on ctx switch */
	int last_checked;		/**< Last context checked for DMA */
	int last_context;		/**< Last current context */
	unsigned long last_switch;	/**< jiffies at last context switch */
	/*@} */

	/** \name VBLANK IRQ support */
	/*@{ */

	/*
	 * At load time, disabling the vblank interrupt won't be allowed since
	 * old clients may not call the modeset ioctl and therefore misbehave.
	 * Once the modeset ioctl *has* been called though, we can safely
	 * disable them when unused.
	 */
	bool vblank_disable_allowed;

	/* array of size num_crtcs */
	struct drm_vblank_crtc *vblank;

	struct lock vblank_time_lock;    /**< Protects vblank count and time updates during vblank enable/disable */
	struct lock vbl_lock;
	struct timer_list vblank_disable_timer;

	u32 max_vblank_count;           /**< size of vblank counter register */

	/**
	 * List of events
	 */
	struct list_head vblank_event_list;
	struct lock event_lock;

	/*@} */

	struct sigio      *buf_sigio;	/* Processes waiting for SIGIO     */

				/* Sysctl support */
	struct drm_sysctl_info *sysctl;
	int		  sysctl_node_idx;


	drm_sg_mem_t      *sg;  /* Scatter gather memory */
	unsigned int num_crtcs;                  /**< Number of CRTCs on this device */

	unsigned long     *ctx_bitmap;
	void		  *dev_private;

	void		  *drm_ttm_bdev;

	/*@} */

	struct drm_agp_head *agp;	/**< AGP data */

	struct device *dev;             /**< Device structure */
	struct pci_dev *pdev;		/**< PCI device structure */
	int pci_vendor;			/**< PCI vendor id */
	int pci_device;			/**< PCI device id */

	struct drm_driver *driver;
	struct drm_local_map *agp_buffer_map;
	unsigned int agp_buffer_token;
	struct drm_minor *control;		/**< Control node for card */
	struct drm_minor *primary;		/**< render type primary screen head */

        struct drm_mode_config mode_config;	/**< Current mode config */

	/** \name GEM information */
	/*@{ */
	struct lock object_name_lock;
	struct idr object_name_idr;
	/*@} */
	void             *mm_private;

	void *sysctl_private;
	char busid_str[128];
	int modesetting;

	int switch_power_state;

	atomic_t unplugged; /* device has been unplugged or gone away */
};

#define DRM_SWITCH_POWER_ON 0
#define DRM_SWITCH_POWER_OFF 1
#define DRM_SWITCH_POWER_CHANGING 2

static __inline__ int drm_core_check_feature(struct drm_device *dev,
					     int feature)
{
	return ((dev->driver->driver_features & feature) ? 1 : 0);
}

static inline int drm_device_is_unplugged(struct drm_device *dev)
{
	int ret = atomic_read(&dev->unplugged);
	smp_rmb();
	return ret;
}

#if __OS_HAS_AGP
static inline int drm_core_has_AGP(struct drm_device *dev)
{
	return drm_core_check_feature(dev, DRIVER_USE_AGP);
}
#else
#define drm_core_has_AGP(dev) (0)
#endif

enum dmi_field {
        DMI_NONE,
        DMI_BIOS_VENDOR,
        DMI_BIOS_VERSION,
        DMI_BIOS_DATE,
        DMI_SYS_VENDOR,
        DMI_PRODUCT_NAME,
        DMI_PRODUCT_VERSION,
        DMI_PRODUCT_SERIAL,
        DMI_PRODUCT_UUID,
        DMI_BOARD_VENDOR,
        DMI_BOARD_NAME,
        DMI_BOARD_VERSION,
        DMI_BOARD_SERIAL,
        DMI_BOARD_ASSET_TAG,
        DMI_CHASSIS_VENDOR,
        DMI_CHASSIS_TYPE,
        DMI_CHASSIS_VERSION,
        DMI_CHASSIS_SERIAL,
        DMI_CHASSIS_ASSET_TAG,
        DMI_STRING_MAX,
};

struct dmi_strmatch {
	unsigned char slot;
	char substr[79];
};

struct dmi_system_id {
        int (*callback)(const struct dmi_system_id *);
        const char *ident;
        struct dmi_strmatch matches[4];
};
#define	DMI_MATCH(a, b) {(a), (b)}
bool dmi_check_system(const struct dmi_system_id *);

extern int	drm_notyet_flag;
extern int	drm_debug;

extern unsigned int drm_vblank_offdelay;
extern unsigned int drm_timestamp_precision;
extern unsigned int drm_timestamp_monotonic;

/* Device setup support (drm_drv.c) */
int	drm_probe(device_t kdev, drm_pci_id_list_t *idlist);
int	drm_attach(device_t kdev, drm_pci_id_list_t *idlist);
int	drm_create_cdevs(device_t kdev);

d_ioctl_t drm_ioctl;
extern int drm_lastclose(struct drm_device *dev);

				/* Device support (drm_fops.h) */
extern struct lock drm_global_mutex;
d_open_t drm_open;
d_close_t drm_close;
d_read_t drm_read;
d_kqfilter_t drm_kqfilter;
int drm_release(device_t kdev);

d_mmap_t drm_mmap;
d_mmap_single_t drm_mmap_single;
extern drm_local_map_t	*drm_getsarea(struct drm_device *dev);

void drm_cdevpriv_dtor(void *cd);

void drm_event_wakeup(struct drm_pending_event *e);

int drm_add_busid_modesetting(struct drm_device *dev,
    struct sysctl_ctx_list *ctx, struct sysctl_oid *top);

/* File operations helpers (drm_fops.c) */
extern int		drm_open_helper(struct cdev *kdev, int flags, int fmt,
					 DRM_STRUCTPROC *p,
					struct drm_device *dev,
					struct file *fp);
extern struct drm_file	*drm_find_file_by_proc(struct drm_device *dev,
					DRM_STRUCTPROC *p);

#ifdef DUMBBELL_WIP
extern int drm_gem_prime_handle_to_fd(struct drm_device *dev,
		struct drm_file *file_priv, uint32_t handle, uint32_t flags,
		int *prime_fd);
extern int drm_gem_prime_fd_to_handle(struct drm_device *dev,
		struct drm_file *file_priv, int prime_fd, uint32_t *handle);

extern int drm_prime_handle_to_fd_ioctl(struct drm_device *dev, void *data,
					struct drm_file *file_priv);
extern int drm_prime_fd_to_handle_ioctl(struct drm_device *dev, void *data,
					struct drm_file *file_priv);

extern int drm_prime_sg_to_page_addr_arrays(struct sg_table *sgt, vm_page_t *pages,
					    dma_addr_t *addrs, int max_pages);
#endif /* DUMBBELL_WIP */
extern struct sg_table *drm_prime_pages_to_sg(vm_page_t *pages, int nr_pages);
extern void drm_prime_gem_destroy(struct drm_gem_object *obj, struct sg_table *sg);

int drm_gem_dumb_destroy(struct drm_file *file,
			 struct drm_device *dev,
			 uint32_t handle);

#ifdef DUMBBELL_WIP
void drm_prime_init_file_private(struct drm_prime_file_private *prime_fpriv);
void drm_prime_destroy_file_private(struct drm_prime_file_private *prime_fpriv);
int drm_prime_add_imported_buf_handle(struct drm_prime_file_private *prime_fpriv, struct dma_buf *dma_buf, uint32_t handle);
int drm_prime_lookup_imported_buf_handle(struct drm_prime_file_private *prime_fpriv, struct dma_buf *dma_buf, uint32_t *handle);
void drm_prime_remove_imported_buf_handle(struct drm_prime_file_private *prime_fpriv, struct dma_buf *dma_buf);

int drm_prime_add_dma_buf(struct drm_device *dev, struct drm_gem_object *obj);
int drm_prime_lookup_obj(struct drm_device *dev, struct dma_buf *buf,
			 struct drm_gem_object **obj);
#endif /* DUMBBELL_WIP */

				/* Memory management support (drm_memory.h) */
#include <drm/drm_memory.h>
void	drm_mem_init(void);
void	drm_mem_uninit(void);
void	*drm_ioremap_wc(struct drm_device *dev, drm_local_map_t *map);
void	*drm_ioremap(struct drm_device *dev, drm_local_map_t *map);
void	drm_ioremapfree(drm_local_map_t *map);
int	drm_mtrr_add(unsigned long offset, size_t size, int flags);
int	drm_mtrr_del(int handle, unsigned long offset, size_t size, int flags);

int	drm_ctxbitmap_init(struct drm_device *dev);
void	drm_ctxbitmap_cleanup(struct drm_device *dev);
void	drm_ctxbitmap_free(struct drm_device *dev, int ctx_handle);

/* Locking IOCTL support (drm_lock.c) */
int	drm_lock_take(struct drm_lock_data *lock_data,
		      unsigned int context);
int	drm_lock_transfer(struct drm_lock_data *lock_data,
			  unsigned int context);
int	drm_lock_free(struct drm_lock_data *lock_data,
		      unsigned int context);

/*
 * These are exported to drivers so that they can implement fencing using
 * DMA quiscent + idle. DMA quiescent usually requires the hardware lock.
 */

unsigned long drm_get_resource_start(struct drm_device *dev,
				     unsigned int resource);
unsigned long drm_get_resource_len(struct drm_device *dev,
				   unsigned int resource);
void	drm_rmmap(struct drm_device *dev, drm_local_map_t *map);
int	drm_order(unsigned long size);
int	drm_addbufs_agp(struct drm_device *dev, struct drm_buf_desc *request);
int	drm_addbufs_pci(struct drm_device *dev, struct drm_buf_desc *request);
extern int drm_addmap(struct drm_device *dev, resource_size_t offset,
		      unsigned int size, enum drm_map_type type,
		      enum drm_map_flags flags, struct drm_local_map **map_ptr);

/* DMA support (drm_dma.c) */
int	drm_dma_setup(struct drm_device *dev);
void	drm_dma_takedown(struct drm_device *dev);
void	drm_free_buffer(struct drm_device *dev, drm_buf_t *buf);
void	drm_reclaim_buffers(struct drm_device *dev, struct drm_file *file_priv);
#define drm_core_reclaim_buffers drm_reclaim_buffers

				/* IRQ support (drm_irq.h) */
int	drm_irq_install(struct drm_device *dev);
int	drm_irq_uninstall(struct drm_device *dev);
void	drm_driver_irq_preinstall(struct drm_device *dev);
void	drm_driver_irq_postinstall(struct drm_device *dev);
void	drm_driver_irq_uninstall(struct drm_device *dev);

extern int drm_vblank_init(struct drm_device *dev, int num_crtcs);
extern int drm_wait_vblank(struct drm_device *dev, void *data,
			   struct drm_file *filp);
extern u32 drm_vblank_count(struct drm_device *dev, int crtc);
extern u32 drm_vblank_count_and_time(struct drm_device *dev, int crtc,
				     struct timeval *vblanktime);
extern void drm_send_vblank_event(struct drm_device *dev, int crtc,
				     struct drm_pending_vblank_event *e);
extern bool drm_handle_vblank(struct drm_device *dev, int crtc);
extern int drm_vblank_get(struct drm_device *dev, int crtc);
extern void drm_vblank_put(struct drm_device *dev, int crtc);
extern void drm_vblank_off(struct drm_device *dev, int crtc);
extern void drm_vblank_cleanup(struct drm_device *dev);
extern u32 drm_get_last_vbltimestamp(struct drm_device *dev, int crtc,
				     struct timeval *tvblank, unsigned flags);
extern int drm_calc_vbltimestamp_from_scanoutpos(struct drm_device *dev,
						 int crtc, int *max_error,
						 struct timeval *vblank_time,
						 unsigned flags,
						 const struct drm_crtc *refcrtc,
						 const struct drm_display_mode *mode);
extern void drm_calc_timestamping_constants(struct drm_crtc *crtc,
					    const struct drm_display_mode *mode);

/* Modesetting support */
extern void drm_vblank_pre_modeset(struct drm_device *dev, int crtc);
extern void drm_vblank_post_modeset(struct drm_device *dev, int crtc);
extern int drm_modeset_ctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);

/* AGP/PCI Express/GART support (drm_agpsupport.c) */
int	drm_device_is_agp(struct drm_device *dev);
int	drm_device_is_pcie(struct drm_device *dev);
drm_agp_head_t *drm_agp_init(void);
int	drm_agp_acquire(struct drm_device *dev);
int	drm_agp_release(struct drm_device *dev);
int	drm_agp_info(struct drm_device * dev, struct drm_agp_info *info);
int	drm_agp_enable(struct drm_device *dev, struct drm_agp_mode mode);
void	*drm_agp_allocate_memory(size_t pages, u32 type);
int	drm_agp_free_memory(void *handle);
int	drm_agp_bind_memory(void *handle, off_t start);
int	drm_agp_unbind_memory(void *handle);
int	drm_agp_alloc(struct drm_device *dev, struct drm_agp_buffer *request);
int	drm_agp_free(struct drm_device *dev, struct drm_agp_buffer *request);
int	drm_agp_bind(struct drm_device *dev, struct drm_agp_binding *request);
int	drm_agp_unbind(struct drm_device *dev, struct drm_agp_binding *request);

/* Scatter Gather Support (drm_scatter.c) */
void	drm_sg_cleanup(drm_sg_mem_t *entry);
int	drm_sg_alloc(struct drm_device *dev, struct drm_scatter_gather * request);

/* sysctl support (drm_sysctl.h) */
extern int		drm_sysctl_init(struct drm_device *dev);
extern int		drm_sysctl_cleanup(struct drm_device *dev);

/* ATI PCIGART support (ati_pcigart.c) */
int	drm_ati_pcigart_init(struct drm_device *dev,
				struct drm_ati_pcigart_info *gart_info);
int	drm_ati_pcigart_cleanup(struct drm_device *dev,
				struct drm_ati_pcigart_info *gart_info);

/* Cache management (drm_memory.c) */
void	drm_clflush_pages(vm_page_t *pages, unsigned long num_pages);

/* Locking IOCTL support (drm_drv.c) */
int	drm_lock(struct drm_device *dev, void *data,
		 struct drm_file *file_priv);
int	drm_unlock(struct drm_device *dev, void *data,
		   struct drm_file *file_priv);
int	drm_version(struct drm_device *dev, void *data,
		    struct drm_file *file_priv);
int	drm_setversion(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);

/* Misc. IOCTL support (drm_ioctl.c) */
int	drm_irq_by_busid(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int	drm_getunique(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
int	drm_setunique(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
int	drm_getmap(struct drm_device *dev, void *data,
		   struct drm_file *file_priv);
int	drm_getclient(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
int	drm_getstats(struct drm_device *dev, void *data,
		     struct drm_file *file_priv);
int	drm_getcap(struct drm_device *dev, void *data,
		     struct drm_file *file_priv);
extern int drm_setclientcap(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
int	drm_noop(struct drm_device *dev, void *data,
		 struct drm_file *file_priv);

/* Context IOCTL support (drm_context.c) */
int	drm_resctx(struct drm_device *dev, void *data,
		   struct drm_file *file_priv);
int	drm_addctx(struct drm_device *dev, void *data,
		   struct drm_file *file_priv);
int	drm_getctx(struct drm_device *dev, void *data,
		   struct drm_file *file_priv);
int	drm_switchctx(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
int	drm_newctx(struct drm_device *dev, void *data,
		   struct drm_file *file_priv);
int	drm_rmctx(struct drm_device *dev, void *data,
		  struct drm_file *file_priv);
int	drm_setsareactx(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int	drm_getsareactx(struct drm_device *dev, void *data,
			struct drm_file *file_priv);

/* Authentication IOCTL support (drm_auth.c) */
int	drm_getmagic(struct drm_device *dev, void *data,
		     struct drm_file *file_priv);
int	drm_authmagic(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);

/* Cache management (drm_cache.c) */
void drm_clflush_virt_range(char *addr, unsigned long length);

/* Buffer management support (drm_bufs.c) */
int	drm_addmap_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int	drm_rmmap_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int	drm_addbufs(struct drm_device *dev, void *data,
		    struct drm_file *file_priv);
int	drm_infobufs(struct drm_device *dev, void *data,
		     struct drm_file *file_priv);
int	drm_markbufs(struct drm_device *dev, void *data,
		     struct drm_file *file_priv);
int	drm_freebufs(struct drm_device *dev, void *data,
		     struct drm_file *file_priv);
int	drm_mapbufs(struct drm_device *dev, void *data,
		    struct drm_file *file_priv);

/* DMA support (drm_dma.c) */
int	drm_dma(struct drm_device *dev, void *data, struct drm_file *file_priv);

/* IRQ support (drm_irq.c) */
int	drm_control(struct drm_device *dev, void *data,
		    struct drm_file *file_priv);

/* AGP/GART support (drm_agpsupport.c) */
int	drm_agp_acquire_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
int	drm_agp_release_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
int	drm_agp_enable_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv);
int	drm_agp_info_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
int	drm_agp_alloc_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
int	drm_agp_free_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
int	drm_agp_unbind_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv);
int	drm_agp_bind_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);

				/* Stub support (drm_stub.h) */
extern int drm_setmaster_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);
extern int drm_dropmaster_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);

/* Scatter Gather Support (drm_scatter.c) */
int	drm_sg_alloc_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
int	drm_sg_free(struct drm_device *dev, void *data,
		    struct drm_file *file_priv);

/* consistent PCI memory functions (drm_pci.c) */
extern drm_dma_handle_t *drm_pci_alloc(struct drm_device *dev, size_t size,
				       size_t align);
void	drm_pci_free(struct drm_device *dev, drm_dma_handle_t *dmah);

			       /* sysfs support (drm_sysfs.c) */
struct drm_sysfs_class;
extern struct class *drm_sysfs_create(struct module *owner, char *name);
extern void drm_sysfs_destroy(void);
extern int drm_sysfs_device_add(struct drm_minor *minor);
extern void drm_sysfs_hotplug_event(struct drm_device *dev);
extern void drm_sysfs_device_remove(struct drm_minor *minor);
extern int drm_sysfs_connector_add(struct drm_connector *connector);
extern void drm_sysfs_connector_remove(struct drm_connector *connector);

/* Graphics Execution Manager library functions (drm_gem.c) */
int drm_gem_init(struct drm_device *dev);
void drm_gem_destroy(struct drm_device *dev);
void drm_gem_object_release(struct drm_gem_object *obj);
void drm_gem_object_free(struct kref *kref);
int drm_gem_object_init(struct drm_device *dev,
			struct drm_gem_object *obj, size_t size);
void drm_gem_private_object_init(struct drm_device *dev,
				 struct drm_gem_object *obj, size_t size);

int i915_gem_pager_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t foff, struct ucred *cred, u_short *color);
void i915_gem_pager_dtor(void * handle);

#include <drm/drm_global.h>

static inline void
drm_gem_object_reference(struct drm_gem_object *obj)
{
	kref_get(&obj->refcount);
}

static inline void
drm_gem_object_unreference(struct drm_gem_object *obj)
{
	if (obj != NULL)
		kref_put(&obj->refcount, drm_gem_object_free);
}

static inline void
drm_gem_object_unreference_unlocked(struct drm_gem_object *obj)
{
	if (obj != NULL) {
		struct drm_device *dev = obj->dev;
		DRM_LOCK(dev);
		kref_put(&obj->refcount, drm_gem_object_free);
		DRM_UNLOCK(dev);
	}
}

int drm_gem_handle_create(struct drm_file *file_priv,
			  struct drm_gem_object *obj,
			  u32 *handlep);
int drm_gem_handle_delete(struct drm_file *filp, u32 handle);

static inline void
drm_gem_object_handle_reference(struct drm_gem_object *obj)
{
	drm_gem_object_reference(obj);
	atomic_inc(&obj->handle_count);
}

struct drm_gem_object *drm_gem_object_lookup(struct drm_device *dev,
					     struct drm_file *filp,
					     u32 handle);
int drm_gem_close_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int drm_gem_flink_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int drm_gem_open_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
void drm_gem_open(struct drm_device *dev, struct drm_file *file_priv);
void drm_gem_release(struct drm_device *dev, struct drm_file *file_priv);

void drm_gem_free_mmap_offset(struct drm_gem_object *obj);
int drm_gem_create_mmap_offset(struct drm_gem_object *obj);
int drm_gem_create_mmap_offset_size(struct drm_gem_object *obj, size_t size);

int drm_gem_mmap_single(struct drm_device *dev, vm_ooffset_t *offset,
    vm_size_t size, struct vm_object **obj_res, int nprot);
void drm_gem_pager_dtr(void *obj);

struct ttm_bo_device;
int ttm_bo_mmap_single(struct ttm_bo_device *bdev, vm_ooffset_t *offset,
    vm_size_t size, struct vm_object **obj_res, int nprot);
struct ttm_buffer_object;
void ttm_bo_release_mmap(struct ttm_buffer_object *bo);

void drm_device_lock_mtx(struct drm_device *dev);
void drm_device_unlock_mtx(struct drm_device *dev);
int drm_device_sleep_mtx(struct drm_device *dev, void *chan, int flags,
    const char *msg, int timeout);
void drm_device_assert_mtx_locked(struct drm_device *dev);
void drm_device_assert_mtx_unlocked(struct drm_device *dev);

void drm_device_lock_struct(struct drm_device *dev);
void drm_device_unlock_struct(struct drm_device *dev);
int drm_device_sleep_struct(struct drm_device *dev, void *chan, int flags,
    const char *msg, int timeout);
void drm_device_assert_struct_locked(struct drm_device *dev);
void drm_device_assert_struct_unlocked(struct drm_device *dev);

void drm_compat_locking_init(struct drm_device *dev);
void drm_sleep_locking_init(struct drm_device *dev);

/* drm_modes.c */
bool drm_mode_parse_command_line_for_connector(const char *mode_option,
    struct drm_connector *connector, struct drm_cmdline_mode *mode);
struct drm_display_mode *drm_mode_create_from_cmdline_mode(
    struct drm_device *dev, struct drm_cmdline_mode *cmd);

/* Inline replacements for drm_alloc and friends */
static __inline__ void *
drm_alloc(size_t size, struct malloc_type *area)
{
	return kmalloc(size, area, M_WAITOK | M_NULLOK);
}

static __inline__ void *
drm_calloc(size_t nmemb, size_t size, struct malloc_type *area)
{
	return kmalloc(size * nmemb, area, M_WAITOK | M_NULLOK | M_ZERO);
}

static __inline__ void
drm_free(void *pt, struct malloc_type *area)
{
	kfree(pt);
}

static __inline__ void
drm_core_ioremap_wc(struct drm_local_map *map, struct drm_device *dev)
{
	map->handle = drm_ioremap_wc(dev, map);
}
static __inline__ void
drm_core_ioremap(struct drm_local_map *map, struct drm_device *dev)
{
	map->handle = drm_ioremap(dev, map);
}
static __inline__ void
drm_core_ioremapfree(struct drm_local_map *map, struct drm_device *dev)
{
	if ( map->handle && map->size )
		drm_ioremapfree(map);
}

static __inline__ struct drm_local_map *
drm_core_findmap(struct drm_device *dev, unsigned long offset)
{
	struct drm_map_list *_entry;

	list_for_each_entry(_entry, &dev->maplist, head) {
		if (offset == (unsigned long)_entry->map->handle)
			return _entry->map;
	}
	return NULL;
}

static __inline__ void drm_core_dropmap(struct drm_map *map)
{
}

#include <drm/drm_mem_util.h>

/* FreeBSD compatibility macros */
#define VM_OBJECT_WLOCK(object)		VM_OBJECT_LOCK(object)
#define VM_OBJECT_WUNLOCK(object)	VM_OBJECT_UNLOCK(object)

#define DRM_PCIE_SPEED_25 1
#define DRM_PCIE_SPEED_50 2
#define DRM_PCIE_SPEED_80 4

extern int drm_pcie_get_speed_cap_mask(struct drm_device *dev, u32 *speed_mask);

#define	drm_can_sleep()	(DRM_HZ & 1)

#endif /* __KERNEL__ */
#endif /* _DRM_P_H_ */
