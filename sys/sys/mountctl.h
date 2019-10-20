/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_MOUNTCTL_H_
#define _SYS_MOUNTCTL_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_TIME_H_
#include <sys/time.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#endif

/*
 * General constants
 */

#define JIDMAX		32	/* id string buf[] size (incls \0) */

#define MOUNTCTL_INSTALL_VFS_JOURNAL	1
#define MOUNTCTL_REMOVE_VFS_JOURNAL	2
#define MOUNTCTL_RESYNC_VFS_JOURNAL	3
#define MOUNTCTL_STATUS_VFS_JOURNAL	4
#define MOUNTCTL_RESTART_VFS_JOURNAL	5

#define MOUNTCTL_INSTALL_BLK_JOURNAL	8
#define MOUNTCTL_REMOVE_BLK_JOURNAL	9
#define MOUNTCTL_RESYNC_BLK_JOURNAL	10
#define MOUNTCTL_STATUS_BLK_JOURNAL	11

#define MOUNTCTL_SET_EXPORT		16	/* sys/mount.h:export_args */
#define MOUNTCTL_STATVFS		17	/* get extended stats */
#define MOUNTCTL_MOUNTFLAGS		18	/* extract mountflags */

/*
 * Data structures for the journaling API
 */

struct mountctl_install_journal {
	char	id[JIDMAX];
	int	flags;		/* journaling flags */
	int	unused01;
	int64_t	membufsize;	/* backing store */
	int64_t	swapbufsize;	/* backing store */
	int64_t	transid;	/* starting with specified transaction id */
	int64_t unused02;
	int	stallwarn;	/* stall warning (seconds) */
	int	stallerror;	/* stall error (seconds) */
	int	unused03;
	int	unused04;
};

#define MC_JOURNAL_UNUSED0001		0x00000001
#define MC_JOURNAL_STOP_REQ		0x00000002	/* stop request pend */
#define MC_JOURNAL_STOP_IMM		0x00000004	/* STOP+trash fifo */
#define MC_JOURNAL_WACTIVE		0x00000008	/* wthread running */
#define MC_JOURNAL_RACTIVE		0x00000010	/* rthread running */
#define MC_JOURNAL_WWAIT		0x00000040	/* write stall */
#define MC_JOURNAL_WANT_AUDIT		0x00010000	/* audit trail */
#define MC_JOURNAL_WANT_REVERSABLE	0x00020000	/* reversable stream */
#define MC_JOURNAL_WANT_FULLDUPLEX	0x00040000	/* has ack stream */

struct mountctl_restart_journal {
	char	id[JIDMAX];
	int	flags;
	int	unused01;
};

struct mountctl_remove_journal {
	char	id[JIDMAX];
	int	flags;
};

#define MC_JOURNAL_REMOVE_TRASH		0x00000001	/* data -> trash */
#define MC_JOURNAL_REMOVE_ASSYNC	0x00000002	/* asynchronous op */

struct mountctl_status_journal {
	char	id[JIDMAX];
	int	index;
};

#define MC_JOURNAL_INDEX_ALL		-2
#define MC_JOURNAL_INDEX_ID		-1

struct mountctl_journal_ret_status {
	int	recsize;
	char	id[JIDMAX];
	int	index;
	int	flags;
	int64_t	membufsize;
	int64_t	membufused;
	int64_t	membufunacked;
	int64_t	swapbufsize;
	int64_t	swapbufused;
	int64_t	swapbufunacked;
	int64_t transidstart;
	int64_t transidcurrent;
	int64_t transidunacked;
	int64_t transidacked;
	int64_t bytessent;
	int64_t bytesacked;
	int64_t fifostalls;
	int64_t reserved[4];
	struct timeval lastack;
};

#define MC_JOURNAL_STATUS_MORETOCOME	0x00000001

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

/*
 * Support structures for the generic journaling structure
 */
