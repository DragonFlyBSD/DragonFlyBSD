/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Antonio Huete Jimenez <tuxillo@quantumachine.net>
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
 *
 */

#ifndef _SYS_VFS_DIRFS_DIRFS_H_
#define _SYS_VFS_DIRFS_DIRFS_H_

#include <unistd.h>

#include <sys/lockf.h>
#include <sys/stat.h>
#include <sys/vnode.h>

MALLOC_DECLARE(M_DIRFS);
MALLOC_DECLARE(M_DIRFS_NODE);
MALLOC_DECLARE(M_DIRFS_MISC);

#ifndef KTR_DIRFS
#define KTR_DIRFS KTR_ALL
#endif

#define DIRFS_NOFD	-1	/* No fd present */

#define DIRFS_ROOT	0x00000001
#define DIRFS_PASVFD	0x00000002

#define DIRFS_TXTFLG "pasvfd"

/* Used for buffer cache operations */
#define BSIZE	16384
#define BMASK	(BSIZE - 1)

/*
 * XXX This should be temporary. A semi-proper solution would be to expose
 * below prototypes in the _KERNEL_VIRTUAL case.
 */
extern int getdirentries(int, char *, int, long *);
extern int statfs(const char *, struct statfs *);

/*
 * Debugging macros. The impact should be determined and in case it has a
 * considerable performance penalty, it should be enclosed in a DEBUG #ifdef.
 */
#define debug_called() do {					\
		dbg(9, "called\n", __func__);			\
} while(0)

#define dbg(lvl, fmt, ...) do {					\
		debug(lvl, "%s: " fmt, __func__, ##__VA_ARGS__);	\
} while(0)

#define debug_node(s) do {						\
		dbg(5, "mode=%u flags=%u dn_name=%s "			\
		    "uid=%u gid=%u objtype=%u nlinks=%d "		\
		    "size=%jd ctime=%ju atime=%ju mtime=%ju\n",		\
		    s->dn_mode, s->dn_flags, s->dn_name,		\
		    s->dn_uid, s->dn_gid, s->dn_type,			\
		    s->dn_links, s->dn_size,				\
		    s->dn_ctime, s->dn_atime,				\
		    s->dn_mtime);					\
} while(0)

#define debug_node2(n) do {						\
		dbg(5, "dnp=%p name=%s fd=%d parent=%p vnode=%p "	\
		    "refcnt=%d state=%s\n",				\
		    n, n->dn_name, n->dn_fd, n->dn_parent, n->dn_vnode,	\
		    n->dn_refcnt, dirfs_flag2str(n));			\
} while(0)

/*
 * Locking macros
 */
#define dirfs_node_islocked(n)	(lockstatus(&(n)->dn_lock,curthread) == LK_EXCLUSIVE)
#define dirfs_node_lock(n)	lockmgr(&(n)->dn_lock, LK_EXCLUSIVE|LK_RETRY)
#define dirfs_node_unlock(n) 	lockmgr(&(n)->dn_lock, LK_RELEASE)
#define dirfs_mount_lock(m)	lockmgr(&(m)->dm_lock, LK_EXCLUSIVE|LK_RETRY)
#define dirfs_mount_unlock(m)	lockmgr(&(m)->dm_lock, LK_RELEASE)
#define dirfs_mount_gettoken(m)	lwkt_gettoken(&(m)->dm_token)
#define dirfs_mount_reltoken(m)	lwkt_reltoken(&(m)->dm_token)

#define dirfs_node_isroot(n)	(n->dn_state & DIRFS_ROOT)

/*
 * Main in-memory node structure which will represent a host file when active.
 * Upon VOP_NRESOLVE() an attempt to initialize its generic fields will be made
 * via a fstatat(2)/lstat(2) call.
 */
struct dirfs_node {
	enum vtype		dn_type;	/* Node type. Same as vnode
						   type for simplicty */

	int			dn_state;	/* Node state flags */

	TAILQ_ENTRY(dirfs_node)	dn_fdentry;	/* Passive fd cache */
	RB_ENTRY(dirfs_node)	dn_rbentry;	/* Inode no. lookup */

	int			dn_refcnt;	/* Refs from children */
	int			dn_fd;		/* File des. for open(2) */

	struct dirfs_node *	dn_parent;	/* Pointer to parent node */

	struct vnode *          dn_vnode;	/* Reference to its vnode on
						   the vkernel scope */
	char *			dn_name;
	int			dn_namelen;

        struct lockf            dn_advlock;
	struct lock		dn_lock;

	uint32_t		dn_st_dev;	/* Device number */

