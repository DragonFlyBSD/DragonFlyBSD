/*-
 * Copyright (c) 2019 Tomohiro Kusumi <tkusumi@netbsd.org>
 * Copyright (c) 2019 The DragonFly Project
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
 */

#ifndef FUSE_FUSE_H
#define FUSE_FUSE_H

#ifndef INVARIANTS
#define INVARIANTS
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/objcache.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/mutex2.h>
#include <sys/refcount.h>
#include <sys/event.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/ucred.h>
#include <sys/unistd.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <machine/atomic.h>

#include "fuse_debug.h"
#include "fuse_mount.h"
#include "fuse_abi.h"

#define VFSTOFUSE(mp) ((struct fuse_mount*)((mp)->mnt_data))
#define VTOI(vp) ((struct fuse_node*)((vp)->v_data))

#define FUSE_BLKSIZE PAGE_SIZE
#define FUSE_BLKMASK (FUSE_BLKSIZE - 1)
#define FUSE_BLKMASK64 ((off_t)(FUSE_BLKSIZE - 1))

SYSCTL_DECL(_vfs_fuse);

extern int fuse_debug;
extern struct vop_ops fuse_vnode_vops;
extern struct vop_ops fuse_spec_vops;

struct fuse_mount {
	struct mount *mp;
	struct vnode *devvp;
	struct ucred *cred;
	struct kqinfo kq;
	struct fuse_node *rfnp;
	struct mtx mnt_lock;
	struct mtx ipc_lock;
	TAILQ_HEAD(,fuse_ipc) request_head;
	TAILQ_HEAD(,fuse_ipc) reply_head;

	unsigned int refcnt;
	unsigned long unique;
	int dead;
	uint64_t nosys;
	uint32_t abi_major;
	uint32_t abi_minor;
	uint32_t max_write;
};

RB_HEAD(fuse_dent_tree, fuse_dent);

struct fuse_node {
	struct vnode *vp;
	struct vattr attr;
	struct fuse_mount *fmp;
	struct fuse_node *pfnp;
	struct mtx node_lock;
	struct fuse_dent_tree dent_head;

	uint64_t ino;
	enum vtype type;
	int nlink;
	size_t size;
	uint64_t nlookup;
	uint64_t fh;
	bool closed; /* XXX associated with closed fh */
};

struct fuse_dent {
	struct fuse_node *fnp;
	RB_ENTRY(fuse_dent) dent_entry;

	char *name;
};

struct fuse_buf {
	void *buf;
	size_t len;
};

struct fuse_ipc {
	struct fuse_mount *fmp;
	struct fuse_buf request;
	struct fuse_buf reply;
	TAILQ_ENTRY(fuse_ipc) request_entry;
	TAILQ_ENTRY(fuse_ipc) reply_entry;

	unsigned int refcnt;
	uint64_t unique;
	int done;
};

int fuse_cmp_version(struct fuse_mount*, uint32_t, uint32_t);
int fuse_mount_kill(struct fuse_mount*);
int fuse_mount_free(struct fuse_mount*);

int fuse_device_init(void);
void fuse_device_cleanup(void);

void fuse_node_new(struct fuse_mount*, uint64_t, enum vtype,
    struct fuse_node**);
void fuse_node_free(struct fuse_node*);
void fuse_dent_new(struct fuse_node*, const char*, int, struct fuse_dent**);
void fuse_dent_free(struct fuse_dent*);
void fuse_dent_attach(struct fuse_node*, struct fuse_dent*);
void fuse_dent_detach(struct fuse_node*, struct fuse_dent*);
int fuse_dent_find(struct fuse_node*, const char*, int, struct fuse_dent**);
int fuse_alloc_node(struct fuse_node*, uint64_t, const char*, int, enum vtype,
    struct vnode**);
int fuse_node_vn(struct fuse_node*, int, struct vnode**);
int fuse_node_truncate(struct fuse_node*, size_t, size_t);
void fuse_node_init(void);
void fuse_node_cleanup(void);