struct journal_memfifo {
	int	size;		/* size (power of two) */
	int	mask;		/* index mask (size - 1) */
	int	rindex;		/* stream reader index (track fd writes) */
	int	xindex;		/* last acked / reader restart */
	int	windex;		/* stream writer index */
	char	*membase;	/* memory buffer representing the FIFO */
};

/*
 * Generic journaling structure attached to a mount point.
 */
struct journal {
	TAILQ_ENTRY(journal) jentry;
	struct file	*fp;
	char		id[JIDMAX];
	int		flags;		/* journaling flags */
	int64_t		transid;
	int64_t		total_acked;
	int64_t		fifostalls;
	struct journal_memfifo fifo;
	struct thread	wthread;
	struct thread	rthread;
};


/*
 * The jrecord structure is used to build a journaling transaction.  Since
 * a single journaling transaction might encompass very large buffers it
 * is possible for multiple transactions to be written out to the FIFO
 * in parallel and in peacemeal.
 */
struct jrecord {
	struct journal	*jo;
	char		*stream_ptr;
	int		stream_residual;
	int		stream_reserved;
	struct journal_rawrecbeg *rawp;
	struct journal_subrecord *parent;
	struct journal_subrecord *last;
	int16_t 	streamid;
	int		pushcount;
	int		pushptrgood;
	int		residual;
	int		residual_align;

	/*
	 * These fields are not used by the jrecord routines.  They may
	 * be used by higher level routines to manage multiple jrecords.
	 * See the jreclist_*() functions.
	 */
	TAILQ_ENTRY(jrecord) user_entry;
	void *user_save;
};

struct jrecord_list {
	TAILQ_HEAD(, jrecord) list;
	int16_t		streamid;
};

#endif	/* kernel or kernel structures */

#if defined(_KERNEL)

struct namecache;
struct ucred;
struct uio;
struct xio;
struct vnode;
struct vattr;
struct vm_page;

void journal_create_threads(struct journal *jo);
void journal_destroy_threads(struct journal *jo, int flags);

/*
 * Primary journal record support procedures
 */
void jrecord_init(struct journal *jo,
			struct jrecord *jrec, int16_t streamid);
struct journal_subrecord *jrecord_push(
			struct jrecord *jrec, int16_t rectype);
void jrecord_pop(struct jrecord *jrec, struct journal_subrecord *parent);
void jrecord_leaf(struct jrecord *jrec,
			int16_t rectype, void *ptr, int bytes);
void jrecord_leaf_uio(struct jrecord *jrec,
			int16_t rectype, struct uio *uio);
void jrecord_leaf_xio(struct jrecord *jrec,
			int16_t rectype, struct xio *xio);
struct journal_subrecord *jrecord_write(struct jrecord *jrec,
			int16_t rectype, int bytes);
void jrecord_done(struct jrecord *jrec, int abortit);

/*
 * Rollup journal record support procedures
 */
void jrecord_write_path(struct jrecord *jrec,
			int16_t rectype, struct namecache *ncp);
void jrecord_write_vattr(struct jrecord *jrec, struct vattr *vat);
void jrecord_write_cred(struct jrecord *jrec, struct thread *td,
			struct ucred *cred);
void jrecord_write_vnode_ref(struct jrecord *jrec, struct vnode *vp);
void jrecord_write_vnode_link(struct jrecord *jrec, struct vnode *vp,
			struct namecache *notncp);
void jrecord_write_pagelist(struct jrecord *jrec, int16_t rectype,
			struct vm_page **pglist, int *rtvals, int pgcount,
			off_t offset);
void jrecord_write_uio(struct jrecord *jrec, int16_t rectype, struct uio *uio);
void jrecord_file_data(struct jrecord *jrec, struct vnode *vp,
			off_t off, off_t bytes);

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_JOURNAL);
MALLOC_DECLARE(M_JFIFO);
#endif

#else

#include <sys/cdefs.h>

__BEGIN_DECLS
int	mountctl (const char *path, int op, int fd, void *ctl, int ctllen,
		  void *buf, int buflen);
__END_DECLS

#endif	/* kernel */

#endif	/* header */