	/* Generic attributes */
	ino_t			dn_ino;
	long			dn_blocksize;
	uid_t			dn_uid;
	gid_t			dn_gid;
	mode_t			dn_mode;
	int			dn_flags;
	nlink_t			dn_links;
	int32_t			dn_atime;
	int32_t			dn_atimensec;
	int32_t			dn_mtime;
	int32_t			dn_mtimensec;
	int32_t			dn_ctime;
	int32_t			dn_ctimensec;
	unsigned long		dn_gen;
	off_t			dn_size;
};
typedef struct dirfs_node *dirfs_node_t;

/*
 * In-memory dirfs mount structure. It corresponds to a mounted
 * dirfs filesystem.
 */
struct dirfs_mount {
	RB_HEAD(, dn_rbentry) dm_inotree;
	TAILQ_HEAD(, dirfs_node) dm_fdlist;

	struct lock	 	dm_lock;
	struct lwkt_token	dm_token;
	dirfs_node_t		dm_root;	/* Root dirfs node */
	struct mount *		dm_mount;
	int			dm_rdonly;

	int			dm_fd_used;	/* Opened file descriptors */

	char			dm_path[MAXPATHLEN];
};
typedef struct dirfs_mount *dirfs_mount_t;

/*
 * VFS <-> DIRFS conversion macros
 */
#define VFS_TO_DIRFS(mp)	((dirfs_mount_t)((mp)->mnt_data))
#define DIRFS_TO_VFS(dmp)	((struct mount *)((dmp)->dm_mount))
#define VP_TO_NODE(vp)		((dirfs_node_t)((vp)->v_data))
#define NODE_TO_VP(dnp)		((dnp)->dn_vnode)

/* Misc stuff */
extern int debuglvl;
extern int dirfs_fd_limit;
extern int dirfs_fd_used;
extern long passive_fd_list_miss;
extern long passive_fd_list_hits;

extern struct vop_ops dirfs_vnode_vops;

/*
 * Misc functions for node flags and reference count
 */
static __inline void
dirfs_node_ref(dirfs_node_t dnp)
{
	atomic_add_int(&dnp->dn_refcnt, 1);
}

static __inline int
dirfs_node_unref(dirfs_node_t dnp)
{
	/*
	 * Returns non-zero on last unref.
	 */
	KKASSERT(dnp->dn_refcnt > 0);
	return (atomic_fetchadd_int(&dnp->dn_refcnt, -1) == 1);
}

static __inline void
dirfs_node_setflags(dirfs_node_t dnp, int flags)
{
	atomic_set_int(&dnp->dn_state, flags);
}

static __inline void
dirfs_node_clrflags(dirfs_node_t dnp, int flags)
{
	atomic_clear_int(&dnp->dn_state, flags);
}


/*
 * Prototypes
 */
dirfs_node_t dirfs_node_alloc(struct mount *);
int dirfs_node_stat(int, const char *, dirfs_node_t);
int dirfs_nodetype(struct stat *);
void dirfs_node_setname(dirfs_node_t, const char *, int);
char *dirfs_node_fullpath(dirfs_mount_t, const char *);
int dirfs_node_free(dirfs_mount_t, dirfs_node_t);
void dirfs_node_drop(dirfs_mount_t dmp, dirfs_node_t dnp);
void dirfs_node_setpassive(dirfs_mount_t dmp, dirfs_node_t dnp, int state);
void dirfs_alloc_vp(struct mount *, struct vnode **, int, dirfs_node_t);
void dirfs_free_vp(dirfs_mount_t, dirfs_node_t);
int dirfs_alloc_file(dirfs_mount_t, dirfs_node_t *, dirfs_node_t,
    struct namecache *, struct vnode **, struct vattr *, int);
dirfs_node_t dirfs_findfd(dirfs_mount_t dmp, dirfs_node_t cur,
			char **pathto, char **pathfree);
void dirfs_dropfd(dirfs_mount_t dmp, dirfs_node_t dnp1, char *pathfree);
char *dirfs_node_absolute_path(dirfs_mount_t, dirfs_node_t, char **);
char *dirfs_node_absolute_path_plus(dirfs_mount_t, dirfs_node_t,
			char *, char **);
int dirfs_open_helper(dirfs_mount_t, dirfs_node_t, int, char *);
int dirfs_close_helper(dirfs_node_t);
int dirfs_node_refcnt(dirfs_node_t);
char *dirfs_flag2str(dirfs_node_t);
int dirfs_node_getperms(dirfs_node_t, int *, int *, int *);
int dirfs_node_chflags(dirfs_node_t, int, struct ucred *);
int dirfs_node_chtimes(dirfs_node_t);
int dirfs_node_chmod(dirfs_mount_t, dirfs_node_t, mode_t cur_mode);
int dirfs_node_chown(dirfs_mount_t, dirfs_node_t,
			uid_t cur_uid, uid_t cur_gid, mode_t cur_mode);
int dirfs_node_chsize(dirfs_node_t, off_t);
void debug(int, const char *, ...);

#endif /* _SYS_VFS_DIRFS_DIRFS_H_ */