uint64_t fuse_fh(struct file*);
void fuse_get_fh(struct file*, uint64_t);
void fuse_put_fh(struct file*);
uint64_t fuse_nfh(struct fuse_node*);
void fuse_get_nfh(struct fuse_node*, uint64_t);
void fuse_put_nfh(struct fuse_node*);
void fuse_file_init(void);
void fuse_file_cleanup(void);

void fuse_buf_alloc(struct fuse_buf*, size_t);
void fuse_buf_free(struct fuse_buf*);
struct fuse_ipc *fuse_ipc_get(struct fuse_mount*, size_t);
void fuse_ipc_put(struct fuse_ipc*);
void *fuse_ipc_fill(struct fuse_ipc*, int, uint64_t, struct ucred*);
int fuse_ipc_tx(struct fuse_ipc*);
void fuse_ipc_init(void);
void fuse_ipc_cleanup(void);

int fuse_read(struct vop_read_args*);
int fuse_write(struct vop_write_args*);
int fuse_dio_write(struct vop_write_args*);

void fuse_hexdump(const char*, size_t);
void fuse_fill_in_header(struct fuse_in_header*, uint32_t, uint32_t, uint64_t,
    uint64_t, uint32_t, uint32_t, uint32_t);
int fuse_forget_node(struct fuse_mount*, uint64_t, uint64_t, struct ucred*);
int fuse_audit_length(struct fuse_in_header*, struct fuse_out_header*);
const char *fuse_get_ops(int);

static __inline int
fuse_test_dead(struct fuse_mount *fmp)
{
	return atomic_load_acq_int(&fmp->dead);
}

static __inline void
fuse_set_dead(struct fuse_mount *fmp)
{
	atomic_store_rel_int(&fmp->dead, 1);
}

static __inline int
fuse_test_nosys(struct fuse_mount *fmp, int op)
{
	return atomic_load_acq_64(&fmp->nosys) & (1 << op);
}

static __inline void
fuse_set_nosys(struct fuse_mount *fmp, int op)
{
	atomic_set_64(&fmp->nosys, 1 << op);
}

static __inline int
fuse_ipc_test_replied(struct fuse_ipc *fip)
{
	return atomic_load_acq_int(&fip->done);
}

static __inline void
fuse_ipc_set_replied(struct fuse_ipc *fip)
{
	atomic_store_rel_int(&fip->done, 1);
}

static __inline int
fuse_ipc_test_and_set_replied(struct fuse_ipc *fip)
{
	return atomic_cmpset_int(&fip->done, 0, 1);
}

static __inline void*
fuse_in(struct fuse_ipc *fip)
{
	return fip->request.buf;
}

static __inline size_t
fuse_in_size(struct fuse_ipc *fip)
{
	return fip->request.len;
}

static __inline void*
fuse_in_data(struct fuse_ipc *fip)
{
	return (struct fuse_in_header*)fuse_in(fip) + 1;
}

static __inline size_t
fuse_in_data_size(struct fuse_ipc *fip)
{
	return fuse_in_size(fip) - sizeof(struct fuse_in_header);
}

static __inline void*
fuse_out(struct fuse_ipc *fip)
{
	return fip->reply.buf;
}

static __inline size_t
fuse_out_size(struct fuse_ipc *fip)
{
	return fip->reply.len;
}

static __inline void*
fuse_out_data(struct fuse_ipc *fip)
{
	return (struct fuse_out_header*)fuse_out(fip) + 1;
}

static __inline size_t
fuse_out_data_size(struct fuse_ipc *fip)
{
	return fuse_out_size(fip) - sizeof(struct fuse_out_header);
}

static __inline void
fuse_knote(struct vnode *vp, int flags)
{
	if (flags)
		KNOTE(&vp->v_pollinfo.vpi_kqinfo.ki_note, flags);
}

#endif /* FUSE_FUSE_H */
